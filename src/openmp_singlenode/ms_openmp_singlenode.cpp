#include <core/core.hpp>
#include <utils/utils.hpp>
#include <chrono>
#include <vector>
#include <algorithm>
#include <fstream>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <omp.h>
#include <fcntl.h>
#include <queue>
#include <numeric>

int main(int argc,char*argv[]) noexcept{
    uint64_t records_num=0;
    size_t threads_num=0;
    bool verbose = false;
    std::string in_filename="";
    std::string out_filename="";
    size_t memory_limit=MAX_MEMORY_LIMIT;
    bool success=parse_cli_args(argc,argv,threads_num,verbose,in_filename,out_filename,memory_limit);
    if(!success){
        std::cout << "Exiting..." << std::endl;
        return 1;
    }
    if(in_filename==""){
        std::cout << "please specify the input file path" << std::endl;
        return 1;
    }
    if(out_filename==""){
        std::cout << "please specify the output file path" << std::endl;
        return 1;
    }
    omp_set_num_threads(threads_num);
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<PosKeyPair> pos_key_data= std::move(read_records(in_filename,memory_limit));
    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:pos_key_data){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }

    std::vector<IndexPair> sorted_ranges;
    size_t chunk_base = pos_key_data.size() / threads_num;
    size_t remainder = pos_key_data.size() % threads_num;
    
    size_t start = 0;
    for (size_t i = 0; i < threads_num; ++i) {
        size_t chunk_size = chunk_base + (i < remainder ? 1 : 0);
        size_t end = start + chunk_size;
        sorted_ranges.push_back(IndexPair(start,end));
        start = end;
    }

    #pragma omp parallel for shared(pos_key_data) schedule(static)
    for(int i=0;i<threads_num;i++){
        const int start=sorted_ranges[i].first;
        const int end=sorted_ranges[i].second;
        radix_sort_slice(pos_key_data,start,end);
    }

    // estimating  the ranks using ms_select
    std::vector<uint64_t> pivots(threads_num-1);
    
    #pragma omp parallel for shared(pivots) shared(sorted_ranges) schedule(static)
    for(int i=1;i<=pivots.size();i++){
        int rank=i*(pos_key_data.size()/threads_num);
        pivots[i-1]=ms_select2(pos_key_data,sorted_ranges,rank);
    }
    
    std::vector<std::vector<IndexPair>> bucket_subranges(threads_num);
    
    for (size_t j=0;j<sorted_ranges.size();++j) {
        const size_t start=sorted_ranges[j].first;
        const size_t end=sorted_ranges[j].second;
        auto begin_it = pos_key_data.begin() + start;
        auto end_it = pos_key_data.begin() + end;

        size_t last_idx = start;

        for (size_t b = 0; b < threads_num; ++b) {
            auto low = pos_key_data.begin() + last_idx;
            
            auto high = (b < pivots.size())
            ? std::upper_bound(low, end_it, pivots[b],
                [](uint64_t val, const PosKeyPair& elem) {
                    return val < elem.key;
                })
                : end_it;
                
            if (low < high) {
                bucket_subranges[b].emplace_back(low - pos_key_data.begin(), high - pos_key_data.begin());
            }
                
            last_idx = high - pos_key_data.begin();
            if (last_idx >= end) break;
        }
        
    }
    
    
    PosKeyVec result;
    std::vector<PosKeyVec> merged_ranges(threads_num);
    std::vector<size_t> bytes_nums(threads_num);
    const unsigned int payload_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    #pragma omp parallel for shared(pos_key_data) shared(bucket_subranges) schedule(static)
    for(int i=0;i<threads_num;i++){
            merged_ranges[i]=std::move(k_way_merge_heap(pos_key_data,bucket_subranges[i]));
            size_t local_bytes=0;
            #pragma omp simd reduction(+:local_bytes)
            for(size_t j=0;j<merged_ranges[i].size();j++){
                local_bytes+=payload_header_size+merged_ranges[i][j].len;
            }
            bytes_nums[i]=local_bytes;
    }
    
    std::ifstream in_file(in_filename,std::ifstream::binary | std::ifstream::ate);
    size_t file_size= in_file.tellg();
    in_file.close();
    std::ofstream out_file(out_filename,std::ofstream::binary);
    out_file.seekp(file_size-1);
    out_file.put(0);
    out_file.close();
    std::vector<size_t> offsets(threads_num);
    size_t byte_offset=0;
    for(int i=0;i<threads_num;i++){
        offsets[i]=byte_offset;
        byte_offset+=bytes_nums[i];
    }
    const size_t payload_thread_max=memory_limit/threads_num;
    
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < threads_num; ++i) {
        size_t thread_offset = offsets[i];                  // Start of this thread's output region
        const auto& pkp_list = merged_ranges[i];            // Sorted records for this thread
        std::ifstream in_file(in_filename, std::ios::binary);
        
        char* payload_buf=new char[payload_max];                      // Input record buffer
        char* out_buf=new char[payload_thread_max];                   // Output write buffer
        size_t out_pos = 0;                                 // Current write buffer position
        
        int fd = open(out_filename.c_str(), O_RDWR);
        if (fd < 0) {
            perror("open");
            continue;
        }

        for (size_t j=0;j<pkp_list.size();++j) {
            in_file.seekg(pkp_list[j].offset + sizeof(uint64_t) + sizeof(uint64_t));
            in_file.read(payload_buf, pkp_list[j].len);
            size_t record_size = sizeof(pkp_list[j].key) + sizeof(pkp_list[j].len) + pkp_list[j].len;
            if (out_pos + record_size > payload_thread_max) {
                ssize_t written = pwrite(fd, out_buf, out_pos, thread_offset);
                if (written < 0) {
                    perror("pwrite");
                    break;
                }
                thread_offset += written;
                out_pos = 0;
            }
            std::memcpy(out_buf + out_pos, &pkp_list[j].key, sizeof(pkp_list[j].key));
            out_pos += sizeof(pkp_list[j].key);
            std::memcpy(out_buf + out_pos, &pkp_list[j].len, sizeof(pkp_list[j].len));
            out_pos += sizeof(pkp_list[j].len);
            std::memcpy(out_buf + out_pos, payload_buf, pkp_list[j].len);
            out_pos += pkp_list[j].len;
        }
        if (out_pos > 0) {
            ssize_t written = pwrite(fd, out_buf, out_pos, thread_offset);
            if (written < 0) {
                perror("pwrite");
            }
        }
        delete out_buf;
        delete payload_buf;
        in_file.close();
        close(fd);
    }
    //openmp implementation
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "time(ms):" << duration.count() << std::endl;

    return 0;
}
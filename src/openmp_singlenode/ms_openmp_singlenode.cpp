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
    std::string out_filename="";
    std::string in_filename="";
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
    std::vector<PosKeyPair> pos_key_data= std::move(read_records_pread(in_filename,memory_limit));
    if (verbose){
        size_t i=0;
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
    std::cout<< "read - start sort " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now()-start_time) << std::endl;
    #pragma omp parallel for shared(pos_key_data) schedule(static)
    for(int i=0;i<threads_num;i++){
        const int start=sorted_ranges[i].first;
        const int end=sorted_ranges[i].second;
        radix_sort_buffer(pos_key_data.data()+start,end-start);
    }

    // estimating  the ranks using ms_select
    std::vector<uint64_t> pivots(threads_num-1);
    std::cout<< "end sort - start pivots " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now()-start_time) << std::endl;
    #pragma omp parallel for shared(pivots) shared(sorted_ranges) schedule(static)
    for(int i=1;i<=pivots.size();i++){
        size_t rank=i*(pos_key_data.size()/threads_num);
        pivots[i-1]=ms_select2(pos_key_data,sorted_ranges,rank);
    }
    
    std::vector<std::vector<IndexPair>> bucket_subranges(threads_num,std::vector<IndexPair>(threads_num));
    #pragma omp parallel for shared(pos_key_data)
    for (size_t j=0;j<sorted_ranges.size();++j) {
        const size_t start=sorted_ranges[j].first;
        const size_t end=sorted_ranges[j].second;
        build_pivot_subrange(start,end,j,pivots,pos_key_data,bucket_subranges);
    }
    std::vector<size_t> merge_offsets(threads_num);
    std::vector<size_t> total_sizes(threads_num);
    for(int i=0;i<threads_num;i++){
        size_t total_size = 0;
        for (const auto& range : bucket_subranges[i]) {
            total_size += (range.second - range.first);
        }
        total_sizes[i]=total_size;
    }
    
    size_t merge_offset=0;
    for(int i=0;i<threads_num;i++){
        merge_offsets[i]=merge_offset;
        merge_offset+=total_sizes[i];
    }
    PosKeyVec result;
    std::vector<PosKeyVec> merged_ranges(threads_num);
    std::vector<size_t> bytes_nums(threads_num);
    const unsigned long payload_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    std::vector<PosKeyPair> final_result(pos_key_data.size());
    std::cout<< "end pivots start write" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now()-start_time) << std::endl;
    #pragma omp parallel for shared(pos_key_data,final_result) shared(bucket_subranges) schedule(static)
    for(int i=0;i<threads_num;i++){
            k_way_merge_buffer(pos_key_data.data(),bucket_subranges[i],final_result.data()+merge_offsets[i],total_sizes[i]);
            size_t local_bytes=0;
            #pragma omp simd reduction(+:local_bytes)
            for(size_t j=0;j<total_sizes[i];j++){
                local_bytes+=payload_header_size+final_result[merge_offsets[i]+j].len;
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
    std::cout<< "last stage " <<std::chrono::high_resolution_clock::now() << std::endl;
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < threads_num; ++i) {
        buffered_poskey_write_pread(
            in_filename,out_filename,
            offsets[i],final_result.data()+merge_offsets[i],
            total_sizes[i],payload_max,payload_thread_max
        );
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "time(ms):" << duration.count() << std::endl;

    return 0;
}
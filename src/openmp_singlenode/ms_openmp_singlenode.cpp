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
#include <omp.h>

using PosKeyVec=std::vector<PosKeyPair>;

using IndexPair=std::pair<unsigned long,unsigned long>;
using SortResult=std::tuple<unsigned long,unsigned long,unsigned long>;

int main(int argc,char*argv[]){
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

    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<PosKeyPair> pos_key_data= read_records(in_filename,memory_limit);
    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:pos_key_data){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }

    const int threads_work_load=pos_key_data.size()/threads_num;
    #pragma omp parallel for shared(pos_key_pair)
    for(int i=0;i<threads_num;i++){
        const int start=i*threads_work_load;
        const int end=std::min(pos_key_data.size(),(unsigned long)start + threads_work_load);
        std::sort(
            pos_key_data.begin()+start,pos_key_data.begin()+end,
            [](const PosKeyPair& a,const PosKeyPair& b){return a.key<b.key;}
        );
    }
    
    // estimating  the ranks using ms_select
    
    std::vector<uint64_t> pivots(threads_num-1);
    
    std::vector<IndexPair> sub_ranges(threads_num);
    for(int i=0;i<threads_num;i++){
        const int start=i*threads_work_load;
        const int end=std::min(pos_key_data.size(),(unsigned long)start + threads_work_load);
        sub_ranges.push_back(IndexPair(start,end));
    }

    #pragma omp parallel for shared(pivots) private(sub_ranges)
    for(int i=0;i<pivots.size();i++){
        int rank=i*(pos_key_data.size()/threads_num);
        pivots[i]=ms_select(pos_key_data,sub_ranges,rank);
    }



    std::vector<std::vector<IndexPair>> bucket_subranges(threads_num);

    for (const auto& [start, end] : sub_ranges) {
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
    #pragma omp parallel for shared(pos_key_data) shared(bucket_subranges)
    for(int i=0;i<threads_num;i++){
        const int start=i*threads_work_load;
        const int end=std::min(pos_key_data.size(),(unsigned long)start + threads_work_load);
        PosKeyVec merged= k_way_merge_from_ranges(pos_key_data,bucket_subranges[i]);
        merged_ranges[i]=merged;
    }
    const unsigned int payload_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    std::ofstream out_file(out_filename,std::ofstream::binary);
    std::ifstream in_file(in_filename,std::ofstream::binary);
    char buffer[payload_max];
    //Note: buffering cannot be applied as we don't know where the records are the be read from
    //there is still a form of buffering in the ofstream library
    for(auto& range:merged_ranges){
        for(auto& pkp:range){
            in_file.seekg(pkp.offset+payload_header_size);
            in_file.read(buffer,pkp.len);
            out_file.write(reinterpret_cast<char*>(&pkp.key),sizeof(uint64_t));
            out_file.write(reinterpret_cast<char*>(&pkp.len),sizeof(uint64_t));
            out_file.write(buffer,pkp.len);
        }
    }
    in_file.close();
    out_file.close();
    
    //openmp implementation
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "time(ms):" << duration.count() << std::endl;

    return 0;
}
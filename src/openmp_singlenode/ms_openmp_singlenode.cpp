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
    std::string filename="";
    bool success=parse_cli_args(argc,argv,threads_num,verbose,filename);
    if(!success){
        std::cout << "Exiting..." << std::endl;
        return 1;
    }
    if(filename==""){
        std::cout << "please specify filename" << std::endl;
        return 1;
    }
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::ifstream in_file(filename,std::ifstream::binary | std::ios::ate);
    std::streamsize file_size= in_file.tellg();
    in_file.seekg(0);
    uint64_t max_file_payload_size;
    in_file.read(reinterpret_cast<char*>(&max_file_payload_size),sizeof(uint64_t));    
    if(max_file_payload_size != payload_max){
        std::cout<< "error:the maximum payload size should correspond to " << payload_max << " but it isn't.\nExiting..." << std::endl;
        in_file.close();
        return 1;
    }
    records_num=0;
    in_file.read(reinterpret_cast<char*>(&records_num),sizeof(uint64_t));
    
    std::vector<uint64_t> offsets(records_num);
    in_file.read(reinterpret_cast<char*>(offsets.data()),sizeof(uint64_t)*records_num);
    std::cout << "starting with:\n" 
              << "\tnumber of records:\t" << records_num 
              << "\tmax payload size:\t" << max_file_payload_size << std::endl;

    const unsigned int header_offset=in_file.tellg();
    unsigned long records_count=0;
    size_t current_size=0;
    std::vector<PosKeyPair> pos_key_data;
    const unsigned int max_record_size=payload_max+sizeof(uint32_t)+sizeof(uint64_t);
    const unsigned int buffer_size= std::min(MAX_MEMORY_LIMIT,max_record_size*records_num);
    char* buffer=new char[buffer_size];
    const unsigned int payload_header_size=sizeof(uint32_t)+sizeof(uint64_t);
    const unsigned int file_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    const unsigned int offset_header_size=sizeof(uint64_t)*records_num;
    // we read only keys and store the indexes to apply std::sort
    unsigned int buffer_offset=0;
    unsigned int record_offset=0;
    while(records_count<records_num){
        unsigned int buffer_start=buffer_offset;
        //avoid reading over the buffer length
        unsigned int buffer_length=std::min(buffer_size,(uint)file_size-(uint)(file_header_size) -buffer_start);
        in_file.seekg(file_header_size+offset_header_size+buffer_offset);
        in_file.read(buffer,buffer_length);
        //Note: header is not read in the buffer
        //If the offset exceed the payload header size stop
        while( record_offset + payload_header_size< buffer_length && records_count < records_num){
            PosKeyPair pkp;
            uint32_t payload_len=0;
            std::memcpy(&payload_len,buffer+record_offset,sizeof(uint32_t));
            std::memcpy(&pkp.key,buffer+record_offset+sizeof(uint32_t),sizeof(uint64_t));
            pkp.pos=records_count;
            pos_key_data.push_back(std::move(pkp));
            record_offset+=payload_len+payload_header_size;
            ++records_count;
        }
        buffer_offset=record_offset;
        record_offset=0;
    }
    in_file.close();
    delete[] buffer;
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
            [](const PosKeyPair& a,const PosKeyPair& b){return a.key<=b.key;}
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

    for(const auto& range:merged_ranges){
        for(const auto& pkp:range){
            result.push_back(pkp);
        }
    }
    //openmp implementation
    auto end_time = std::chrono::high_resolution_clock::now();
    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:result){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;

    return 0;
}
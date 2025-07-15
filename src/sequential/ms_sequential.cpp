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
#include <fcntl.h>

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
    std::cout<<"starting from " << in_filename << " to "<< out_filename << " with a limit of "<< memory_limit/(1024UL*1024L*1024L)<< "GBs" <<std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<PosKeyPair> pos_key_data=std::move(read_records_pread(in_filename,memory_limit));
    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:pos_key_data){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }
    std::sort(
        pos_key_data.begin(),pos_key_data.end(),
        [](const PosKeyPair& a,const PosKeyPair& b){return a.key<b.key;}
    );
    std::ifstream in_file(in_filename,std::ifstream::binary | std::ifstream::ate);
    size_t new_file_size= in_file.tellg();
    in_file.close();
    std::ofstream out_file(out_filename,std::ofstream::binary);
    out_file.seekp(new_file_size-1);
    out_file.put(0);
    out_file.close();

    buffered_poskey_write_pread(
        in_filename,out_filename,
        0,pos_key_data.data(),pos_key_data.size(),
        payload_max,memory_limit
    );
    
    if (verbose){
        std::cout<< "done writing" << std::endl;
        unsigned int i=0;
        for(const auto& pkp:pos_key_data){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;

    return 0;
}
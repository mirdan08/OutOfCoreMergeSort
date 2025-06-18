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
    std::vector<PosKeyPair> pos_key_data;
    read_payloads(
        in_file,MAX_MEMORY_LIMIT,records_num,pos_key_data
    );
    std::sort(
        pos_key_data.begin(),pos_key_data.end(),
        [](const PosKeyPair& a,const PosKeyPair& b){return a.key<b.key;}
    );

    if (verbose){
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
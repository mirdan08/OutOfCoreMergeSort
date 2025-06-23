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

    std::vector<PosKeyPair> pos_key_data=read_records(in_filename,memory_limit);
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

    const unsigned int payload_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    std::ofstream out_file(out_filename,std::ofstream::binary);
    std::ifstream in_file(in_filename,std::ofstream::binary);
    char buffer[payload_max];
    //Note: buffering cannot be applied as we don't know where the records are the be read from
    //there is still a form of buffering in the ofstream library
    for(int i=0;i<pos_key_data.size();i++){
        in_file.seekg(pos_key_data[i].offset+payload_header_size);
        in_file.read(buffer,pos_key_data[i].len);
        out_file.write(reinterpret_cast<char*>(&pos_key_data[i].key),sizeof(uint64_t));
        out_file.write(reinterpret_cast<char*>(&pos_key_data[i].len),sizeof(uint64_t));
        out_file.write(buffer,pos_key_data[i].len);
    }
    in_file.close();
    out_file.close();
    delete[] buffer;
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
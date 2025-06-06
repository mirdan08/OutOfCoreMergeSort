#include <core.hpp>
#include <utils.hpp>
#include <chrono>
#include <vector>
#include <algorithm>
#include<fstream>
#include<memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>

int main(int argc,char*argv[]){
    size_t records_num=0;
    size_t threads_num=0;
    bool verbose = false;
    bool success=parse_cli_args(argc,argv,records_num,threads_num,verbose);

    if(!success){
        std::cout << "exiting" << std::endl;
        return 1;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::ifstream in_file("input.pms",std::ifstream::binary);
    size_t max_file_payload_size;
    in_file.read(reinterpret_cast<char*>(&max_file_payload_size),sizeof(size_t));    
    if(max_file_payload_size != PAYLOAD_MAX){
        std::cout<< "error:the maximum payload size should correspond to " << PAYLOAD_MAX << " but it isn't\nExiting..." << std::endl;
        in_file.close();
        return 1;
    }
    size_t records_num;
    in_file.read(reinterpret_cast<char*>(&records_num),sizeof(size_t));

    const unsigned int header_offset=in_file.tellg();

    unsigned long records_count=0;
    size_t current_size=0;
    std::vector<PosKeyPair> pos_key_data;
    const unsigned int record_size=PAYLOAD_MAX+sizeof(uint32_t)+sizeof(unsigned long);
    const unsigned int buffers_num= (record_size*records_num+MAX_MEMORY_LIMIT-1)/MAX_MEMORY_LIMIT;
    const unsigned int buffer_size= std::min(MAX_MEMORY_LIMIT,record_size*records_num);
    char* buffer=new char[buffer_size] ;
    // we read only keys and store the indexes to apply std::sort
    for(int i=0;i<buffers_num;++i){
        unsigned int buffer_start=i*buffer_size;
        //avoid reading over the buffer length
        unsigned int buffer_length=std::min(buffer_size,record_size*buffers_num-buffer_start);    
        in_file.read(buffer,buffer_length);
        unsigned int bytes_read=0;
        //Note: header is not read in the buffer
        while( bytes_read< buffer_length ){
            PosKeyPair pkp;
            std::memcpy(&pkp.key,buffer+records_count*record_size+sizeof(uint32_t),sizeof(unsigned long));
            pkp.pos=records_count;
            pos_key_data.push_back(std::move(pkp));
            records_count++;
            bytes_read+=record_size;
        }
    }
    in_file.close();
    delete[] buffer;

    std::cout << "starting with:\n" 
              << "\tnumber of records:\t" << records_num 
              << "\tmax payload size:\t" << PAYLOAD_MAX << std::endl;
    std::sort(
        pos_key_data.begin(),pos_key_data.end(),
        [](const PosKeyPair& a,const PosKeyPair& b){return a.key<b.key;}
    );
    unsigned int i=0;
    for(const auto& pkp:pos_key_data){
        std::cout<< i << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;

    return 0;
}
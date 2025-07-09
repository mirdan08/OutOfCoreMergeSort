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
    std::cout<<"starting from " << in_filename << " to "<< out_filename << " with a limit of "<< memory_limit <<std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    std::cout<< "before read" << std::endl;
    
    std::vector<PosKeyPair> pos_key_data=read_records(in_filename,memory_limit);
    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:pos_key_data){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }
    std::cout<< "start sorting" << std::endl;
    std::sort(
        pos_key_data.begin(),pos_key_data.end(),
        [](const PosKeyPair& a,const PosKeyPair& b){return a.key<b.key;}
    );
    std::cout<< "end sorting" << std::endl;
    const unsigned int payload_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    std::ofstream out_file(out_filename,std::ofstream::binary);

    std::ifstream in_file(in_filename,std::ofstream::binary);
    
    char* payload_buf=new char[payload_max];                      // Input record buffer
    char* out_buf=new char[memory_limit];                   // Output write buffer
    size_t out_pos = 0;                                 // Current write buffer position
    
    int fd = open(out_filename.c_str(), O_RDWR);
    if (fd < 0) {
        perror("open");
    }
    size_t offset=0;
    for (const auto& pkp : pos_key_data) {
        in_file.seekg(pkp.offset + sizeof(uint64_t) + sizeof(uint64_t));
        in_file.read(payload_buf, pkp.len);
        size_t record_size = sizeof(pkp.key) + sizeof(pkp.len) + pkp.len;
        if (out_pos + record_size > memory_limit) {
            ssize_t written = pwrite(fd, out_buf, out_pos, offset);
            if (written < 0) {
                perror("pwrite");
                break;
            }
            offset += written;
            out_pos = 0;
        }
        std::memcpy(out_buf + out_pos, &pkp.key, sizeof(pkp.key));
        out_pos += sizeof(pkp.key);
        std::memcpy(out_buf + out_pos, &pkp.len, sizeof(pkp.len));
        out_pos += sizeof(pkp.len);
        std::memcpy(out_buf + out_pos, payload_buf, pkp.len);
        out_pos += pkp.len;

    }
    if (out_pos > 0) {
        ssize_t written = pwrite(fd, out_buf, out_pos, offset);
        if (written < 0) {
            perror("pwrite");
        }
    }
    delete out_buf;
    delete payload_buf;
    in_file.close();
    close(fd);
    //delete[] buffer;
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
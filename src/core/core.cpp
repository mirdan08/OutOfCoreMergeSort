#include "core.hpp"
#include <iostream>
#include <random>
#include <memory>
#include <fstream>
#include <cstring>

Record* initialize_random_record(
    const unsigned long max_payload_size,
    unsigned int seed
){
    std::mt19937 gen(seed); 
    std::uniform_int_distribution<unsigned long> key_dis(0, 10000000);
    std::uniform_int_distribution<uint32_t>  payload_size_dis(8, max_payload_size);
    std::uniform_int_distribution<>  char_dis('0', 'Z');
    auto r=std::make_unique<Record>();
    r->key=key_dis(gen);
    r->len=payload_size_dis(gen);
    for(int i=0;i<r->len;++i){
        r->payload[i]=static_cast<char>(char_dis(gen));
    }
    return r.release();
}

Record* read_record(std::ifstream& in_file){
    auto r=std::make_unique<Record>();
    in_file.read(reinterpret_cast<char*>(&(r->len)),sizeof(uint32_t));
    char buffer[payload_max+sizeof(unsigned long)];
    in_file.read(buffer,r->len+sizeof(unsigned long));
    std::memcpy(&r->key,buffer,sizeof(unsigned long));
    std::memcpy(&r->key,buffer+sizeof(unsigned long),static_cast<unsigned int>(r->len));
    return r.release();
}

void write_record(Record& record,std::ofstream& out_file){
    out_file.write(reinterpret_cast<char*>(&record.len),sizeof(uint32_t));
    out_file.write(reinterpret_cast<char*>(&record.key),sizeof(unsigned long));
    out_file.write(reinterpret_cast<char*>(&record.payload),sizeof(char)*record.len);
}
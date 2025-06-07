#include <cstdint>
#include<fstream>

#ifndef RPAYLOAD_MAX
#define RPAYLOAD_MAX 10
#endif

//max payload size
const unsigned long PAYLOAD_MAX= RPAYLOAD_MAX;
//max memory limit for single node
const unsigned long MAX_MEMORY_LIMIT=32*1024*1024;
//a single record struct
struct Record {
    uint32_t len; 
    uint64_t key; 
    char payload[PAYLOAD_MAX];
};
//create randomly initialized records
Record* initialize_random_record(
    const unsigned long max_payload_size,
    unsigned int seed
);

//used to sort the values within a single node
struct PosKeyPair{
    uint64_t key; 
    uint64_t pos; 
};

Record* read_record(std::ifstream& in_file);
void write_record(Record& record,std::ofstream& out_file);
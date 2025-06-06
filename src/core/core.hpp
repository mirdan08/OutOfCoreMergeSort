#include <cstdint>

#ifndef RPAYLOAD_MAX
#define RPAYLOAD_MAX 128
#endif

//max payload size
const unsigned long PAYLOAD_MAX= RPAYLOAD_MAX;
//max memory limit for single node
const unsigned long MAX_MEMORY_LIMIT=32*1024*1024;
//a single record struct
struct Record {
    uint32_t len; 
    unsigned long key; 
    char payload[PAYLOAD_MAX];
};
//create randomly initialized records
Record* initialize_random_record(
    const unsigned long max_payload_size,
    unsigned int seed
);

//used to sort the values within a single node
struct PosKeyPair{
    unsigned long key; 
    unsigned long pos; 
};

Record* read_record(std::ifstream& in_file);
void write_record(Record& record,std::ofstream& out_file);
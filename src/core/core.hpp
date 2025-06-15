#include <cstdint>
#include<fstream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef RPAYLOAD_MAX
#define RPAYLOAD_MAX 100
#endif

#ifndef RMAX_MEMORY_LIMIT
#define RMAX_MEMORY_LIMIT 1024
#endif

//max payload size
const unsigned long payload_max= RPAYLOAD_MAX;

//max memory limit for single node
const unsigned long MAX_MEMORY_LIMIT=RMAX_MEMORY_LIMIT;
//a single record struct
struct Record {
    uint32_t len; 
    uint64_t key; 
    char payload[payload_max];
};
//create randomly initialized records
Record* initialize_random_record(const unsigned long max_payload_size,unsigned int seed);

//used to sort the values within a single node
struct PosKeyPair{
    uint64_t key; 
    uint64_t pos; 
};

Record* read_record(std::ifstream& in_file);
void write_record(Record& record,std::ofstream& out_file);


using PosKeyVec=std::vector<PosKeyPair>;

using IndexPair=std::pair<unsigned long,unsigned long>;
using SortResult=std::tuple<unsigned long,unsigned long,unsigned long>;

uint64_t ms_select(const PosKeyVec& data, const std::vector<IndexPair> ranges, int k) ;
std::vector<PosKeyPair> k_way_merge_from_ranges(const PosKeyVec& data,const std::vector<IndexPair>& subranges);
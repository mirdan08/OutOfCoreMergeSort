#include <cstdint>
#include<fstream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#pragma once

#ifndef RPAYLOAD_MAX
#define RPAYLOAD_MAX 100
#endif
//max payload size
const unsigned long payload_max= RPAYLOAD_MAX;

//default max memory limit for single node
const unsigned long MAX_MEMORY_LIMIT=3359738368;
//a single record struct
struct Record {
    uint32_t len; 
    uint64_t key; 
    char payload[payload_max];
};

struct MemoryRecord{
    uint32_t len; 
    uint64_t key; 
    uint64_t offset;
};

//used to sort the values within a single node
struct PosKeyPair{
    uint64_t key; 
    uint64_t pos;
    uint64_t len;
    uint64_t offset; 
};


void write_record(Record& record,std::ofstream& out_file);


using PosKeyVec=std::vector<PosKeyPair>;

using IndexPair=std::pair<unsigned long,unsigned long>;
using SortResult=std::tuple<unsigned long,unsigned long,unsigned long>;

uint64_t ms_select(const PosKeyVec& data, const std::vector<IndexPair> ranges, int k) ;
std::vector<PosKeyPair> k_way_merge_from_ranges(const PosKeyVec& data,const std::vector<IndexPair>& subranges);
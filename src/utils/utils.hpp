#include<getopt.h>
#include<cstdlib>
#include<cstddef>
#include<iostream>
#include<cstdint>
#include<vector>
#include<core/core.hpp>
#include<string>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#pragma once

std::vector<PosKeyPair> read_records(std::string& file_path,const unsigned long memory_limt);

bool parse_cli_args(int argc,char*argv[],size_t& threads_num,bool& verbose,std::string& in_filename,std::string& out_filename,size_t& memory_limit);
void read_payloads_chunk(
    const std::string file_path,
    const size_t start_offset,
    const unsigned long memory_limit,
    uint64_t records_num,
    const size_t array_start,
    const size_t array_end,
    std::vector<PosKeyPair>& pos_key_data
);
std::vector<PosKeyPair> read_records_pread(const std::string& file_path, const unsigned long memory_limit);

std::pair<size_t,RecordVec*> readRecords(const std::string& file_path, const unsigned long memory_limit,size_t offset);

void writeRecords(
    const std::string& out_filename,
    size_t file_offset,
    Record* data,
    size_t data_count,
    size_t payload_max,
    size_t memory_limit);
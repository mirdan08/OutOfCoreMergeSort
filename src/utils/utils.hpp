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

bool parse_cli_args(int argc,char*argv[],size_t& threads_num,bool& verbose,std::string& filename);

void read_payloads(std::ifstream& in_file,const unsigned long MAX_MEMORY_LIMIT,uint64_t records_num,std::vector<PosKeyPair>& pos_key_data);
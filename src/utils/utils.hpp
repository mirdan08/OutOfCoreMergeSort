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

bool parse_cli_args(int argc,char*argv[],size_t& threads_num,bool& verbose,std::string& in_filename,std::string& out_filename,size_t& memory_limit);

#include<getopt.h>
#include<cstdlib>
#include<cstddef>
#include<iostream>

bool parse_cli_args(int argc,char*argv[],size_t& threads_num,bool& verbose,std::string& filename);
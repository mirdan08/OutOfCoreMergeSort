#include<utils/utils.hpp>
#include <fcntl.h>

bool parse_cli_args(int argc,char*argv[],size_t& threads_num,bool& verbose,std::string& in_filename,std::string& out_filename,size_t& memory_limit){
    int opt;
    while ((opt = getopt(argc, argv, "t:v:i:o:m:")) != -1) {
        switch (opt) {
            case 't': {
                threads_num = std::stoi(optarg);
                break;
            }
            case 'v':{
                int v = std::stoi(optarg);
                verbose= (v>0?true:false);
                break;
            }
            case 'i':{
                in_filename = optarg;
                break;
            }
            case 'o':{
                out_filename = optarg;
                break;
            }

            case 'm':{
                memory_limit = std::stoul(optarg);
                std::cout<< memory_limit << std::endl;
                break;
            }

            default:{
                std::cout << "wrong arguments" << std::endl;
                return false;
            }
        }
    }

    return true;
}
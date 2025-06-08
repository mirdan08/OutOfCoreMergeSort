#include<utils/utils.hpp>
#include<string>

bool parse_cli_args(int argc,char*argv[],size_t& threads_num,bool& verbose,std::string& filename){
    int opt;
    while ((opt = getopt(argc, argv, "t:v:i:")) != -1) {
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
                filename = optarg;
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
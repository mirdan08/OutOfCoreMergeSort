#include<utils/utils.hpp>

bool parse_cli_args(int argc,char*argv[],size_t& records_num,size_t& threads_num,bool& verbose){
    int opt;
    while ((opt = getopt(argc, argv, "s:r:t:v:l:")) != -1) {

        switch (opt) {
            case 's': {
                records_num = std::stoull(optarg);
                break;
            }
            case 't': {
                threads_num = std::stoi(optarg);
                break;
            }
            case 'v':{
                int v = std::stoi(optarg);
                verbose= (v>0?true:false);
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
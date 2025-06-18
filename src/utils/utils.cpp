#include<utils/utils.hpp>

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

void read_payloads(std::ifstream& in_file,const unsigned long MAX_MEMORY_LIMIT,uint64_t records_num,std::vector<PosKeyPair>& pos_key_data){
    std::streamsize file_size= in_file.tellg();
    const unsigned int header_offset=in_file.tellg();
    unsigned long records_count=0;
    size_t current_size=0;
    const unsigned int max_record_size=payload_max+sizeof(uint32_t)+sizeof(uint64_t);
    const unsigned int buffer_size= std::min(MAX_MEMORY_LIMIT,max_record_size*records_num);
    char* buffer=new char[buffer_size];
    const unsigned int payload_header_size=sizeof(uint32_t)+sizeof(uint64_t);
    const unsigned int file_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    const unsigned int offset_header_size=sizeof(uint64_t)*records_num;
    // we read only keys and store the indexes to apply std::sort
    unsigned int buffer_offset=0;
    unsigned int record_offset=0;
    while(records_count<records_num){
        unsigned int buffer_start=buffer_offset;
        //avoid reading over the buffer length
        unsigned int buffer_length=std::min(buffer_size,(uint)file_size-buffer_start);
        in_file.seekg(file_header_size+offset_header_size+buffer_start);
        in_file.read(buffer,buffer_length);
        //Note: header is not read in the buffer
        //If the offset exceed the payload header size stop
        while( (record_offset + payload_header_size)< buffer_length && records_count < records_num){
            PosKeyPair pkp;
            uint32_t payload_len=0;
            std::memcpy(&payload_len,buffer+record_offset,sizeof(uint32_t));
            std::memcpy(&pkp.key,buffer+record_offset+sizeof(uint32_t),sizeof(uint64_t));
            pkp.pos=records_count;
            pos_key_data.push_back(std::move(pkp));
            record_offset+=payload_len+payload_header_size;
            ++records_count;
        }
        buffer_offset+=record_offset;
        record_offset=0;
    }
    in_file.close();
    delete[] buffer;
}
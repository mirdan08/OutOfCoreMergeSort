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

std::vector<PosKeyPair> read_records(std::string& file_path,const unsigned long memory_limt){
    std::vector<PosKeyPair> pos_key_data;
    std::ifstream in_file(file_path,std::ifstream::binary | std::ifstream::ate);
    //get file size and return to normal position
    std::streamsize file_size= in_file.tellg();
    in_file.seekg(0);
    unsigned long records_count=0;
    //if file is smaller than available memory we just load it
    const unsigned long buffer_size= std::min(memory_limt,(unsigned long)file_size);
    char* buffer=new char[buffer_size];
    const unsigned long payload_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    // we read only keys and store the indexes to apply std::sort
    unsigned long buffer_offset=0;
    unsigned long record_offset=0;
    unsigned long bytes_read=0;
    while(bytes_read<file_size){
        unsigned long buffer_start=buffer_offset;
        record_offset=0;
        //avoid reading over the buffer length
        unsigned long buffer_length=std::min(buffer_size,(uint)file_size-buffer_start);
        in_file.seekg(buffer_start);
        in_file.read(buffer,buffer_length);
        //Note: header is not read in the buffer
        //If the offset exceed the payload header size stop
        while( (record_offset + payload_header_size)< buffer_length && bytes_read<file_size){
            PosKeyPair pkp;
            std::memcpy(&pkp.key,buffer+record_offset,sizeof(uint64_t));
            std::memcpy(&pkp.len,buffer+record_offset+sizeof(uint64_t),sizeof(uint64_t));
            pkp.pos=records_count;
            
            pkp.offset=buffer_start+record_offset;
            record_offset+=pkp.len+payload_header_size;
            bytes_read+=pkp.len+payload_header_size;
            pos_key_data.push_back(std::move(pkp));
            ++records_count;
        }
        buffer_offset+=record_offset;
    }
    in_file.close();
    delete[] buffer;
    return std::move(pos_key_data);
}

std::vector<PosKeyPair> read_records_pread(const std::string& file_path, const unsigned long memory_limit) {
    std::vector<PosKeyPair> pos_key_data;

    // Open file with O_RDONLY
    int fd = open(file_path.c_str(), O_RDONLY);
    if (fd < 0) {
        perror("open");
        throw std::runtime_error("Failed to open file");
    }

    // Get file size
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 0) {
        perror("lseek");
        close(fd);
        throw std::runtime_error("Failed to determine file size");
    }

    unsigned long records_count = 0;
    unsigned long buffer_size = std::min(memory_limit, (unsigned long)file_size);

    char* buffer = new char[buffer_size];
    const unsigned long payload_header_size = sizeof(uint64_t) + sizeof(uint64_t);

    unsigned long buffer_offset = 0;
    unsigned long record_offset = 0;
    unsigned long bytes_read = 0;

    while (bytes_read < (unsigned long)file_size) {
        unsigned long buffer_start = buffer_offset;
        record_offset = 0;

        // Determine how much to read without overshooting
        unsigned long buffer_length = std::min(buffer_size, (unsigned long)file_size - buffer_start);

        // Use pread instead of seekg + read
        ssize_t n_read = pread(fd, buffer, buffer_length, buffer_start);
        if (n_read < 0) {
            perror("pread");
            delete[] buffer;
            close(fd);
            throw std::runtime_error("Failed during pread");
        }

        while ((record_offset + payload_header_size) < (unsigned long)n_read && bytes_read < (unsigned long)file_size) {
            PosKeyPair pkp;
            std::memcpy(&pkp.key, buffer + record_offset, sizeof(uint64_t));
            std::memcpy(&pkp.len, buffer + record_offset + sizeof(uint64_t), sizeof(uint64_t));
            //std::cout<< pkp.len<< "-" <<pkp.key << std::endl;
            pkp.pos = records_count;
            pkp.offset = buffer_start + record_offset;
            record_offset += pkp.len + payload_header_size;
            bytes_read += pkp.len + payload_header_size;

            pos_key_data.push_back(std::move(pkp));
            ++records_count;
        }

        buffer_offset += record_offset;
    }

    delete[] buffer;
    close(fd);
    return pos_key_data;
}

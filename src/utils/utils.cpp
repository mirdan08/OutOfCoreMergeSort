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



std::pair<size_t,RecordVec*> readRecords(const std::string& file_path, const unsigned long memory_limit,size_t offset) {
    RecordVec* pos_key_data=new RecordVec();

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
    unsigned long buffer_size = std::min(memory_limit, (unsigned long)file_size-offset);

    char* buffer = new char[buffer_size];
    const unsigned long payload_header_size = sizeof(Record::len) + sizeof(Record::key);

    unsigned long buffer_offset = offset;
    unsigned long record_offset = offset;
    unsigned long bytes_read = 0;
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
    while ((record_offset + payload_header_size) < (unsigned long)n_read && bytes_read < (unsigned long)memory_limit) {
        Record record;
        std::memcpy(&record.key, buffer + record_offset, sizeof(Record::key));
        std::memcpy(&record.len, buffer + record_offset + sizeof(Record::key), sizeof(Record::len));
        if((record_offset + payload_header_size+record.len) > (unsigned long)n_read){
            break;
        }
        std::memcpy(&record.payload,buffer+record_offset+payload_header_size, record.len);
        record_offset += record.len + payload_header_size;
        bytes_read += record.len + payload_header_size;
        pos_key_data->emplace_back(std::move(record));
        ++records_count;
        
    }
    buffer_offset += record_offset;

    delete[] buffer;
    close(fd);
    return std::pair(buffer_offset-offset,pos_key_data);
}


void writeRecords(
    const std::string& out_filename,
    size_t file_offset,
    Record* data,
    size_t data_count,
    size_t payload_max,
    size_t memory_limit)
{
    size_t offset = file_offset;  // Output start offset for this thread
    int out_fd = open(out_filename.c_str(), O_RDWR);
    if (out_fd < 0) {
        perror("open output file");
        close(out_fd);
        return;
    }
    char* out_buf = new char[memory_limit];
    size_t out_pos = 0;

    for (size_t j = 0; j < data_count; ++j) {
        uint32_t to_read = data[j].len;
        size_t record_size = sizeof(data[j].key) + sizeof(data[j].len) + to_read;
        // If buffer full, write out
        if (out_pos + record_size > memory_limit) {
            size_t total_written = 0;
            while (total_written < out_pos) {
                ssize_t written = pwrite(out_fd, 
                                        out_buf + total_written, 
                                        out_pos - total_written, 
                                        offset + total_written);
                if (written < 0) {
                    if (errno == EINTR) continue; // Interrupted? retry
                    perror("pwrite");
                    break; // unrecoverable error
                }
                total_written += written;
            }
            
            if (total_written != out_pos) {
                fprintf(stderr, "Failed to write full buffer. Only wrote %zu/%zu bytes\n",
                    total_written, out_pos);
                }
            offset+=total_written;
            out_pos=0;
        }

        // Copy key, len, and payload into output buffer
        std::memcpy(out_buf + out_pos, &data[j].key, sizeof(data[j].key));
        out_pos += sizeof(data[j].key);
        std::memcpy(out_buf + out_pos, &data[j].len, sizeof(data[j].len));
        out_pos += sizeof(data[j].len);
        std::memcpy(out_buf + out_pos, &data[j].payload, data[j].len);
        out_pos += data[j].len;
    }

    if (out_pos > 0 ) {
        size_t total_written = 0;
        while (total_written < out_pos) {
            ssize_t written = pwrite(out_fd, 
                                    out_buf + total_written, 
                                    out_pos - total_written, 
                                    offset + total_written);
            if (written < 0) {
                if (errno == EINTR) continue; // Interrupted? retry
                perror("pwrite");
                break; // unrecoverable error
            }
            total_written += written;
        }
        offset+=total_written;
    }

    delete[] out_buf;
    close(out_fd);
}
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <cstdint>
#include <cstring>
#include <chrono>
#include<getopt.h>

struct RecordHeader {
    uint32_t len;
    uint64_t key;
    // payload[] follows
};

int main(int argc,char*argv[]) {
    int opt;
    std::string out_path="";
    size_t payload_max=0;
    size_t records_count=0;
    bool verbose;
    while ((opt = getopt(argc, argv, "v:o:p:r:")) != -1) {
        switch (opt) {
            case 'p': {
                payload_max = std::stoi(optarg);
                break;
            }
            case 'v':{
                int v = std::stoi(optarg);
                verbose= (v>0?true:false);
                break;
            }
            case 'o':{
                out_path = optarg;
                break;
            }
            case 'r':{
                records_count = std::stoi(optarg);
                break;
            }
            default:{
                std::cout << "wrong arguments" << std::endl;
            }
        }
    }
    std::ofstream out_file(out_path, std::ios::binary);
    if(out_path == ""){
        std::cerr << "No output file was specified\n";
        return 1;
    }
    if (!out_file) {
        std::cerr << "Failed to open output file.\n";
        return 1;
    }
    if(payload_max <=0){
        std::cerr << "payload max size must be >0.\n";
        return 1;
    }
    if(records_count <=0){
        std::cerr << "records count must be >0.\n";
        return 1;
    }
    std::cout << "beggining to write '" << out_path << "' with " << records_count << " records and max payload "<< payload_max <<"."<< std::endl;
    // Write PAYLOAD_MAX
    //out_file.write(reinterpret_cast<const char*>(&payload_max), sizeof(uint64_t));

    // Write placeholder for RECORD_COUNT (we’ll fill real value after generation)
    //out_file.write(reinterpret_cast<const char*>(&records_count), sizeof(uint64_t));

    // Placeholder for offsets
    //const size_t offset_table_pos = out_file.tellp();
    //std::vector<uint64_t> offsets(records_count, 0);
    //out_file.seekp(sizeof(uint64_t) * records_count, std::ios::cur);

    // Record generation
    std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<uint64_t> key_dist(0, UINT64_MAX);
    std::uniform_int_distribution<uint64_t> len_dist(8, payload_max);
    std::uniform_int_distribution<uint8_t> byte_dist(0, 255);

    //std::vector<uint64_t> actual_offsets;
    for (size_t i = 0; i < records_count; ++i) {
        //uint64_t record_offset = static_cast<uint64_t>(out_file.tellp());
        //actual_offsets.push_back(record_offset - (offset_table_pos + sizeof(uint64_t) * records_count));

        uint64_t key = key_dist(rng);
        uint64_t len = static_cast<uint64_t>(len_dist(rng));

        key%=1000000;

        // Write RecordHeader
        out_file.write(reinterpret_cast<const char*>(&key), sizeof(uint64_t));
        out_file.write(reinterpret_cast<const char*>(&len), sizeof(uint64_t));
        if(verbose){
            std::cout<< i << ">" << len << ":" << key << std::endl;
        }

        // Generate and write payload
        std::vector<char> payload(len);
        for (auto& c : payload) c = static_cast<char>(byte_dist(rng));
        out_file.write(payload.data(), len);
    }

    // Seek back and write the real offset table
    //out_file.seekp(offset_table_pos, std::ios::beg);
    //out_file.write(reinterpret_cast<const char*>(actual_offsets.data()), sizeof(uint64_t) * records_count);

    out_file.close();
    std::cout << "File '" << out_path << "' written with " << records_count << " records and max payload "<< payload_max <<"."<< std::endl;
    return 0;
}

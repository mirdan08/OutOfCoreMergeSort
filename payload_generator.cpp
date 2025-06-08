#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <cstdint>
#include <cstring>
#include <chrono>

constexpr size_t PAYLOAD_MAX = 100;  // customize as needed
constexpr size_t RECORD_COUNT = 100; // number of records to generate
const std::string OUTPUT_FILENAME = "output.pms";

struct RecordHeader {
    uint32_t len;
    uint64_t key;
    // payload[] follows
};

int main() {
    std::ofstream out_file(OUTPUT_FILENAME, std::ios::binary);
    if (!out_file) {
        std::cerr << "Failed to open output file.\n";
        return 1;
    }

    // Write PAYLOAD_MAX
    out_file.write(reinterpret_cast<const char*>(&PAYLOAD_MAX), sizeof(uint64_t));

    // Write placeholder for RECORD_COUNT (we’ll fill real value after generation)
    out_file.write(reinterpret_cast<const char*>(&RECORD_COUNT), sizeof(uint64_t));

    // Placeholder for offsets
    const size_t offset_table_pos = out_file.tellp();
    std::vector<uint64_t> offsets(RECORD_COUNT, 0);
    out_file.seekp(sizeof(uint64_t) * RECORD_COUNT, std::ios::cur);

    // Record generation
    std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<uint64_t> key_dist(0, UINT64_MAX);
    std::uniform_int_distribution<size_t> len_dist(8, PAYLOAD_MAX);
    std::uniform_int_distribution<uint8_t> byte_dist(0, 255);

    std::vector<uint64_t> actual_offsets;
    for (size_t i = 0; i < RECORD_COUNT; ++i) {
        uint64_t record_offset = static_cast<uint64_t>(out_file.tellp());
        actual_offsets.push_back(record_offset - (offset_table_pos + sizeof(uint64_t) * RECORD_COUNT));

        uint32_t len = static_cast<uint32_t>(len_dist(rng));
        uint64_t key = key_dist(rng);

        key%=1000000;

        // Write RecordHeader
        out_file.write(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
        out_file.write(reinterpret_cast<const char*>(&key), sizeof(uint64_t));

        // Generate and write payload
        std::vector<char> payload(len);
        for (auto& c : payload) c = static_cast<char>(byte_dist(rng));
        out_file.write(payload.data(), len);
    }

    // Seek back and write the real offset table
    out_file.seekp(offset_table_pos, std::ios::beg);
    out_file.write(reinterpret_cast<const char*>(actual_offsets.data()), sizeof(uint64_t) * RECORD_COUNT);

    out_file.close();
    std::cout << "File '" << OUTPUT_FILENAME << "' written with " << RECORD_COUNT << " records.\n";
    return 0;
}

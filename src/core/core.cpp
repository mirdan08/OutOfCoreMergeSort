#include "core.hpp"
#include <iostream>
#include <random>
#include <memory>
#include <fstream>
#include <cstring>
#include <cassert>

Record* initialize_random_record(
    const unsigned long max_payload_size,
    unsigned int seed
){
    std::mt19937 gen(seed); 
    std::uniform_int_distribution<unsigned long> key_dis(0, 10000000);
    std::uniform_int_distribution<uint32_t>  payload_size_dis(8, max_payload_size);
    std::uniform_int_distribution<>  char_dis('0', 'Z');
    auto r=std::make_unique<Record>();
    r->key=key_dis(gen);
    r->len=payload_size_dis(gen);
    for(int i=0;i<r->len;++i){
        r->payload[i]=static_cast<char>(char_dis(gen));
    }
    return r.release();
}

Record* read_record(std::ifstream& in_file){
    auto r=std::make_unique<Record>();
    in_file.read(reinterpret_cast<char*>(&(r->len)),sizeof(uint32_t));
    char buffer[payload_max+sizeof(unsigned long)];
    in_file.read(buffer,r->len+sizeof(unsigned long));
    std::memcpy(&r->key,buffer,sizeof(unsigned long));
    std::memcpy(&r->key,buffer+sizeof(unsigned long),static_cast<unsigned int>(r->len));
    return r.release();
}

void write_record(Record& record,std::ofstream& out_file){
    out_file.write(reinterpret_cast<char*>(&record.len),sizeof(uint32_t));
    out_file.write(reinterpret_cast<char*>(&record.key),sizeof(unsigned long));
    out_file.write(reinterpret_cast<char*>(&record.payload),sizeof(char)*record.len);
}


uint64_t ms_select(const PosKeyVec& data, const std::vector<IndexPair> ranges, int k) {
    int p = ranges.size();
    // Each pair: first = left bound, second = right bound
    std::vector<std::pair<size_t, size_t>> bounds(p);

    for (int i = 0; i < p; ++i) {
        bounds[i].first = ranges[i].first;
        bounds[i].second = ranges[i].second - 1;
    }

    while (true) {
        // Gather candidates: pick middle element of each subrange
        std::vector<uint64_t> candidates;
        for (int i = 0; i < p; ++i) {
            if (bounds[i].first < bounds[i].second) {
                size_t mid = (bounds[i].first  +  bounds[i].second) /2 ;
                candidates.push_back(data[mid].key);
            }
        }
        if(candidates.empty()) break;
        bool finished = true;
        for (int i = 0; i < p; ++i) {
            if (bounds[i].first < bounds[i].second) {
                finished = false;
                break;
            }
        }
        if (finished) break;

        // Select median of candidates as pivot
        size_t mid = candidates.size() / 2;
        std::nth_element(candidates.begin(), candidates.begin() + mid, candidates.end());
        uint64_t pivot = candidates[mid];

        // Calculate global rank of pivot
        int global_rank = 0;
        for (int i = 0; i < p; ++i) {
            size_t left = bounds[i].first;
            size_t right = bounds[i].second + 1;  // +1 because upper_bound end is exclusive
            
            auto subrange_begin = data.begin() + left;
            auto subrange_end = data.begin() + right;
            
            auto it = std::upper_bound(
                subrange_begin, subrange_end,
                pivot,
                [](unsigned long val,const PosKeyPair& elem) {
                    return val < elem.key;
                });
                
                global_rank += it - subrange_begin;
            }
            if (global_rank >= k) {
                for (int i = 0; i < p; ++i) {
                    
                    size_t left = bounds[i].first;
                    size_t right = bounds[i].second +1;
                    auto subrange_begin = data.begin() + left;
                    auto subrange_end = data.begin() + right;

                    
                    auto it = std::upper_bound(
                        subrange_begin, subrange_end,
                        pivot,
                        [](unsigned long val,const PosKeyPair& elem) {
                            return val < elem.key;
                        });
                    auto offset=it-data.begin();
                    if( offset==0 ||  offset< bounds[i].first){
                        bounds[i].second=bounds[i].first;
                    }else{
                        bounds[i].second=offset-1;
                    }
                }
            } else {
                for (int i = 0; i < p; ++i) {
                    size_t left = bounds[i].first;
                    size_t right = bounds[i].second + 1;
                    auto subrange_begin = data.begin() + left;
                    auto subrange_end = data.begin() + right;
                    
                    auto it = std::upper_bound(
                        subrange_begin, subrange_end,
                        pivot,
                        [](unsigned long val,const PosKeyPair& elem) {
                            return val < elem.key;
                        }
                    );

                    auto offset=it-data.begin();
                    if(offset==0 || offset < bounds[i].first){
                        bounds[i].first=bounds[i].second;
                    }else{
                        bounds[i].first =offset;
                    }
                }
        }
                
    }

    uint64_t result = UINT64_MAX;
    for (int i = 0; i < p; ++i) {
        if (bounds[i].first <= bounds[i].second) {
            result = std::min(result, data[bounds[i].first].key);
        }
    }
    return result;
}


std::vector<PosKeyPair> k_way_merge_from_ranges(
    const PosKeyVec& data,
    const std::vector<IndexPair>& subranges
) {
    size_t k = subranges.size();
    std::vector<size_t> positions(k);
    for (size_t i = 0; i < k; ++i) {
        positions[i] = subranges[i].first;
    }

    std::vector<PosKeyPair> merged;
    std::vector<size_t> min_positions(subranges.size(),0);

    while (true) {
        int min_idx = -1;
        uint64_t min_key = UINT64_MAX;

        for (size_t i = 0; i < k; ++i) {
            size_t pos = positions[i];
            if (pos < subranges[i].second) {
                uint64_t key = data[pos].key;
                if (key < min_key) {
                    min_key = key;
                    min_idx = i;
                }
            }
        }

        if (min_idx == -1)
            break;  // All subranges are exhausted

        merged.push_back(data[positions[min_idx]]);
        positions[min_idx]++;
    }
    return merged;
}
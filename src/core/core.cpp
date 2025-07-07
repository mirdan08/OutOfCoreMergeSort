#include "core.hpp"
#include <iostream>
#include <random>
#include <memory>
#include <fstream>
#include <cstring>
#include <cassert>
#include <queue>

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
        // Select median of candidates as pivot
        size_t mid = candidates.size() / 2;
        std::nth_element(candidates.begin(), candidates.begin() + mid, candidates.end());
        uint64_t pivot = candidates[mid];
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
            //std::cout << k << " global rank " << global_rank << std::endl;
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
                        }
                    );
                    auto offset=it-data.begin();
                    if( offset==0 ||  offset< bounds[i].first || it==subrange_end){
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
                    if(offset==0 || offset < bounds[i].first || it==subrange_end){
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
using PosKeyVec=std::vector<PosKeyPair>;

using IndexPair=std::pair<unsigned long,unsigned long>;
using SortResult=std::tuple<unsigned long,unsigned long,unsigned long>;

uint64_t ms_select2(const std::vector<PosKeyPair>& data,
    const std::vector<std::pair<size_t, size_t>>& sorted_ranges,
    size_t global_rank) noexcept {
    // Set initial binary search bounds for keys
    uint64_t low = std::numeric_limits<uint64_t>::min();
    uint64_t high = std::numeric_limits<uint64_t>::max();

        while (low < high) {
        uint64_t mid = low + (high - low) / 2;

        // Estimate how many elements are ≤ mid across all sorted ranges
        size_t rank = 0;
        for (size_t j=0;j<sorted_ranges.size();++j) {
            const size_t start=sorted_ranges[j].first;
            const size_t end=sorted_ranges[j].second;
            auto it = std::upper_bound(
            data.begin() + start, data.begin() + end, mid,
            [](uint64_t value, const PosKeyPair& elem) {
                return value < elem.key;
            }
        );
            rank += (it - (data.begin() + start));
        }

        if (rank <= global_rank) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return low;
}
struct HeapNode {
    uint64_t key;          // Key for sorting
    size_t subrange_idx;   // Which subarray this element belongs to
    size_t pos;            // Position in data of this element
    // This operator makes the priority_queue a min-heap by key
    bool operator>(const HeapNode& other) const {
        return key > other.key;
    }
};

std::vector<PosKeyPair> k_way_merge_heap(
    const PosKeyVec& data,
    const std::vector<IndexPair>& subranges
) noexcept {
    size_t k = subranges.size();

    // Min-heap: smallest key at top
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> min_heap;

    // Reserve total output size for efficiency
    size_t total_size = 0;
    for (const auto& range : subranges) {
        total_size += (range.second - range.first);
    }
    std::vector<PosKeyPair> merged(total_size);
    //merged.reserve(total_size);

    // Initialize the heap with the first element of each subrange (if not empty)
    for (size_t i = 0; i < k; ++i) {
        size_t start = subranges[i].first;
        size_t end = subranges[i].second;
        if (start < end) {
            min_heap.push({data[start].key, i, start});
        }
    }
    size_t i=0;
    // Extract-min and push next element from the same subrange until heap is empty
    while (!min_heap.empty()) {
        HeapNode current = min_heap.top();
        min_heap.pop();

        merged[i]=data[current.pos];
        ++i;

        size_t next_pos = current.pos + 1;
        size_t sub_i = current.subrange_idx;
        if (next_pos < subranges[sub_i].second) {
            min_heap.push({data[next_pos].key, sub_i, next_pos});
        }
    }

    return merged;
}

void k_way_merge_buffer(
    PosKeyPair* src_data,
    std::vector<IndexPair>& subranges,
    PosKeyPair* dst_data,
    const size_t count
) noexcept {
    size_t k = subranges.size();

    // Min-heap: smallest key at top
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> min_heap;

    // Reserve total output size for efficiency
    size_t total_size = count;
    // Initialize the heap with the first element of each subrange (if not empty)
    for (size_t i = 0; i < k; ++i) {
        size_t start = subranges[i].first;
        size_t end = subranges[i].second;
        if (start < end) {
            min_heap.push({src_data[start].key, i, start});
        }
    }
    size_t i=0;
    // Extract-min and push next element from the same subrange until heap is empty
    while (!min_heap.empty()) {
        HeapNode current = min_heap.top();
        min_heap.pop();

        dst_data[i]=src_data[current.pos];
        ++i;

        size_t next_pos = current.pos + 1;
        size_t sub_i = current.subrange_idx;
        if (next_pos < subranges[sub_i].second) {
            min_heap.push({src_data[next_pos].key, sub_i, next_pos});
        }
    }
}

void radix_sort_by_key(PosKeyVec& data) noexcept {
    constexpr size_t num_bytes = sizeof(uint64_t); // 8 bytes for uint64_t
    constexpr size_t radix = 256;  // 8-bit radix per pass
    const size_t n = data.size();
    
    PosKeyVec buffer(n);
    
    for (size_t byte = 0; byte < num_bytes; ++byte) {
        size_t count[radix] = {0};
        
        // Histogram the byte values
        for (size_t j=0;j<data.size();++j) {
            uint8_t val = (data[j].key >> (byte * 8)) & 0xFF;
            count[val]++;
        }
        
        // Compute prefix sum
        size_t offset[radix];
        offset[0] = 0;
        for (size_t i = 1; i < radix; ++i)
        offset[i] = offset[i - 1] + count[i - 1];
        
        // Place elements into buffer
        for (size_t j=0;j<data.size();++j) {
            uint8_t val = (data[j].key >> (byte * 8)) & 0xFF;
            buffer[offset[val]++] = data[j];
        }
        
        // Swap buffers
        std::swap(data, buffer);
    }
}

void radix_sort_slice(PosKeyVec& data, size_t start, size_t end) noexcept {
    PosKeyVec slice(data.begin() + start, data.begin() + end);
    radix_sort_by_key(slice);
    std::copy(slice.begin(), slice.end(), data.begin() + start);
}
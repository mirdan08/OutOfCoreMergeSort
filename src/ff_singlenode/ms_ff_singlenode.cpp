
#include <core/core.hpp>
#include <utils/utils.hpp>
#include <ff/ff.hpp>
#include <chrono>
#include <vector>
#include <algorithm>
#include <fstream>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using PosKeyVec=std::vector<PosKeyPair>;

using IndexPair=std::pair<unsigned long,unsigned long>;
using SortResult=std::tuple<unsigned long,unsigned long,unsigned long>;


//initial sorter for mergesort
struct SortingEmitter: public ff::ff_monode_t<int,IndexPair>{
    SortingEmitter(size_t stream_size,size_t workers_num)
        :stream_size(stream_size)
        ,workers_num(workers_num){};
    IndexPair* svc(int* in){
        size_t chunk_start=-1;
        size_t chunk_end=-1;
        size_t chunk_size=(stream_size/workers_num);
        for(int i=0;i<workers_num;i++){
            chunk_start=i*chunk_size;
            chunk_end= chunk_start + std::min(chunk_size,stream_size-chunk_start);
            IndexPair* res=new IndexPair(chunk_start,chunk_end);     
            ff_send_out(res);
        }
        std::cout << "done emitting" << std::endl;
        return EOS;
    }
    private:
        size_t stream_size;
        size_t workers_num;
};
//initial sorter for mergesort
struct SortingWorker: public ff::ff_monode_t<IndexPair,IndexPair>{
    SortingWorker(PosKeyVec& data):data(data){};
    IndexPair* svc(IndexPair* in){
        std::sort(
                    data.begin()+in->first,data.begin()+in->second,
                    [](const PosKeyPair& a,const PosKeyPair& b){return a.key<b.key;}
                );
        IndexPair* res=new IndexPair(in->first,in->second);        
        ff_send_out(res);
        delete in;
        std::cout << "done sorting" << std::endl;
        return GO_ON;
    }
    private:
        PosKeyVec& data;
};

uint64_t ms_select(const PosKeyVec& data, const std::vector<IndexPair>& ranges, int k) {
    int p = ranges.size();
    // Each pair: first = left bound, second = right bound
    std::vector<std::pair<size_t, size_t>> bounds(p);

    for (int i = 0; i < p; ++i) {
        bounds[i].first = ranges[i].first;          // l[i]
        bounds[i].second = ranges[i].second - 1;    // r[i]
    }

    while (true) {
        // Gather candidates: pick middle element of each subrange
        std::vector<uint64_t> candidates;
        for (int i = 0; i < p; ++i) {
            if (bounds[i].first <= bounds[i].second) {
                size_t mid = bounds[i].first + (bounds[i].second - bounds[i].first) / 2;
                candidates.push_back(data[mid].key);
            }
            if(k==250){
                std::cout << k<< " ";
                for(const auto& c:candidates){
                    std::cout << c<< " ";
                }
                std::cout << std::endl;

                for (int i = 0; i < p; ++i) {
                    if(bounds[i].first < bounds[i].second){
                        std::cout << i << "-" << bounds[i].first << ":" << bounds[i].second << std::endl;
                    }
                }
            }
        }

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
        if(k==250){
            std::cout << global_rank << "<>" << k << std::endl;
        }
        // Update bounds based on comparison with k
        if (global_rank >= k) {
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
                    });

                bounds[i].second = (it - data.begin()) - 1;
                if (bounds[i].second < bounds[i].first) bounds[i].second = bounds[i].first; // Avoid invalid range
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
                    });

                bounds[i].first =(it - data.begin())+1;
                if (bounds[i].first > bounds[i].second) bounds[i].first = bounds[i].second; // Avoid invalid range
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
// ---- SelectWorker: receives k and outputs k-th global key ----
struct SelectWorker : ff::ff_node_t<int,uint64_t> {
    const PosKeyVec& data;
    const std::vector<IndexPair>& ranges;

    SelectWorker(const PosKeyVec& d, const std::vector<IndexPair>& r) : data(d), ranges(r) {}

    uint64_t* svc(int* task) {
        std::cout<< "ms select start" << std::endl;
        int k = *static_cast<int*>(task);
        delete static_cast<int*>(task);
        uint64_t* result = new uint64_t(ms_select(data, ranges, k));
        std::cout<< "ms select end" << std::endl;
        return result;
    }
};

// ---- SelectCollector: gathers pivots and reports them ----
struct SelectCollector : ff::ff_node_t<uint64_t,void> {
    size_t p;
    std::vector<uint64_t> pivots;

    SelectCollector(size_t p) : p(p) {
        pivots.reserve(p - 1);
    }

    void* svc(uint64_t* task) {
        uint64_t pivot = *static_cast<uint64_t*>(task);
        delete static_cast<uint64_t*>(task);
        pivots.push_back(pivot);

        if (pivots.size() == p - 1) {
            std::sort(pivots.begin(), pivots.end());
            std::cout << "Selected Pivots:\n";
            for (auto val : pivots) std::cout << val << " ";
            std::cout << "\n";
        }

        return GO_ON;
    }
};

// ---- SelectEmitter: emits k values for pivot search ----
struct SelectEmitter : ff::ff_node_t<int,int> {
    size_t n, p, current = 1;
    SelectEmitter(size_t n, size_t p) : n(n), p(p) {};
    int* svc(int* in) {

        if (current > p - 1) return EOS;
        int* k = new int(current * n / p);
        ++current;
        std::cout << "emitting rank " << *k << std::endl;
        return k;
    }
};



void sort_with_ff(PosKeyVec& data,size_t sorting_workers,size_t num_workers){
    ff::ff_farm sorting_farm;
    std::vector<ff::ff_node*> workers;

    sorting_farm.add_emitter(SortingEmitter (data.size(),num_workers));
    for (int i=0;i<sorting_workers;i++){
        workers.push_back(new SortingWorker(data));
    }
    sorting_farm.add_workers(workers);
    sorting_farm.remove_collector();

    ff::ff_farm  ranking_farm;
    ranking_farm.add_emitter(SelectEmitter(data.size(),num_workers));
    
    std::vector<IndexPair> sorted_ranges;

    size_t chunk_size= data.size()/num_workers; 

    for(int i=0;i<num_workers;i++){
        size_t chunk_start=i*chunk_size;
        size_t chunk_end= i*chunk_size + std::min(chunk_size,data.size()-chunk_start);
        sorted_ranges.push_back(IndexPair(chunk_start,chunk_end));
    }

    std::vector<ff::ff_node*> rank_workers;
    for (int i=0;i<sorting_workers;i++){
        rank_workers.push_back(new SelectWorker(data,sorted_ranges));
    }
    ranking_farm.add_workers(rank_workers);
    SelectCollector sc(num_workers);
    ranking_farm.add_collector(&sc);

    ff::ff_pipeline pipeline;

    pipeline.add_stage(&sorting_farm);
    pipeline.add_stage(&ranking_farm);

    if(pipeline.run_and_wait_end()<0)
        std::cerr << "errors with the pipeline" << std::endl;

}

int main(int argc,char*argv[]){
    uint64_t records_num=0;
    size_t threads_num=0;
    bool verbose = false;
    std::string filename="";
    bool success=parse_cli_args(argc,argv,threads_num,verbose,filename);
    if(!success){
        std::cout << "Exiting..." << std::endl;
        return 1;
    }
    if(filename==""){
        std::cout << "please specify filename" << std::endl;
        return 1;
    }
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::ifstream in_file(filename,std::ifstream::binary | std::ios::ate);
    std::streamsize file_size= in_file.tellg();
    in_file.seekg(0);
    uint64_t max_file_payload_size;
    in_file.read(reinterpret_cast<char*>(&max_file_payload_size),sizeof(uint64_t));    
    if(max_file_payload_size != payload_max){
        std::cout<< "error:the maximum payload size should correspond to " << payload_max << " but it isn't.\nExiting..." << std::endl;
        in_file.close();
        return 1;
    }
    records_num=0;
    in_file.read(reinterpret_cast<char*>(&records_num),sizeof(uint64_t));
    
    std::vector<uint64_t> offsets(records_num);
    in_file.read(reinterpret_cast<char*>(offsets.data()),sizeof(uint64_t)*records_num);
    std::cout << "starting with:\n" 
              << "\tnumber of records:\t" << records_num 
              << "\tmax payload size:\t" << max_file_payload_size << std::endl;

    const unsigned int header_offset=in_file.tellg();
    unsigned long records_count=0;
    size_t current_size=0;
    std::vector<PosKeyPair> pos_key_data;
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
        unsigned int buffer_length=std::min(buffer_size,(uint)file_size-(uint)(file_header_size) -buffer_start);
        in_file.seekg(file_header_size+offset_header_size+buffer_offset);
        in_file.read(buffer,buffer_length);

        //Note: header is not read in the buffer
        //If the offset exceed the payload header size stop
        while( record_offset + payload_header_size< buffer_length && records_count < records_num){
            PosKeyPair pkp;
            uint32_t payload_len=0;
            std::memcpy(&payload_len,buffer+record_offset,sizeof(uint32_t));
            std::memcpy(&pkp.key,buffer+record_offset+sizeof(uint32_t),sizeof(uint64_t));
            pkp.pos=records_count;
            pos_key_data.push_back(std::move(pkp));
            record_offset+=payload_len+payload_header_size;
            ++records_count;
        }
        buffer_offset=record_offset;
        record_offset=0;
    }
    in_file.close();
    delete[] buffer;
    size_t num_workers=4;
    std::cout << "done reading" << std::endl;
    sort_with_ff(pos_key_data,num_workers,num_workers);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:pos_key_data){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;

    return 0;
}
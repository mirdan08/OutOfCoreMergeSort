
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


//initial sorter for mergesort
struct SortingEmitter : public ff::ff_monode_t<int, IndexPair> {
    SortingEmitter(size_t stream_size, size_t workers_num)
        : stream_size(stream_size), workers_num(workers_num) {}

    IndexPair* svc(int* in) {
        size_t chunk_base = stream_size / workers_num;
        size_t remainder = stream_size % workers_num;

        size_t start = 0;
        for (size_t i = 0; i < workers_num; ++i) {
            size_t chunk_size = chunk_base + (i < remainder ? 1 : 0);
            size_t end = start + chunk_size;
            IndexPair* res = new IndexPair(start, end);
            ff_send_out(res);
            start = end;
        }
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
        return in;
    }
    private:
        PosKeyVec& data;
};
// ---- SelectWorker: receives k and outputs k-th global key ----
struct SelectWorker : ff::ff_node_t<int,uint64_t> {
    const PosKeyVec& data;
    const std::vector<IndexPair> ranges;

    SelectWorker(const PosKeyVec& d, const std::vector<IndexPair> r) : data(d), ranges(r) {}

    uint64_t* svc(int* task) {

        int k = *task;
        uint64_t* result = new uint64_t(ms_select(data, ranges, k));
        //delete task;
        return result;
    }
};

// ---- SelectCollector: gathers pivots and reports them ----
struct SelectCollector : ff::ff_minode_t<uint64_t,void> {
    size_t p;
    std::vector<uint64_t> pivots;
    PosKeyVec& data;
    std::vector<IndexPair>& ranges;


    SelectCollector(size_t p,PosKeyVec& data,std::vector<IndexPair>& ranges): 
         p(p),
         data(data),
         ranges(ranges){
        pivots.reserve(p - 1);
    }

    void* svc(uint64_t* task) {
        pivots.push_back(*task);

        //delete task;
        if (pivots.size() == p - 1) {
            std::sort(pivots.begin(), pivots.end());
            std::vector<std::vector<IndexPair>*> bucket_subranges(p);
            for (size_t i = 0; i < p; ++i) {
                bucket_subranges[i] = new std::vector<IndexPair>();
            }
            
            for (const auto& [start_idx, end_idx] : ranges) {
                auto begin_it = data.begin() + start_idx;
                auto end_it = data.begin() + end_idx;
                
                size_t last_idx = start_idx;
                
                for (size_t b = 0; b < p; ++b) {
                    auto low = data.begin() + last_idx;
                    
                    auto high = (b < pivots.size())
                    ? std::upper_bound(low, end_it, pivots[b],
                        [](uint64_t val, const PosKeyPair& elem) {
                            return val < elem.key;
                        })
                        : end_it;
                        
                        if (low < high) {
                            bucket_subranges[b]->emplace_back(low - data.begin(), high - data.begin());
                        }
                        
                        last_idx = high - data.begin();
                        if (last_idx >= end_idx) break;
                    }
                    
                }
                
                for (size_t b = 0; b < p; ++b) {
                    ff_send_out(bucket_subranges[b]); // Each is vector<IndexPair>*
                }
                return EOS;
        }
        //delete static_cast<uint64_t*>(task);
        return GO_ON;
    }
};

struct SubMergeWorker : ff::ff_node_t<
std::vector<IndexPair>,
PosKeyVec
>{

    PosKeyVec& data;
    SubMergeWorker(PosKeyVec& data):data(data){}
    
    PosKeyVec* svc(std::vector<IndexPair>* task) {
        PosKeyVec* merged = new PosKeyVec(k_way_merge_from_ranges(data, *task));
        //delete task;
        // Process or store `merged` as needed
        return merged;
    }
    
};
struct SubRangeCollector : ff::ff_minode_t<
PosKeyVec,
int
>{
    PosKeyVec& data;
    std::vector<PosKeyVec*> sub_ranges;
    PosKeyVec& result;
    int range_counter=0;
    int num_workers;
    SubRangeCollector(int num_workers,PosKeyVec& data,PosKeyVec& result):num_workers(num_workers),data(data),result(result){};
    int* svc(PosKeyVec* task) {
        auto* merged_result = task;
        if(!merged_result->empty()){
            sub_ranges.push_back(merged_result);
        }
        //delete task;
        range_counter++;
        if(range_counter==num_workers){
            std::sort(
                sub_ranges.begin(),sub_ranges.end(),
                [](PosKeyVec* a,PosKeyVec* b){
                    return a->front().key <= b->front().key;
                }
            );
            for(const auto& range:sub_ranges){
                for(const auto& pkp:(*range)){
                    result.push_back(pkp);
                }
            } 
            return EOS;
        }
        return GO_ON;
    }
    
};

// ---- SelectEmitter: emits k values for pivot search ----
struct SelectEmitter : ff::ff_node_t<int,int> {
    size_t n, p, current = 1;
    SelectEmitter(size_t n, size_t p) : n(n), p(p) {};
    int* svc(int* in) {
        //delete in;
        if (current > p - 1) return EOS;
        int* k = new int(current * n / p);
        ++current;
        return k;
    }
};



void sort_with_ff(PosKeyVec& data,size_t sorting_workers,size_t num_workers,PosKeyVec& result){
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
    size_t chunk_base = data.size() / num_workers;
    size_t remainder = data.size() % num_workers;

    size_t start = 0;
    for (size_t i = 0; i < num_workers; ++i) {
        size_t chunk_size = chunk_base + (i < remainder ? 1 : 0);
        size_t end = start + chunk_size;
        sorted_ranges.push_back(IndexPair(start,end));
        start = end;
    }


    std::vector<ff::ff_node*> rank_workers;
    for (int i=0;i<sorting_workers;i++){
        rank_workers.push_back(new SelectWorker(data,sorted_ranges));
    }
    ranking_farm.add_workers(rank_workers);
    SelectCollector sc(num_workers,data,sorted_ranges);
    ranking_farm.add_collector(&sc);

    ff::ff_farm subranges_sorting_farm;
    std::vector<ff::ff_node*> subrange_workers;
    for (int i=0;i<sorting_workers;i++){
        subrange_workers.push_back(new SubMergeWorker(data));
    }
    subranges_sorting_farm.add_workers(subrange_workers);
    SubRangeCollector src(num_workers,data,result);
    subranges_sorting_farm.add_collector(&src);

    ff::ff_pipeline pipeline;
    pipeline.add_stage(&sorting_farm);
    pipeline.add_stage(&ranking_farm);
    pipeline.add_stage(&subranges_sorting_farm);
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
    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:pos_key_data){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }

    size_t num_workers=threads_num;
    PosKeyVec result;
    sort_with_ff(pos_key_data,num_workers,num_workers,result);
    auto end_time = std::chrono::high_resolution_clock::now();
    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:result){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;

    return 0;
}
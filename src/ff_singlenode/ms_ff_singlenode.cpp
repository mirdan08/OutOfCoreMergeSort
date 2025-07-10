
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
#include <fcntl.h>

//initial sorter for mergesort
struct SortingEmitter : public ff::ff_monode_t<int, IndexPair> {
    SortingEmitter(std::vector<IndexPair> sorted_ranges)
        :sorted_ranges(sorted_ranges) {};

    IndexPair* svc(int* in) {
        for(const auto& [range_start,range_end]:sorted_ranges){
            ff_send_out(new IndexPair(range_start,range_end));
        }
        return EOS;
    }
private:
    size_t stream_size;
    size_t workers_num;
    std::vector<IndexPair> sorted_ranges;
};

//initial sorter for mergesort
struct SortingWorker: public ff::ff_monode_t<IndexPair,IndexPair>{
    SortingWorker(PosKeyVec& data):data(data){};
    IndexPair* svc(IndexPair* in){
        std::sort(
                    data.begin()+in->first,data.begin()+in->second,
                    [](const PosKeyPair& a,const PosKeyPair& b){return a.key<b.key;}
                );
        radix_sort_buffer(data.data()+(in->first),in->second-in->first);
        return in;
    }
    private:
        PosKeyVec& data;
};
// ---- SelectWorker: receives k and outputs k-th global key ----
struct SelectWorker : ff::ff_node_t<int,uint64_t> {
    const PosKeyVec& data;
    const std::vector<IndexPair> ranges;

    SelectWorker(const PosKeyVec& d, const std::vector<IndexPair> r) : data(d), ranges(r) {};

    uint64_t* svc(int* task) {
        int k = *task;
        uint64_t* result = new uint64_t(ms_select2(data, ranges, k));
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
            //std::sort(pivots.begin(), pivots.end());

            std::vector<std::vector<IndexPair>> bucket_subranges(p,std::vector<IndexPair>(p));
            
            for (int j=0;j<ranges.size();j++) {
                auto begin_it = data.begin() + ranges[j].first;
                auto end_it = data.begin() + ranges[j].second;
                build_pivot_subrange(ranges[j].first,ranges[j].second,j,pivots,data,bucket_subranges);
                
                /* size_t last_idx = start_idx;
                
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
                } */
                    
            }
            
            for (size_t b = 0; b < p; ++b) {
                const auto& range=bucket_subranges[b];
                ff_send_out(new std::vector<IndexPair>(std::move(range)));
            }
            return EOS;
        }
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
        size_t size=0;
        for(const auto& p:*task){
            size+=p.second-p.first;
        }
        PosKeyVec* merged=new PosKeyVec(size);

        k_way_merge_buffer(data.data(),*task,merged->data(),size);
        //PosKeyVec* merged = new PosKeyVec(std::move(k_way_merge_from_ranges(data, *task)));
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
    std::string out_filename;
    std::string in_filename;
    size_t file_size;
    SubRangeCollector(int num_workers,PosKeyVec& data,PosKeyVec& result,std::string in_filename,std::string out_filename)
    :num_workers(num_workers),
    data(data),
    result(result),
    out_filename(out_filename),
    in_filename(in_filename)
    {};

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
                    return a->front().key < b->front().key;
                }
            );
            std::ifstream in_file(in_filename,std::ifstream::binary | std::ifstream::ate);
            size_t file_size= in_file.tellg();
            in_file.close();
            std::ofstream out_file(out_filename,std::ofstream::binary);
            out_file.seekp(file_size-1);
            out_file.put(0);
            out_file.close();
            size_t byte_counter=0;
            for(const auto& range:sub_ranges){
                auto* res=new std::pair(byte_counter,range);
                ff_send_out(res);
                for(auto& pkp:*range){
                    byte_counter+=pkp.len+sizeof(uint64_t)+sizeof(uint64_t);
                }
            }
            return EOS;
        }
        return GO_ON;
    }
    
};

struct FileWriter: ff::ff_minode_t<
std::pair<size_t,PosKeyVec*>,
int
>{
    size_t num_workers;
    std::string out_filename;
    std::string in_filename;
    size_t memory_limit;
    FileWriter(std::string in_filename,std::string out_filename,size_t num_workers,size_t memory_limit)
    :out_filename(out_filename),in_filename(in_filename),num_workers(num_workers),memory_limit(memory_limit){};
    int* svc(std::pair<size_t,PosKeyVec*>* in){
        size_t byte_offset=in->first;
        buffered_poskey_write(in_filename,out_filename,byte_offset,in->second->data(),in->second->size(),payload_max,memory_limit);
        /* PosKeyVec* merged_result=in->second;

        int fd = open(out_filename.c_str(),O_RDWR);
        if (fd < 0) {
            perror("open");
        }
        std::ifstream in_file(in_filename,std::ofstream::binary);
        char buffer[payload_max];
        //Note: buffering cannot be applied as we don't know where the records are the be read from
        //there is still a form of buffering in the ofstream library
        for(auto& pkp:*merged_result){
            in_file.seekg(pkp.offset+sizeof(uint64_t)+sizeof(uint64_t));
            in_file.read(buffer,pkp.len);
            pwrite(fd,&pkp.key,sizeof(pkp.key),byte_offset);
            pwrite(fd,&pkp.len,sizeof(pkp.len),byte_offset+sizeof(pkp.key));
            pwrite(fd,buffer,pkp.len,byte_offset+sizeof(pkp.key)+sizeof(pkp.len));
            byte_offset+=sizeof(pkp.key)+sizeof(pkp.len)+pkp.len;
        }
        in_file.close();
        close(fd); */
        return new int(byte_offset);
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

void sort_with_ff(
    PosKeyVec& data,std::string in_filename,std::string out_filename,
    size_t sorting_workers,size_t num_workers,
    PosKeyVec& result,size_t memory_limit
){
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

    ff::ff_farm sorting_farm;
    std::vector<ff::ff_node*> workers;
    sorting_farm.add_emitter(SortingEmitter (sorted_ranges));
    for (int i=0;i<sorting_workers;i++){
        workers.push_back(new SortingWorker(data));
    }
    sorting_farm.add_workers(workers);
    sorting_farm.remove_collector();

    ff::ff_farm  ranking_farm;
    ranking_farm.add_emitter(SelectEmitter(data.size(),num_workers));
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
    SubRangeCollector src(num_workers,data,result,in_filename,out_filename);
    subranges_sorting_farm.add_collector(&src);

    ff::ff_farm writing_farm;

    std::vector<ff::ff_node* > writer_worker;

    for(int i=0;i<sorting_workers;i++){
        writer_worker.push_back(new FileWriter(in_filename,out_filename,num_workers,memory_limit));
    }
    writing_farm.add_workers(writer_worker);

    ff::ff_pipeline pipeline;
    pipeline.add_stage(&sorting_farm);
    pipeline.add_stage(&ranking_farm);
    pipeline.add_stage(&subranges_sorting_farm);
    pipeline.add_stage(&writing_farm);
    if(pipeline.run_and_wait_end()<0)
        std::cerr << "errors with the pipeline" << std::endl;

}

int main(int argc,char*argv[]){
    uint64_t records_num=0;
    size_t threads_num=0;
    bool verbose = false;
    std::string in_filename="";
    std::string out_filename="";
    size_t memory_limit=MAX_MEMORY_LIMIT;
    bool success=parse_cli_args(argc,argv,threads_num,verbose,in_filename,out_filename,memory_limit);
    if(!success){
        std::cout << "Exiting..." << std::endl;
        return 1;
    }
    if(in_filename==""){
        std::cout << "please specify the input file path" << std::endl;
        return 1;
    }
    if(out_filename==""){
        std::cout << "please specify the output file path" << std::endl;
        return 1;
    }
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<PosKeyPair> pos_key_data= read_records(in_filename,memory_limit);

    if (verbose){
        unsigned int i=0;
        for(const auto& pkp:pos_key_data){
            std::cout<< i++ << "\t[" << pkp.pos << ":" << pkp.key << "]" << std::endl;
        }
    }

    size_t num_workers=threads_num;
    PosKeyVec result;
    sort_with_ff(
        pos_key_data,in_filename,out_filename,
        num_workers,num_workers,
        result,
        memory_limit
    );

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
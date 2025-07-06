#include <core/core.hpp>
#include <utils/utils.hpp>

#include <chrono>
#include <vector>
#include <algorithm>
#include <fstream>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <omp.h>
#include <fcntl.h>
#include <queue>
#include <numeric>
#include <mpi.h>
MPI_Datatype create_poskeypair_type() {
    MPI_Datatype type;
    int lengths[4] = {1, 1, 1, 1};
    const MPI_Aint displacements[4] = {
        offsetof(PosKeyPair, key),
        offsetof(PosKeyPair, pos),
        offsetof(PosKeyPair, len),
        offsetof(PosKeyPair, offset)
    };
    MPI_Datatype types[4] = {MPI_UINT64_T, MPI_UINT64_T, MPI_UINT64_T, MPI_UINT64_T};

    MPI_Type_create_struct(4, lengths, displacements, types, &type);
    MPI_Type_commit(&type);
    return type;
}
void inline build_pivot_subrange(
    size_t start,size_t end, int j,
    const std::vector<uint64_t>& pivots,
    const std::vector<PosKeyPair>& data,
    std::vector<std::vector<IndexPair>>& bucket_subranges 
){
    auto begin_it = data.begin() + start;
    auto end_it = data.begin() + end;
    size_t last_idx = start;
    for (size_t b = 0; b < pivots.size()+1; ++b) {
        auto low = data.begin() + last_idx;
        
        auto high = (b < pivots.size())
        ? std::upper_bound(low, end_it, pivots[b],
            [](uint64_t val, const PosKeyPair& elem) {
                return val < elem.key;
            })
            : end_it;
        bucket_subranges[b][j]=IndexPair(low - data.begin(), high - data.begin());
        last_idx = high - data.begin();
        if (last_idx >= end) break;
    }
}

void buffered_poskey_write(const std::string in_filename,const std::string out_filename,size_t file_offset,PosKeyPair* data,size_t data_count,size_t payload_max,size_t memory_limit){
    size_t thread_offset = file_offset;                  // Start of this thread's output region
    const auto& pkp_list = data;            // Sorted records for this thread
    std::ifstream in_file(in_filename, std::ios::binary);
    
    char* payload_buf=new char[payload_max];                      // Input record buffer
    char* out_buf=new char[memory_limit];                   // Output write buffer
    size_t out_pos = 0;                                 // Current write buffer position
    
    int fd = open(out_filename.c_str(), O_RDWR);
    if (fd < 0) {
        perror("open");
        return;
    }
    
    for (size_t j=0;j<data_count;++j) {
        in_file.seekg(pkp_list[j].offset + sizeof(uint64_t) + sizeof(uint64_t));
        in_file.read(payload_buf, pkp_list[j].len);
        size_t record_size = sizeof(pkp_list[j].key) + sizeof(pkp_list[j].len) + pkp_list[j].len;
        if (out_pos + record_size > memory_limit) {
            ssize_t written = pwrite(fd, out_buf, out_pos, thread_offset);
            if (written < 0) {
                perror("pwrite");
                break;
            }
            thread_offset += written;
            out_pos = 0;
        }
        std::memcpy(out_buf + out_pos, &pkp_list[j].key, sizeof(pkp_list[j].key));
        out_pos += sizeof(pkp_list[j].key);
        std::memcpy(out_buf + out_pos, &pkp_list[j].len, sizeof(pkp_list[j].len));
        out_pos += sizeof(pkp_list[j].len);
        std::memcpy(out_buf + out_pos, payload_buf, pkp_list[j].len);
        out_pos += pkp_list[j].len;
    }
    
    if (out_pos > 0) {
        ssize_t written = pwrite(fd, out_buf, out_pos, thread_offset);
        if (written < 0) {
            perror("pwrite");
        }
    }
    delete out_buf;
    delete payload_buf;
    in_file.close();
    close(fd);
}

int main(int argc,char*argv[]) noexcept{
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
    int provided;
    MPI_Init_thread( &argc , &argv , MPI_THREAD_MULTIPLE, &provided);

    int rank, nprocs;
    std::vector<PosKeyPair> pos_key_data= std::move(read_records(in_filename,memory_limit));

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    std::vector<PosKeyPair> recvbuf(pos_key_data.size());

    MPI_Datatype pkp_type = create_poskeypair_type();

    std::vector<int> sendcounts(nprocs);
    size_t rank_base = pos_key_data.size() / nprocs;       // minimum chunk size per rank
    size_t rank_remainder = pos_key_data.size() % nprocs;  // leftover elements to distribute
    for (int i = 0; i < nprocs; i++) {
        sendcounts[i] = rank_base + (i < rank_remainder ? 1 : 0);
    }
    std::vector<int> displs(nprocs);
    displs[0] = 0;
    for (int i = 1; i < nprocs; i++) {
        displs[i] = displs[i-1] + sendcounts[i-1];
    }

    
    std::vector<PosKeyPair> local_data(sendcounts[rank]);
    
    int recvcount=0; 
    MPI_Scatterv(
        pos_key_data.data(),sendcounts.data(),displs.data(),pkp_type,
        local_data.data(),sendcounts[rank],pkp_type,
        0,MPI_COMM_WORLD
    );

    std::vector<IndexPair> sorted_ranges;
    size_t chunk_base = local_data.size() / threads_num;
    size_t remainder = local_data.size() % threads_num;
    
    size_t start = 0;
    for (size_t i = 0; i < threads_num; ++i) {
        size_t chunk_size = chunk_base + (i < remainder ? 1 : 0);
        size_t end = start + chunk_size;
        sorted_ranges.push_back(IndexPair(start,end));
        start = end;
    }

    #pragma omp parallel for shared(local_data) schedule(static)
    for(int i=0;i<threads_num;i++){
        const int start=sorted_ranges[i].first;
        const int end=sorted_ranges[i].second;
        radix_sort_slice(local_data,start,end);
    }

    // estimating  the ranks using 
    std::vector<uint64_t> pivots(std::max(threads_num-1,1UL));
    
    #pragma omp parallel for shared(pivots) shared(sorted_ranges) schedule(static)
    for(int i=1;i<=pivots.size();i++){
        int desired_rank=i*(local_data.size()/threads_num);
        pivots[i-1]=ms_select2(local_data,sorted_ranges,desired_rank);
    }
    
    std::vector<std::vector<IndexPair>> bucket_subranges(threads_num);
    for(int i=0;i<bucket_subranges.size();i++){
        bucket_subranges[i].resize(threads_num);
    }
    
    #pragma omp parallel for schedule(static) shared(bucket_subranges)
    for (size_t j=0;j<sorted_ranges.size();++j) {
        const size_t start=sorted_ranges[j].first;
        const size_t end=sorted_ranges[j].second;
        build_pivot_subrange(
            start,end,j,
            
            pivots,local_data,

            bucket_subranges
        );
    }
    PosKeyVec result;
    std::vector<PosKeyVec> merged_ranges(threads_num);
    #pragma omp parallel for shared(local_data) shared(bucket_subranges) schedule(static)
    for(int i=0;i<threads_num;i++){
        merged_ranges[i]=k_way_merge_heap(local_data,bucket_subranges[i]);
    }
    
    PosKeyVec sendbuf;
    for(int i=0;i<threads_num;i++){
        sendbuf.insert(sendbuf.end(),merged_ranges[i].begin(),merged_ranges[i].end());
    }
    MPI_Gatherv(
        sendbuf.data(),sendcounts[rank],pkp_type,
        recvbuf.data(),sendcounts.data(),displs.data(),pkp_type,
        0,MPI_COMM_WORLD
    );
    std::vector<uint64_t> buckets_bytes(nprocs);
    std::vector<PosKeyVec> final_data(nprocs);
    uint64_t rank_offset;
    if(rank==0){
        // estimating  the ranks using ms_select
        std::vector<uint64_t> global_pivots(std::max(nprocs-1,1));
        
        std::vector<IndexPair> rank_sorted_ranges;
        
        rank_sorted_ranges.push_back(IndexPair(displs[nprocs-1],pos_key_data.size()));
        for(size_t i=1;i<nprocs;++i){
            rank_sorted_ranges.push_back(IndexPair(displs[i-1],displs[i]));

        }

        #pragma omp parallel for shared(pivots) shared(rank_sorted_ranges) schedule(static)
        for(int i=1;i<global_pivots.size();i++){
            int global_rank=i*(pos_key_data.size()/nprocs);
            global_pivots[i-1]=ms_select2(recvbuf,rank_sorted_ranges,global_rank);
        }
        if(global_pivots.size()==1){
            global_pivots[0]=ms_select2(recvbuf,rank_sorted_ranges,pos_key_data.size());
        }
        std::vector<std::vector<IndexPair>> global_bucket_subranges(nprocs);
        for(int j=0;j<global_bucket_subranges.size();j++){
            global_bucket_subranges[j].resize(nprocs);
        }
        std::vector<std::vector<size_t>> global_buckets_bytes(nprocs);
        for(int j=0;j<global_buckets_bytes.size();j++){
            global_buckets_bytes[j].resize(nprocs);
        }
        #pragma omp parallel for shared(global_bucket_subranges,global_buckets_bytes) schedule(static)
        for (size_t j=0;j<rank_sorted_ranges.size();++j) {
            const size_t start=rank_sorted_ranges[j].first;
            const size_t end=rank_sorted_ranges[j].second;
            build_pivot_subrange(
                start,end,j,
                global_pivots,recvbuf,
                global_bucket_subranges
            );
            for(size_t b=0;b<nprocs;++b){
                const auto& last_pair=global_bucket_subranges[b][j];
                size_t byte_size=0;
                for(size_t j=last_pair.first;j<last_pair.second;j++){
                    byte_size+=recvbuf[j].len+sizeof(uint64_t)+sizeof(uint64_t);
                }
                global_buckets_bytes[b][j]=byte_size;
            }
        }
        std::ifstream in_file(in_filename,std::ifstream::binary | std::ifstream::ate);
        size_t file_size= in_file.tellg();
        in_file.close();
        std::ofstream out_file(out_filename,std::ofstream::binary);
        out_file.seekp(file_size-1);
        out_file.put(0);
        out_file.close();
        // accumulate the bytes
        //will be used later to parallelize file writing
        std::vector<uint64_t> offsets(nprocs);
        uint64_t byte_count=0;
        for(int i=0;i<global_bucket_subranges.size();i++){
            offsets[i]=byte_count;
            for(int j=0;j<global_bucket_subranges[i].size();j++){
                size_t bytes=global_buckets_bytes[i][j];
                byte_count+=bytes;
            }
        }
        for(int i=1;i<nprocs;i++){
            MPI_Send( 
                &offsets[i] , 1 , MPI_UINT64_T, 
                i , i , MPI_COMM_WORLD
            );
        }

        rank_offset=offsets[0];

        #pragma omp parallel shared(final_data,buckets_bytes,global_bucket_subranges,global_buckets_bytes,recvbuf)
        {
            #pragma omp single
            {
                for(int i=1;i<global_bucket_subranges.size();i++){
                    #pragma omp task
                    {
                        for(int j=0;j<global_bucket_subranges[i].size();j++){
                            size_t count=global_bucket_subranges[i][j].second-global_bucket_subranges[i][j].first;
                            size_t bytes=global_buckets_bytes[i][j];
                            // send offset for the file,the number of values
                            // and the actual data to reference the original file
                            MPI_Send( 
                                &bytes , 1 , MPI_UINT64_T, 
                                i , j , MPI_COMM_WORLD
                            );
                            MPI_Send( 
                                &count , 1 , MPI_UINT64_T, 
                                i , j , MPI_COMM_WORLD
                            );
                            MPI_Send(
                                recvbuf.data()+global_bucket_subranges[i][j].first,count,pkp_type,
                                i,j,MPI_COMM_WORLD
                            );
                        }
                        
                        if(global_buckets_bytes[i].size()==0){
                            for(int j=0;j<nprocs;j++){
                                size_t bytes=0;
                                MPI_Send( 
                                    &bytes , 1 , MPI_UINT64_T, 
                                    i , j , MPI_COMM_WORLD
                                );
                                
                            }
                        }
                    }
                }
                #pragma omp task
                {
                    for(int i=0;i<global_bucket_subranges[0].size();i++){
                        size_t count=global_bucket_subranges[0][i].second-global_bucket_subranges[0][i].first;
                        buckets_bytes[i]=global_buckets_bytes[0][i];
                        final_data[i].resize(count);
                        std::copy(
                            recvbuf.begin()+global_bucket_subranges[0][i].first,recvbuf.begin()+global_bucket_subranges[0][i].second,
                            final_data[i].begin()
                        );
                    }
                }
            }
        }
    }else{
        MPI_Recv(&rank_offset,1,MPI_UINT64_T,0,MPI_ANY_TAG,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

        #pragma omp parallel for schedule(static) shared(buckets_bytes,final_data)
        for(int i=0;i<nprocs;i++){
            uint64_t count = 0;
            uint64_t bucket_bytes =0;
            MPI_Recv(&bucket_bytes, 1, MPI_UINT64_T, 0, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if(bucket_bytes==0) continue;
            buckets_bytes[i]=bucket_bytes;
            MPI_Recv(&count, 1, MPI_UINT64_T, 0, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            final_data[i].resize(count);
            MPI_Recv(final_data[i].data(),count,pkp_type,0,i,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
        }
    }
    const size_t payload_thread_max=memory_limit/threads_num;

    std::vector<PosKeyPair> rank_result;
    for(int i=0;i<nprocs;++i){
        for(const auto& pkp:final_data[i]){
            rank_result.push_back(pkp);
        }
    }

    std::vector<IndexPair> pairs;
    size_t offset_acc=0;
    for(int i=0;i<final_data.size();i++){
        pairs.push_back(IndexPair(offset_acc,offset_acc+final_data[i].size()));
        offset_acc+=final_data[i].size();
    }
    std::vector<uint64_t> file_pivots(std::max(threads_num-1,1UL));
    #pragma omp parallel for shared(pivots) shared(sorted_ranges) schedule(static)
    for(int i=1;i<=file_pivots.size();i++){
        int desired_rank=i*(rank_result.size()/threads_num);
        file_pivots[i-1]=ms_select2(rank_result,pairs,desired_rank);
    }

    
    std::vector<std::vector<IndexPair>> rank_bucket_subranges(threads_num);
    for(int j=0;j<rank_bucket_subranges.size();j++){
        rank_bucket_subranges[j].resize(pairs.size());
    }

    #pragma omp parallel for shared(rank_bucket_subranges)
    for (size_t j=0;j<pairs.size();++j) {
        const size_t start=pairs[j].first;
        const size_t end=pairs[j].second;
        build_pivot_subrange(
            start,end,j,
            file_pivots,rank_result,
            rank_bucket_subranges
        );
    }
    std::vector<PosKeyVec> sorted_merged_ranges(threads_num);
    std::vector<size_t> bytes_nums(threads_num);
    const unsigned int payload_header_size=sizeof(uint64_t)+sizeof(uint64_t);
    #pragma omp parallel for shared(rank_result) shared(pairs) schedule(static)
    for(int i=0;i<threads_num;i++){
            sorted_merged_ranges[i]=std::move(k_way_merge_heap(rank_result,rank_bucket_subranges[i]));
            size_t local_bytes=0;
            #pragma omp simd reduction(+:local_bytes)
            for(size_t j=0;j<sorted_merged_ranges[i].size();j++){
                local_bytes+=payload_header_size+sorted_merged_ranges[i][j].len;
            }
            bytes_nums[i]=local_bytes; 
    }
    std::vector<size_t> offsets(threads_num);
    size_t byte_offset=0;
    for(int i=0;i<threads_num;i++){
        offsets[i]=byte_offset+rank_offset;
        byte_offset+=bytes_nums[i];
    }



    #pragma omp parallel for schedule(static) shared(sorted_merged_ranges)
    for (int i = 0; i < threads_num; ++i) {
        buffered_poskey_write(in_filename,out_filename,offsets[i],sorted_merged_ranges[i].data(),sorted_merged_ranges[i].size(),payload_max,payload_thread_max);
    }
    MPI_Type_free(&pkp_type);
    MPI_Finalize();
    if(rank==0){
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "time(ms):" << duration.count() << std::endl;
    }   
    return 0;
}
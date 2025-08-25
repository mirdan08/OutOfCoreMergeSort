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
#include <fcntl.h>
#include <omp.h>
#include <queue>
#include <numeric>
#include <queue>
#include <filesystem>
#include <mpi.h>

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


    auto start_time = MPI_Wtime();
    //std::chrono::high_resolution_clock::now();

    std::ifstream in_file(in_filename,std::ifstream::binary | std::ifstream::ate);
    size_t fileSize= in_file.tellg();
    in_file.close();

    std::ofstream outFile(out_filename,std::ofstream::binary | std::ofstream::trunc);
    outFile.seekp(fileSize-1);
    outFile.put(0);
    outFile.close();

    int inFd =open(in_filename.c_str(),O_RDONLY);
    if (inFd < 0) {
        perror("open input file");
        return 1;
    }
    int outFd =open(out_filename.c_str(),O_WRONLY);
    if (outFd < 0) {
        perror("open output file");
        return 1;
    }
    int provided;
    MPI_Init_thread( &argc , &argv , MPI_THREAD_MULTIPLE, &provided);
    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    std::vector<char*> recordsBatchs;
    std::vector<size_t> recordsSizes;
    std::string tmp="tmp_run_" +  std::to_string(rank);
    std::ofstream tmpFile(tmp,std::ofstream::binary | std::ofstream::trunc);
    tmpFile.put(0);
    tmpFile.close();
    
    int tmpFd =open(tmp.c_str(),O_RDWR);
    if (tmpFd < 0) {
        perror("open tmp file");
        return 1;
    }

    if(rank==0){
        //master rank, emitter+collector

        //emitter section
        size_t currentOffset=0;
        BufferedRecordReader bufReader(inFd,0,memory_limit/2);
        std::vector<char*> buffers;

        size_t bytesTraveling=0;

        std::vector<MPI_Request> reqs;
        std::vector<size_t> runsSizes;

        size_t i;

        size_t rankIdx=1;

        size_t chunkIdx=0;
        while(currentOffset<fileSize){
            i=0;
            //bookeping on the requests, avoid filling memory while buffering and sending
            while(bytesTraveling>=memory_limit && i<reqs.size()){
                if(reqs[i]!=MPI_REQUEST_NULL) {
                    MPI_Wait(&reqs[i],MPI_STATUS_IGNORE);
                    delete buffers[i];
                    bytesTraveling-=runsSizes[i];
                }
                i++;
                reqs[i]=MPI_REQUEST_NULL;
            }

            auto [newOffset,recordData]=bufReader.getRecords(fileSize);
            size_t batchSize=newOffset-currentOffset;
            
            char* buffer=bufReader.extractBuffer();
            MPI_Request request;
            reqs.push_back(request);
            runsSizes.push_back(batchSize);
            //send batch of records to worker
            MPI_Send(&batchSize,1,MPI_UNSIGNED_LONG,rankIdx,0,MPI_COMM_WORLD);
            MPI_Isend(buffer,batchSize,MPI_CHAR,rankIdx,0,MPI_COMM_WORLD,&reqs.back());

            buffers.push_back(buffer);
            chunkIdx++;
            bytesTraveling+=batchSize;
            currentOffset=newOffset;

            rankIdx= (rankIdx+1)%nprocs;
            if(rankIdx==0) rankIdx++;
        }
        std::vector<MPI_Request> endReqs;

        //signal end of stream to the workers
        size_t val=0;
        for(size_t i=0;i<nprocs;i++){
            endReqs.emplace_back();
            MPI_Send(&val,1,MPI_UNSIGNED_LONG,i,0,MPI_COMM_WORLD);
        }
        bufReader.clear();
        //cleanup aftert waiting for the last messages to arrive
        for(size_t i=0;i<reqs.size();++i){
            MPI_Wait(&reqs[i],MPI_STATUS_IGNORE);
            if(reqs[i]!=MPI_REQUEST_NULL) {
                MPI_Wait(&reqs[i],MPI_STATUS_IGNORE);
                delete buffers[i];
            }
        }

        //collector+merging phase
        std::vector<RankRunConsumer> readers;
        for(size_t i=1;i<nprocs;i++){
            readers.emplace_back(i,0);
        }
        
        std::priority_queue<HeapNodeRecord, std::vector<HeapNodeRecord>, std::greater<HeapNodeRecord>> minPriorityQueue;
        size_t outWrittenBytes=0;
        BufferedRecordWriter outBufWriter(outFd,0,memory_limit/2);

        for(size_t i=0;i<readers.size();i++){
            const auto& record= readers[i].getRecord();
            if(record.has_value()){
                const auto& recordValue=record.value();
                minPriorityQueue.push({recordValue.key,recordValue.len,recordValue.payload,i});

            }
        }
        size_t currFileOffset=0;

        size_t j=0;
        //merging phase, receive from ranks and merge
        while (minPriorityQueue.size()!=0){
            auto [key,len,payload,way]=minPriorityQueue.top();
            minPriorityQueue.pop();
            Record r;
            r.key=key;
            r.len=len;
            r.payload=payload;
            ssize_t insertedBytes= outBufWriter.addRecord(r,false);
            if(insertedBytes==-1){
                //bufer is full, flush it and make a new one
                size_t size=outBufWriter.getbufferOffset();
                char* buffer=outBufWriter.extractBuffer();
                
                #pragma omp taskwait

                #pragma omp task shared(currFileOffset,outFd) firstprivate(buffer,size)
                {
                    ssize_t written= BufferedRecordWriter::flushBuffer(outFd,buffer,size,currFileOffset);
                    delete buffer;
                    currFileOffset+=written;
                }
                outBufWriter.addRecord(r,false);
            }


            outWrittenBytes+=Record::recordBytesSize(r);
            
            const auto& newRecord=readers[way].getRecord();
            if(newRecord.has_value()){
                const auto& recordValue=newRecord.value();
                minPriorityQueue.push({recordValue.key,recordValue.len,recordValue.payload,way});
            }
        }

        #pragma omp taskwait
        size_t size=outBufWriter.getbufferOffset();
        char* buffer=outBufWriter.extractBuffer();
        ssize_t written= BufferedRecordWriter::flushBuffer(outFd,buffer,size,currFileOffset);
        delete buffer; 
    }else{
        //worker node
        BufferedRecordWriter bufWriter(tmpFd,0,(memory_limit/2));
        std::vector<size_t> bytesOffsets;
        std::vector<size_t> sizes;
        size_t batchSize=0;

        size_t fileOffset=0;
        
        size_t currFileSize=0;

        size_t runsOffset=0;

        size_t numRecords=0;

        #pragma omp parallel
        {
            #pragma omp single
            {
                while(true){
                    //receiving from the master
                    MPI_Recv( &batchSize , 1 , MPI_UNSIGNED_LONG , 0 ,0, MPI_COMM_WORLD , MPI_STATUS_IGNORE);
                    if(batchSize==0) break;
                    currFileSize+=batchSize;
                    lseek(tmpFd,currFileSize,SEEK_SET);
                    char* buffer=new char[batchSize];

                    MPI_Recv( buffer , batchSize ,MPI_CHAR, 0 , 0 , MPI_COMM_WORLD , MPI_STATUS_IGNORE);

                    #pragma omp task firstprivate(batchSize,buffer) shared(bufWriter,bytesOffsets,runsOffset,sizes,numRecords)
                    {
                        //sorting the chunk
                        std::vector<Record> recordData= BufferedRecordReader::buildRecordBatch(buffer,batchSize);
                        std::sort(recordData.begin(),recordData.end());

                        char* newBuffer=new char[batchSize];
                        size_t newOffset=0;
                        for(size_t j=0;j<recordData.size();++j){
                            std::memcpy(newBuffer+newOffset,&recordData[j].key,sizeof(Record::key));
                            std::memcpy(newBuffer+newOffset+sizeof(Record::key),&recordData[j].len,sizeof(Record::len));
                            std::memcpy(newBuffer+newOffset+Record::headerBytesSize(),recordData[j].payload,recordData[j].len);
                            newOffset+= Record::recordBytesSize(recordData[j]);
                        }
                        //writing to the temporary file
                        #pragma omp critical
                        {   
                            bytesOffsets.push_back(runsOffset);
                            sizes.push_back(recordData.size());
                            ssize_t written= BufferedRecordWriter::flushBuffer(tmpFd,newBuffer,batchSize,runsOffset);
                            delete buffer;
                            delete newBuffer;
                            runsOffset+=batchSize;
                        }
                    }
                }
                #pragma omp taskwait
                
                bufWriter.flushBuffer();
                bufWriter.clear();

                fsync(tmpFd);
                
                size_t nWays=bytesOffsets.size();
                std::vector<BufferedRunConsumer> readers;
                for(size_t i=0;i<nWays-1;i++){
                    readers.emplace_back(tmpFd,bytesOffsets[i],memory_limit/(nWays+1),bytesOffsets[i+1]);
                }
                readers.emplace_back(tmpFd,bytesOffsets[nWays-1],memory_limit/(nWays+1),runsOffset);
                
                std::priority_queue<HeapNodeRecord, std::vector<HeapNodeRecord>, std::greater<HeapNodeRecord>> minPriorityQueue;
                size_t outWrittenBytes=0;
                BufferedRecordWriter outBufWriter(outFd,0,memory_limit/2);

                std::vector<size_t> recordCounter(nWays,1);
                for(size_t i=0;i<nWays;i++){
                    Record record= readers[i].getRecord();
                    minPriorityQueue.push({record.key,record.len,record.payload,i});
                }
                size_t currFileOffset=0;

                size_t currBatchSize=0;
                //merging phase
                while (minPriorityQueue.size()!=0){
                    auto [key,len,payload,way]=minPriorityQueue.top();
                    minPriorityQueue.pop();
                    Record r;
                    r.key=key;
                    r.len=len;
                    r.payload=payload;

                    ssize_t insertedBytes= outBufWriter.addRecord(r,false);
                    if(insertedBytes==-1){
                        //bufer is full, flush it and make a new one
                        size_t size=outBufWriter.getbufferOffset();
                        
                        char* buffer=outBufWriter.extractBuffer();

                        #pragma omp taskwait
                        //like in the singlenode, but send to master instead of write to file
                        #pragma omp task firstprivate(buffer,currBatchSize,size)
                        {   
                            MPI_Send( &currBatchSize , 1 , MPI_UNSIGNED_LONG, 0 , 0 , MPI_COMM_WORLD);
                            MPI_Send(buffer,size,MPI_CHAR,0,0,MPI_COMM_WORLD);
                            delete buffer;
                        }
                        insertedBytes= outBufWriter.addRecord(r,false);
                        currBatchSize=0;
                    }
                    currBatchSize+=insertedBytes;

                    if(recordCounter[way]<sizes[way]){
                        const auto& newRecord=readers[way].getRecord();
                        recordCounter[way]++;
                        minPriorityQueue.push({newRecord.key,newRecord.len,newRecord.payload,way});
                    }
                }
                #pragma omp taskwait
                size_t size=outBufWriter.getbufferOffset();
                if(size>0){

                    char* buffer=outBufWriter.extractBuffer();
                    MPI_Send( &size , 1 , MPI_UNSIGNED_LONG, 0 , 0 , MPI_COMM_WORLD);
                    MPI_Send(buffer,size,MPI_CHAR,0,0,MPI_COMM_WORLD);
                    delete buffer;
                }
                //send end of records
                size_t val=0;
                MPI_Send( &val , 1 , MPI_UNSIGNED_LONG, 0 , 0 , MPI_COMM_WORLD);

                outBufWriter.clear();
                std::filesystem::remove(tmp);

            }
        }
    }





    MPI_Barrier( MPI_COMM_WORLD);
    MPI_Finalize();
    if(rank==0){
        auto end_time = MPI_Wtime();;
        auto duration = (end_time - start_time);
        std::cout << "time(ms):" << duration*1000.0 << std::endl;
    }
    return 0;
}
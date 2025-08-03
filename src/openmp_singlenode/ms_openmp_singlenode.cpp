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
#include <filesystem>
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
    std::cout<<"starting from " << in_filename << " to "<< out_filename << " with a limit of "<< memory_limit/(1024UL*1024L*1024L)<< "GBs" << " payload max="<< payload_max <<std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();


    std::ifstream in_file(in_filename,std::ifstream::binary | std::ifstream::ate);
    size_t fileSize= in_file.tellg();
    in_file.close();
    std::ofstream tmpFile("tmp_run",std::ofstream::binary | std::ofstream::trunc);
    tmpFile.seekp(fileSize-1);
    tmpFile.put(0);
    tmpFile.close();

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
    std::string tmp="tmp_run";
    int tmpFd =open(tmp.c_str(),O_RDWR);
    if (tmpFd < 0) {
        perror("open tmp file");
        return 1;
    }
    omp_set_num_threads(threads_num);
    #pragma omp parallel
    {
        #pragma omp single
        {
            size_t currentOffset=0;
        
            std::vector<size_t> bytesOffsets;
            std::vector<size_t> sizes;
        
            BufferedRecordWriter bufWriter(tmpFd,0,(memory_limit/2));
            BufferedRecordReader bufReader(inFd,0,std::max((memory_limit/2)/threads_num,32UL*1024UL*1024UL ));
        
            std::vector<BufferedRecordReader> bufReaders;
            std::vector<char*> buffers;
            size_t runsOffset=0;
            while(currentOffset<fileSize){

                auto [newOffset,recordData]=bufReader.getRecords(fileSize);
                size_t batchSize=newOffset-currentOffset;
                char* buffer=bufReader.extractBuffer();
                buffers.push_back(buffer);
                //bytesOffsets.push_back(currentOffset);
                
                currentOffset=newOffset;
                
                #pragma omp task firstprivate(recordData,buffer,batchSize) shared(bufWriter,bytesOffsets,runsOffset,sizes)
                {
                    std::sort(recordData.begin(),recordData.end());
                    #pragma omp critical
                    {
                        bufWriter.addRecords(recordData.data(),recordData.size(),true);
                        bytesOffsets.push_back(runsOffset);
                        sizes.push_back(recordData.size());
                        delete buffer;
                        runsOffset+=batchSize;
                    }
                }
            }

            #pragma omp taskwait

            bufWriter.flushBuffer();
            bufWriter.clear();
            bufReader.clear();

            fsync(tmpFd);
            lseek(tmpFd, 0, SEEK_SET);
            
            size_t nWays=bytesOffsets.size();
            std::vector<BufferedRunConsumer> readers;

            for(size_t i=0;i<nWays-1;i++){
                readers.emplace_back(tmpFd,bytesOffsets[i],memory_limit/(nWays+1),bytesOffsets[i+1]);
            }
            readers.emplace_back(tmpFd,bytesOffsets[nWays-1],memory_limit/(nWays+1),fileSize);
            
            std::priority_queue<HeapNodeRecord, std::vector<HeapNodeRecord>, std::greater<HeapNodeRecord>> minPriorityQueue;
            size_t outWrittenBytes=0;
            BufferedRecordWriter outBufWriter(outFd,0,memory_limit/(nWays+1));

            std::vector<size_t> recordCounter(nWays,1);
            for(size_t i=0;i<nWays;i++){
                Record record= readers[i].getRecord();
                minPriorityQueue.push({record.key,record.len,record.payload,i});
            }
            size_t currFileOffset=0;

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
                
                if(recordCounter[way]<sizes[way]){
                    const auto& newRecord=readers[way].getRecord();
                    recordCounter[way]++;
                    minPriorityQueue.push({newRecord.key,newRecord.len,newRecord.payload,way});
                }

            }

            #pragma omp taskwait
            //outBufWriter.flushBuffer();
            size_t size=outBufWriter.getbufferOffset();
            char* buffer=outBufWriter.extractBuffer();
            ssize_t written= BufferedRecordWriter::flushBuffer(outFd,buffer,size,currFileOffset);
            delete buffer;
            outBufWriter.clear();
            for(size_t i=0;i<nWays;i++){
                //    readers[i].clear();
            }
            std::filesystem::remove("tmp_run");
            close(outFd);
            close(inFd);
            close(tmpFd);
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;
    
    return 0;
}
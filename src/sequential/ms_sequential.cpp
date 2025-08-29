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
#include <algorithm>
#include <queue>
#include <filesystem>
#include <sys/stat.h>
#include <execution>

int main(int argc,char*argv[]){
    uint64_t records_num=0;
    size_t threads_num=0;
    bool verbose = false;
    std::string in_filename="";
    std::string out_filename="";
    uint64_t memory_limit=MAX_MEMORY_LIMIT;
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

    
    
    size_t currentOffset=0;

    std::vector<size_t> bytesOffsets;
    std::vector<size_t> sizes;

    int inFd =open(in_filename.c_str(),O_RDONLY);
    if (inFd < 0) {
        perror("open input file");
        return -1;
    }
    int outFd =open(out_filename.c_str(),O_WRONLY);
    if (outFd < 0) {
        perror("open output file");
        return -1;
    }
    std::string tmp="tmp_run";
    int tmpFd =open(tmp.c_str(),O_RDWR);
    if (tmpFd < 0) {
        perror("open tmp file");
        return -1;
    }

    //sorting phase
    BufferedRecordWriter bufWriter(tmpFd,0,memory_limit/2);
    BufferedRecordReader bufReader(inFd,0,memory_limit/2);
    while(currentOffset<fileSize){
        auto [newOffset,recordData]=bufReader.getRecords(fileSize);
        std::sort(recordData.begin(),recordData.end());
        ssize_t bytes= bufWriter.addRecords(recordData.data(),recordData.size(),true);
        bytesOffsets.push_back(currentOffset);
        sizes.push_back(recordData.size());
        currentOffset=newOffset;
    }
    bufWriter.flushBuffer();
    bufWriter.clear();
    bufReader.clear();

    //merging phase
    size_t nWays=bytesOffsets.size();
    std::vector<BufferedRunConsumer> readers;
    for(size_t i=0;i<nWays-1;i++){
        readers.emplace_back(tmpFd,bytesOffsets[i],memory_limit/(nWays+1),bytesOffsets[i+1]);
    }
    readers.emplace_back(tmpFd,bytesOffsets[nWays-1],memory_limit/(nWays+1),fileSize);


    std::priority_queue<HeapNodeRecord, std::vector<HeapNodeRecord>, std::greater<HeapNodeRecord>> minPriorityQueue;
    std::vector<size_t> recordCounter(nWays,1);
    size_t outBufferBytes=0;

    BufferedRecordWriter outBufWriter(outFd,0,memory_limit/(nWays+1));


    for(size_t i=0;i<nWays;i++){
        Record record= readers[i].getRecord();
        minPriorityQueue.push({record.key,record.len,record.payload,i});
        outBufferBytes+= Record::recordBytesSize(record);
    }

    size_t lastKey=0;
    size_t i=0;
    //merging loop
    while (minPriorityQueue.size()!=0){
        auto [key,len,payload,way]=minPriorityQueue.top();
        minPriorityQueue.pop();

        Record r;
        r.key=key;
        r.len=len;
        r.payload=payload;
        lastKey=key;
        i++;
        

        outBufWriter.addRecord(r,true);
        if(recordCounter[way]<sizes[way]){
            recordCounter[way]++;
            const auto& newRecord=readers[way].getRecord();
            outBufferBytes+= Record::recordBytesSize(newRecord);
            minPriorityQueue.push({newRecord.key,newRecord.len,newRecord.payload,way});
        }
    }
    
    outBufWriter.flushBuffer();
    outBufWriter.clear();
    close(outFd);
    close(inFd);
    close(tmpFd);
    std::filesystem::remove("tmp_run");
    
    

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;

    return 0;
}
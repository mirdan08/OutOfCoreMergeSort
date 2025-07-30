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

struct HeapNode {
    uint64_t key;          // Key for sorting
    size_t pos;            // Position in data of this element
    size_t way;
    // This operator makes the priority_queue a min-heap by key
    bool inline operator>(const HeapNode& other) const {
        return key > other.key;
    }
};

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
    std::cout<<"starting from " << in_filename << " to "<< out_filename << " with a limit of "<< memory_limit/(1024UL*1024L*1024L)<< "GBs" << " payload max="<< payload_max <<std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();


    std::ifstream in_file(in_filename,std::ifstream::binary | std::ifstream::ate);
    size_t fileSize= in_file.tellg();
    in_file.close();
    std::ofstream tmpFile("tmp_"+out_filename,std::ofstream::binary);
    tmpFile.seekp(fileSize-1);
    tmpFile.put(0);
    tmpFile.close();

    std::ofstream outFile(out_filename,std::ofstream::binary);
    tmpFile.seekp(fileSize-1);
    tmpFile.put(0);
    tmpFile.close();

    
    
    size_t currentOffset=0;

    std::vector<size_t> bytesOffsets;
    std::vector<size_t> sizes;

    while(currentOffset<fileSize){
        const auto [bytesRead,recordData]=readRecords(in_filename,memory_limit,currentOffset);
        std::sort(recordData->begin(),recordData->end());
        writeRecords("tmp_"+out_filename,currentOffset,recordData->data(),recordData->size(),payload_max,memory_limit);
        bytesOffsets.push_back(currentOffset);
        sizes.push_back(recordData->size());
        currentOffset+=bytesRead;
        delete recordData;
    }

    size_t nWays=bytesOffsets.size();
    std::vector<RecordVec*> ways(nWays);
    std::vector<size_t> waysBytesOffsets(nWays,0);
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> minPriorityQueue;
    std::vector<size_t> recordCounter(nWays,1);
    for(size_t i=0;i<nWays;++i){
        const auto [bytesRead,recordData]=readRecords("tmp_"+out_filename,memory_limit/(nWays+1),bytesOffsets[i]);
        waysBytesOffsets[i]=bytesOffsets[i]+bytesRead;
        ways[i]=recordData;
        minPriorityQueue.push({recordData->at(0).key,0,i});
    }

    size_t outBufferBytes=0;
    size_t outBufferOffset=0;
    std::vector<Record> outputBuffer;

    std::cout << "file size " <<fileSize << std::endl;

    while (minPriorityQueue.size()!=0){
        auto [key,pos,way]=minPriorityQueue.top();
        minPriorityQueue.pop();
        const auto& currentRecord=ways[way]->at(pos);
        outBufferBytes+= Record::recordBytesSize(currentRecord);
        outputBuffer.push_back(std::move(currentRecord));
        recordCounter[way]++;
        // this way has exhausted records, load new values if present
        if(pos==ways[way]->size()-1 && recordCounter[way]<sizes[way]){
            delete ways[way];
            const auto& [bytesRead,recordData]=readRecords("tmp_"+out_filename,memory_limit/(nWays+1),waysBytesOffsets[way]);
            waysBytesOffsets[way]+=bytesRead;
            ways[way]=recordData;
            minPriorityQueue.push({ways[way]->at(0).key,0,way});
        }
        //there are still values to read from a way and way is no exhausted
        else if(recordCounter[way]<sizes[way]){
            minPriorityQueue.push({ways[way]->at(pos+1).key,pos+1,way});
        }
        //buffer is full flush it
        if(outBufferBytes>=memory_limit/(nWays+1)){
            std::cout<< "writing from " <<outBufferOffset << std::endl;
            writeRecords(out_filename,outBufferOffset,outputBuffer.data(),outputBuffer.size(),payload_max,memory_limit/(nWays+1));
            outputBuffer.clear();
            outBufferOffset+=outBufferBytes;
            outBufferBytes=0;
        }
    }


    
    outputBuffer.clear();

    std::filesystem::remove("tmp_"+out_filename);

    for(int i=0;i<nWays;i++){
        if(ways[i]!=nullptr) delete ways[i];
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;

    return 0;
}
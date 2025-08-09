#include <cstdint>
#include <fstream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <omp.h>
#include<optional>
#include<cassert>
#include<mpi.h>

#pragma once

#define _FILE_OFFSET_BITS 64#ifndef RPAYLOAD_MAX
#define RPAYLOAD_MAX 100

//default max memory limit for single node
const unsigned long MAX_MEMORY_LIMIT=1UL*1024UL*1024UL*1024UL;
//a single record struct
struct Record {
    uint32_t len; 
    uint64_t key; 
    char* payload;
    bool inline operator<(const auto& other){
        return key<other.key;
    }
    static size_t headerBytesSize(){
        return sizeof(Record::len)+sizeof(Record::key);
    }
    static size_t recordBytesSize(const Record& instance){
        return headerBytesSize()+instance.len;
    }
};

struct HeapNodeRecord {
    uint64_t key;          // Key for sorting
    uint32_t len;            // Position in data of this element
    char* payload;
    size_t way;
    // This operator makes the priority_queue a min-heap by key
    bool inline operator>(const HeapNodeRecord& other) const {
        return key > other.key;
    }
};

class BufferedRecordWriter{
    size_t bufferOffset=0;
    size_t fileOffset;
    size_t maxBufferSize;
    size_t bufferSize;
    char* buffer;
    int outFd=-1;
    size_t fileSize;

    public:
        BufferedRecordWriter(int fd,size_t fileOffset,size_t maxBufferSize)
            :fileOffset(fileOffset),
                maxBufferSize(maxBufferSize)
        {
            buffer= new char[maxBufferSize];
            bufferSize=maxBufferSize;
            setFile(fd,fileOffset);
        }

        inline ssize_t addRecords(Record* records,const size_t recordsNum,bool autoFlush){
            ssize_t written=0;
            for(size_t i=0;i<recordsNum;++i){
                written+=addRecord(records[i],autoFlush);
            }
            return written;
        }

        inline ssize_t addRecord(const Record& record,bool autoFlush){
            if(Record::recordBytesSize(record)+bufferOffset>=bufferSize && autoFlush) flushBuffer();
            if(Record::recordBytesSize(record)+bufferOffset>=bufferSize && !autoFlush) return -1;
            std::memcpy(buffer+bufferOffset,&record.key,sizeof(Record::key));
            std::memcpy(buffer+bufferOffset+sizeof(Record::key),&record.len,sizeof(Record::len));
            std::memcpy(buffer+bufferOffset+Record::headerBytesSize(),record.payload,record.len);
            
            bufferOffset+=Record::recordBytesSize(record);

            return Record::recordBytesSize(record);
        }
        inline void setFileOffset(size_t fileOffset){
            this->fileOffset=fileOffset;
        }
        inline size_t getbufferOffset(){
            return bufferOffset;
        }
        inline size_t getFileOffset(){
            return fileOffset;
        }
        bool setFile(int fd,size_t fileOffset){
            outFd = fd;
            if (outFd < 0) {
                perror("open output file");
                close(outFd);
                return false;
            }
            bufferOffset=0;
            fileOffset=fileOffset;
            return true;
        }
        
        inline ssize_t flushBuffer(){
            return flushBuffer(this->buffer);
        }

        inline char* extractBuffer(){
            char* oldBuffer=buffer;
            buffer=new char[maxBufferSize];
            bufferOffset=0;
            return oldBuffer;
        }

        static inline ssize_t flushBuffer(int outFd,char* buffer,size_t bufferSize,size_t fileOffset){
            ssize_t totalWritten = 0;
            while (totalWritten < bufferSize) {
                ssize_t written = pwrite(outFd, 
                    buffer + totalWritten, 
                    bufferSize - totalWritten, 
                    fileOffset + totalWritten);
                if (written < 0) {
                    if (errno == EINTR) continue;
                    perror("pwrite");
                    return -1;
                }
                if(written==0) break;
                totalWritten += written;
            }
            return totalWritten;
        }

        inline ssize_t flushBuffer(char* buffer){
            ssize_t totalWritten = 0;
            while (totalWritten < bufferOffset) {
                ssize_t written = pwrite(outFd, 
                    buffer + totalWritten, 
                    bufferOffset - totalWritten, 
                    fileOffset + totalWritten);
                    if (written < 0) {
                        if (errno == EINTR) continue; // Interrupted? retry
                        perror("pwrite");
                        std::cout<< "error in buffer flushing" << std::endl;
                        return -1;
                    }
                    if(written==0) break;
                    totalWritten += written;
            }
            
            fileOffset+=totalWritten;
            bufferOffset=0;
            return totalWritten;
        }

        ssize_t getBufferSize(){
            return bufferSize;
        }

        ssize_t setBufferSize(size_t size){
            //cannot use more memory than max
            if(size>maxBufferSize) return -1;
            bufferSize=size;
            return bufferSize;
        }

        void clear(){
            if (buffer != nullptr){
                delete[] buffer;
                buffer = nullptr;
            }
        
        }

};

class BufferedRecordReader{
    uint64_t bufferOffset=0;
    uint64_t fileOffset;
    uint64_t maxBufferSize;
    char* buffer;
    int inFd=-1;

    size_t lastRecordPos=0;
    

    public:
        BufferedRecordReader(int fd,size_t fileOffset,size_t maxBufferSize)
            :fileOffset(fileOffset),
            maxBufferSize(maxBufferSize)
        {
            buffer=new char[maxBufferSize];
            maxBufferSize=maxBufferSize;
            setFile(fd,fileOffset);
        }
        
        static std::vector<Record> buildRecordBatch(char* buffer,size_t batchSize){
            size_t offset=0;
            std::vector<Record> records;
            while(offset<batchSize){
                uint64_t key;
                uint32_t len;
                char* bufferPtr;
                if(offset+Record::headerBytesSize()>=batchSize){ 
                    break;
                }
                std::memcpy(&key,buffer+offset,sizeof(Record::key));
                std::memcpy(&len,buffer+offset+sizeof(Record::key),sizeof(Record::len)); 

                if(offset+Record::headerBytesSize()+len>batchSize){
                    break;
                }
                bufferPtr=buffer+offset+Record::headerBytesSize();
                records.emplace_back(len,key,bufferPtr);
                offset+=Record::headerBytesSize()+len;
            }
            return records;
        }

        inline uint64_t getFileOffset(){
            return fileOffset;
        }

        inline std::pair<size_t,std::vector<Record>>getRecords(uint64_t limit){
            uint64_t bufferSize=std::min(maxBufferSize,limit-fileOffset);
            uint64_t totalRead=0;

            const uint64_t chunkSize=64UL*1024UL*1024UL;

            while(totalRead<bufferSize){
                uint64_t fileRemaining = limit - (fileOffset + totalRead);
                uint64_t bufferRemaining = bufferSize - totalRead;
                uint64_t toRead = std::min({fileRemaining, bufferRemaining, chunkSize});

                if(toRead==0) break;

                ssize_t nRead = pread(inFd, buffer+totalRead,toRead, fileOffset+totalRead);
                if (nRead < 0) {
                    perror("pread");
                    delete[] buffer;
                    close(inFd);
                    throw std::runtime_error("Failed during pread");
                }
                if(nRead==0) {
                    break;
                }

                totalRead+=(uint64_t)nRead;
            }
            std::vector<Record> result;
            bufferOffset=0;
            while(bufferOffset<bufferSize){
                //std::cout << bufferOffset << "/"<< bufferSize << std::endl;
                uint32_t len;
                uint64_t key;
                char* payload;
                //stop reading if the header is at the limit of the buffer
                if(bufferOffset+Record::headerBytesSize()>=bufferSize){ 
                    break;
                }
                std::memcpy(&key,buffer+bufferOffset,sizeof(Record::key));
                std::memcpy(&len,buffer+bufferOffset+sizeof(Record::key),sizeof(Record::len));
                //std::cout<< key << " " << len << std::endl;
                //stop reading if the len is over the limit of the buffer
                if(bufferOffset+sizeof(Record::key)+sizeof(Record::len)+len>bufferSize){
                    break;
                }
                //don't allocate new memory, use the buffer instead
                payload=buffer+bufferOffset+Record::headerBytesSize();

                bufferOffset+=sizeof(Record::key)+sizeof(Record::len)+len;
                //if everything goes well add it to the result to return
                Record record;
                record.key=key;
                record.len=len;
                record.payload=payload;
                result.push_back(record);
            }
            fileOffset+=bufferOffset;

            return std::pair(fileOffset,std::move(result));
        }
        /*
        Replaces the buffer and returns the old one
        */
        inline char* extractBuffer(){
            char *oldBuffer=buffer;
            buffer=new char[maxBufferSize];
            return oldBuffer;
        }

        bool setFile(int fd,size_t fileOffset){
            inFd =fd;
            if (inFd < 0) {
                perror("open output file");
                return false;
            }
            bufferOffset=0;
            fileOffset=fileOffset;
            return true;
        }

        ssize_t setBufferSize(size_t size){
            //cannot use more memory than max
            if(size>maxBufferSize) return -1;
            maxBufferSize=size;
            return maxBufferSize;
        }

        void clear(){
            if(buffer !=nullptr){
                delete[] buffer;
                buffer = nullptr;
            }
        }
};

class BufferedRunConsumer{
    BufferedRecordReader reader;
    std::vector<Record> records;
    size_t currentPos=0;
    size_t runLimit;
    size_t currOffset;
    public:
        BufferedRunConsumer(int fd,size_t fileOffset,size_t maxBufferSize,size_t runLimit)
            :reader(fd,fileOffset,maxBufferSize)
            ,runLimit(runLimit){

            }
        inline Record getRecord(){
            if(currentPos==records.size()){
                const auto [newOffset,newRecords]=reader.getRecords(runLimit);
                records=newRecords;
                currOffset=newOffset;
                currentPos=0;
            }
            return records[currentPos++];
        }

        void clear(){
            reader.clear();
        }
};

class RankRunConsumer{
    std::vector<Record> records;
    size_t currentPos=0;
    size_t runLimit;
    char* buffer=nullptr;
    int rank;
    int tag;
    public:
        RankRunConsumer(int rank,int tag):rank(rank),tag(tag){}
        inline std::optional<Record> getRecord(){
            if(currentPos==records.size()){

                MPI_Recv(&runLimit,1,MPI_UNSIGNED_LONG,rank,tag,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
                if(runLimit==0) return std::nullopt;
                if(buffer!=nullptr){
                    delete buffer;
                    buffer=nullptr;
                } 
                
                buffer=new char[runLimit];
                MPI_Recv(buffer,runLimit,MPI_CHAR,rank,tag,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

                records=BufferedRecordReader::buildRecordBatch(buffer,runLimit);
                currentPos=0;
            }
            return records[currentPos++];
        }

        void clear(){
            if(buffer!=nullptr) {
                delete buffer;
                buffer =nullptr;
            }
        }
};
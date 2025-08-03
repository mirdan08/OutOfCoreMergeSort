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

#pragma once

#ifndef RPAYLOAD_MAX
#define RPAYLOAD_MAX 100
#endif

#define _FILE_OFFSET_BITS 64

//max payload size
const unsigned long payload_max= RPAYLOAD_MAX;

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

struct MemoryRecord{
    uint32_t len; 
    uint64_t key; 
    uint64_t offset;
};
//used to sort the values within a single node
struct PosKeyPair{
    uint64_t key; 
    uint64_t pos;
    uint64_t len;
    uint64_t offset; 
};


void write_record(Record& record,std::ofstream& out_file);


using PosKeyVec=std::vector<PosKeyPair>;

using RecordVec=std::vector<Record>;

using IndexPair=std::pair<unsigned long,unsigned long>;
using SortResult=std::tuple<unsigned long,unsigned long,unsigned long>;

uint64_t ms_select(const PosKeyVec& data, const std::vector<IndexPair> ranges, int k) ;
uint64_t ms_select2(const std::vector<PosKeyPair>& data,
    const std::vector<std::pair<size_t, size_t>>& sorted_ranges,
    size_t global_rank) noexcept ;

size_t raw_upper_bound(const PosKeyPair* data, size_t size, uint64_t value) noexcept;
void k_way_merge_buffer(
        PosKeyPair* src_data,
        std::vector<IndexPair>& subranges,
        PosKeyPair* dst_data,
        const size_t count
    ) noexcept;

std::vector<PosKeyPair> k_way_merge_from_ranges(const PosKeyVec& data,const std::vector<IndexPair>& subranges);
void radix_sort_by_key(PosKeyVec& data) noexcept;
void radix_sort_buffer(PosKeyPair* data, size_t n);
std::vector<PosKeyPair> k_way_merge_heap(
    const PosKeyVec& data,
    const std::vector<IndexPair>& subranges
) noexcept ;

//void radix_sort_slice(PosKeyVec& data, size_t start, size_t end) noexcept ;
void buffered_poskey_write(const std::string in_filename,const std::string out_filename,size_t file_offset,PosKeyPair* data,size_t data_count,size_t payload_max,size_t memory_limit);
void build_pivot_subrange(
    size_t start,size_t end, int j,
    const std::vector<uint64_t>& pivots,
    const std::vector<PosKeyPair>& data,
    std::vector<std::vector<IndexPair>>& bucket_subranges 
);

void buffered_poskey_write_pread(
    const std::string& in_filename,
    const std::string& out_filename,
    size_t file_offset,
    PosKeyPair* data,
    size_t data_count,
    size_t payload_max,
    size_t memory_limit);

std::pair<size_t,RecordVec*> bufferedRecordRead(
    const std::string& in_filename,
    const std::string& out_filename,
    size_t file_offset,
    PosKeyPair* data,
    size_t data_count,
    size_t payload_max,
    size_t memory_limit);


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
            //std::cout<< totalWritten << " "<< bufferSize << std::endl;
            while (totalWritten < bufferSize) {
                ssize_t written = pwrite(outFd, 
                    buffer + totalWritten, 
                    bufferSize - totalWritten, 
                    fileOffset + totalWritten);
                if (written < 0) {
                    if (errno == EINTR) continue; // Interrupted? retry
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
            std::cout<< currentPos << " " <<records.size() << std::endl;
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
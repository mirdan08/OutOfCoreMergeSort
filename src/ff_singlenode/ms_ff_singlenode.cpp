
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
#include <filesystem>

struct RecordBatchEmitter: ff::ff_monode_t<
    int,
    std::tuple<size_t, std::vector<Record>,char*>
>{
    size_t currentOffset=0;
        
    std::vector<size_t> bytesOffsets;
    std::vector<size_t> sizes;


    BufferedRecordReader bufReader;
    size_t fileSize;
    public:
        RecordBatchEmitter(int inFd,int tmpFd,size_t readerMemoryLimit,size_t writerMemoryLimit,size_t fileSize):
            bufReader(inFd,0,std::max(writerMemoryLimit,32UL*1024UL*1024UL )),
            fileSize(fileSize)

            {}

        std::tuple<size_t, RecordVec,char*>* svc(int* in){
            while(currentOffset<fileSize){
                auto [newOffset,recordData]=bufReader.getRecords(fileSize);
                size_t batchSize=newOffset-currentOffset;
                char* buffer=bufReader.extractBuffer();
                currentOffset+=batchSize;
                ff_send_out(new std::tuple<size_t, RecordVec, char*>(batchSize, recordData, buffer));
            }
            bufReader.clear();
            return EOS;
        }    
};

struct RecordBatchSorter: ff::ff_minode_t<
    std::tuple<size_t, RecordVec, char*>,
    std::tuple<size_t, RecordVec, char*>
>{
    std::tuple<size_t, RecordVec, char*>* svc(std::tuple<size_t, RecordVec, char*>* in){
        auto [batchSize,records,buffer] = *in;
        std::sort(records.begin(),records.end());
        return new std::tuple<size_t, RecordVec, char*>(batchSize,records,buffer);
    }
};

struct RecordBatchCollector: ff::ff_minode_t<
    std::tuple<size_t, RecordVec, char*>,
    std::pair<std::vector<size_t>,std::vector<size_t>>
>
{
    BufferedRecordWriter bufWriter;
    size_t currFilleOffset=0;
    size_t runsOffset=0;
    std::vector<size_t> sizes;
    std::vector<size_t> offsets;
    size_t fileSize;
    int tmpFd;

    RecordBatchCollector(int tmpFd,size_t readerMemoryLimit,size_t fileSize):
        bufWriter(tmpFd,0,readerMemoryLimit),
        fileSize(fileSize),
        tmpFd(tmpFd)
        {}

    std::pair<std::vector<size_t>,std::vector<size_t>>* svc(std::tuple<size_t, RecordVec, char*>* in){
        auto [batchSize,recordData,buffer] = *in;
        
        sizes.push_back(recordData.size());
        offsets.push_back(runsOffset);
        runsOffset+=batchSize;
        bufWriter.addRecords(recordData.data(),recordData.size(),true);

        delete buffer;
        if(runsOffset<fileSize) return GO_ON;
        ff_send_out(new std::pair(sizes,offsets));
        
        bufWriter.flushBuffer();
        bufWriter.clear();
        fsync(tmpFd);
        lseek(tmpFd, 0, SEEK_SET);

        return EOS;
    }
};

struct RecordMerger: ff::ff_minode_t<
    std::pair<std::vector<size_t>,std::vector<size_t>>,
    void
>
{
    size_t fileSize;
    size_t memoryLimit;
    int tmpFd;
    int outFd;
    std::atomic<bool>& writerBusy;

    RecordMerger(std::atomic<bool>& writerBusy,int tmpFd,int outFd,size_t fileSize,size_t memoryLimit):
        writerBusy(writerBusy),
        tmpFd(tmpFd),
        outFd(outFd),
        memoryLimit(memoryLimit),
        fileSize(fileSize)
    {}

    void* svc(std::pair<std::vector<size_t>,std::vector<size_t>>* in){
        
        auto [sizes,offsets] = *in;
        
        size_t nWays=offsets.size();
        std::vector<BufferedRunConsumer> readers;
        
        for(size_t i=0;i<nWays-1;i++){
            readers.emplace_back(tmpFd,offsets[i],memoryLimit/(nWays+1),offsets[i+1]);
        }
        readers.emplace_back(tmpFd,offsets[nWays-1],memoryLimit/(nWays+1),fileSize);
        
        std::priority_queue<HeapNodeRecord, std::vector<HeapNodeRecord>, std::greater<HeapNodeRecord>> minPriorityQueue;
        size_t outWrittenBytes=0;
        BufferedRecordWriter outBufWriter(outFd,0,memoryLimit/(nWays+1));
        
        std::vector<size_t> recordCounter(nWays,1);
        for(size_t i=0;i<nWays;i++){
            Record record= readers[i].getRecord();
            minPriorityQueue.push({record.key,record.len,record.payload,i});
        }
        
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

                while(writerBusy.load());
                writerBusy.store(true);

                ff_send_out(new std::tuple<char*,size_t,size_t>(buffer,size,0));
                outBufWriter.addRecord(r,false);
            }
            outWrittenBytes+=Record::recordBytesSize(r);
            
            if(recordCounter[way]<sizes[way]){
                const auto& newRecord=readers[way].getRecord();
                recordCounter[way]++;
                minPriorityQueue.push({newRecord.key,newRecord.len,newRecord.payload,way});
            }

        }
        
        while(writerBusy.load());
        writerBusy.store(true);
        
        size_t size=outBufWriter.getbufferOffset();
        char* buffer=outBufWriter.extractBuffer();
        ff_send_out(new std::tuple<char*,size_t,size_t>(buffer,size,0));

        outBufWriter.clear();
        for(size_t i=0;i<nWays;i++){
            readers[i].clear();
        }

        return EOS;
    }
};

struct RecordBatchWriter: ff::ff_minode_t<
std::tuple<char*,size_t,size_t>,
void
>{
    std::atomic<bool>& writerBusy;
    size_t globalFileOffset=0;
    int outFd;

    RecordBatchWriter(std::atomic<bool>& writerBusy,int outFd)
    :writerBusy(writerBusy),globalFileOffset(globalFileOffset),outFd(outFd){}

    void* svc(std::tuple<char*,size_t,size_t>* in){
        auto [buffer,size,fileOffset]=*in;
        ssize_t written= BufferedRecordWriter::flushBuffer(outFd,buffer,size,globalFileOffset);
        globalFileOffset+=written;
        writerBusy.store(false);
        return GO_ON;
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

    RecordBatchEmitter recordBatchEmitter(inFd,tmpFd,memory_limit/2,(memory_limit/2)/threads_num,fileSize);
    
    ff::ff_node* recordBatchCollector=new RecordBatchCollector (tmpFd,memory_limit/2,fileSize);

    std::vector<ff::ff_node*> sorters;
    for(size_t i=0;i<threads_num;i++){
        sorters.push_back(new RecordBatchSorter());
    }

    ff::ff_farm runFarm;
    runFarm.add_emitter(recordBatchEmitter);
    runFarm.add_workers(sorters);
    runFarm.add_collector(recordBatchCollector);
    
    std::atomic<bool> writerBusy(false);
    RecordMerger recordMerger(writerBusy,tmpFd,outFd,fileSize,memory_limit);

    ff::ff_farm mergeFarm;

    mergeFarm.add_emitter(recordMerger);

    std::vector<ff::ff_node*> writer;
    writer.push_back(new RecordBatchWriter(writerBusy,outFd));
    mergeFarm.add_workers(writer);
    mergeFarm.remove_collector();
    
    ff::ff_pipeline msFarm;
    msFarm.add_stage(runFarm);
    msFarm.add_stage(mergeFarm);
    msFarm.run_and_wait_end();
    
    std::filesystem::remove("tmp_run");
    close(outFd);
    close(inFd);
    close(tmpFd);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout<< "time(ms):" << duration.count() << std::endl;

    return 0;
}
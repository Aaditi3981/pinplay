/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2016 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
#ifndef ISIMPOINT_INST_H
#define ISIMPOINT_INST_H

#include <map>
#include <queue>
#include <iostream>
#include <fstream>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "pin.H"
#include "instlib.H"
#include "reuse_distance.H"

#define ISIMPOINT_MAX_IMAGES 250
#define ADDRESS64_MASK (~63)

class IMG_INFO
{
    public:
    IMG_INFO(IMG img, INT32 Id);
        INT32 Id() { return _imgId;}
        CHAR * Name() { return _name;}
        ADDRINT  LowAddress() { return _low_address;}
    private:
        CHAR * _name; 
        ADDRINT _low_address; 
        INT32 _imgId;
};

IMG_INFO::IMG_INFO(IMG img, INT32 Id)
{
    _imgId = Id;
    _name = (CHAR *) calloc(strlen(IMG_Name(img).c_str())+1, 1);
    strcpy(_name,IMG_Name(img).c_str());
    _low_address = IMG_LowAddress(img);
}

class IMG_MANAGER
{
  private:
    IMG_INFO * img_info[ISIMPOINT_MAX_IMAGES];
    INT32 _currentImgId;    

  public:
    VOID AddImage(IMG img)
    {
        ASSERTX(_currentImgId < (ISIMPOINT_MAX_IMAGES - 1));
        img_info[_currentImgId] = new IMG_INFO(img, _currentImgId);
        _currentImgId++;
    }
    IMG_INFO * GetImageInfo(INT32 id)
    {
        return img_info[id];
    }
    UINT32 FindImgInfoId(IMG img)
    {
        if (!IMG_Valid(img))
        {
            return 0;
        }
        
        ADDRINT low_address = IMG_LowAddress(img);
        
        for (UINT32 i = _currentImgId-1; i >=1; i--)
        {
            if(img_info[i]->LowAddress() == low_address)
            {
                return i;
            }
        }
        // cerr << "FindImgInfoId(0x" << hex << low_address << ")" <<   endl;
        
        
        return 0;
    }
};

class BLOCK_KEY
{
    friend BOOL operator<(const BLOCK_KEY & p1, const BLOCK_KEY & p2);
        
  public:
    BLOCK_KEY(ADDRINT s, ADDRINT e, USIZE z) : _start(s),_end(e),_size(z) {};
    BOOL IsPoint() const { return (_start - _end) == 1;  }
    ADDRINT Start() const { return _start; }
    ADDRINT End() const { return _end; }
    USIZE Size() const { return _size; }
    BOOL Contains(ADDRINT addr) const;
    
  private:
    const ADDRINT _start;
    const ADDRINT _end;
    const USIZE _size;
};

class PROFILE;
class ISIMPOINT;

LOCALTYPE typedef map<INT32, INT64> BLOCK_COUNT_MAP;
LOCALTYPE typedef enum 
    { 
           LDV_TYPE_NONE = 0,
           LDV_TYPE_APPROXIMATE = 1,
           LDV_TYPE_EXACT = 2
    }LDV_TYPE;

class LDV
{
  private:
    static const UINT32 MAX_BINS = 30;
    RD *_rd;
    std::vector<UINT64> _counts;
  public:
    LDV(LDV_TYPE type)
        : _counts(MAX_BINS+1, 0)
    {
        if (type == LDV_TYPE_APPROXIMATE)
            _rd = new RD_LogRR();
        else if (type == LDV_TYPE_EXACT)
            _rd = new RD_Treap();
        else
            _rd = NULL;
    }
    ~LDV()
    {
        if (_rd)
            delete _rd;
    }
    VOID emit(ofstream &BbFile)
    {
        for(UINT64 bin = 0; bin <= MAX_BINS; ++bin)
        {
            UINT64 value = _counts[bin];
            if (value)
                BbFile << ":" << dec << bin << ":" << dec << value << " ";
            _counts[bin] = 0;
        }
    }

    VOID access(ADDRINT address)
    {
        ASSERTX(_rd);
        UINT32 dist_log2 = _rd->reference(address);
        if (dist_log2 > MAX_BINS)
            dist_log2 = MAX_BINS;
        ++_counts[dist_log2];
    }
};

class BLOCK
{
  public:
    BLOCK(const BLOCK_KEY & key, INT32 instructionCount, INT32 id, INT32 imgId);
    INT32 StaticInstructionCount() const { return _staticInstructionCount; }
    VOID Execute(THREADID tid) { _sliceBlockCount[tid]++; }
    VOID Execute(THREADID tid, const BLOCK* prev_block, ISIMPOINT *isimpoint);
    VOID EmitSliceEnd(THREADID tid, PROFILE *profile);
    VOID EmitProgramEnd(const BLOCK_KEY & key, THREADID tid,
        PROFILE * profile, const ISIMPOINT *isimpoint) const;
    INT64 GlobalBlockCount(THREADID tid) const 
        { return _globalBlockCount[tid] + _sliceBlockCount[tid]; }
    UINT32 ImgId() const { return _imgId; }
    const BLOCK_KEY & Key() const { return _key; }
    INT32 Id() const { return _id; }
    
  private:
    INT32 SliceInstructionCount(THREADID tid) const 
        { return _sliceBlockCount[tid] * _staticInstructionCount; }

    const INT32 _staticInstructionCount; // number of instrs in this block.
    INT32 _id;
    const BLOCK_KEY _key;

    INT32 _sliceBlockCount[PIN_MAX_THREADS]; 
    // times this block was executed in the current slice.
    INT64 _globalBlockCount[PIN_MAX_THREADS]; 
    // times this block was executed prior to the current slice.
    UINT32 _imgId;
    BLOCK_COUNT_MAP _blockCountMap[PIN_MAX_THREADS]; 
    // counter for each previous block.
};

LOCALTYPE typedef pair<BLOCK_KEY, BLOCK*> BLOCK_PAIR;
LOCALTYPE typedef map<BLOCK_KEY, BLOCK*> BLOCK_MAP;

LOCALTYPE typedef queue<UINT64> REGION_LENGTHS_QUEUE;
    
class PROFILE
{
    private:
    static const UINT32 BUFSIZE=100;

    public: 
    PROFILE(INT64 slice_size, LDV_TYPE ldv_type)
        : _ldvState(ldv_type)
    {
        first = true;
        active = false;
        first_eip = 0;
        GlobalInstructionCount = 0;
        SliceTimer = slice_size; // may be updated with "-length lfile"
        CurrentSliceSize = slice_size;// may be updated with "-length lfile"
        last_block = NULL;
    }
    VOID OpenFile(THREADID tid, UINT32 pid, string output_file, BOOL enable_ldv)
    {
        if ( !BbFile.is_open() )
        {
            char num[100];
            if (pid)
            {
                sprintf(num, ".T.%u.%d", (unsigned)pid, (int)tid);
            }
            else
            {
                sprintf(num, ".T.%d", (int)tid);
            }
            string tname = num;
            BbFile.open((output_file+tname+".bb").c_str());
            BbFile.setf(ios::showbase);

            if (enable_ldv)
            {
               LdvFile.open((output_file+tname+".ldv").c_str());
            }
        }
    }
    VOID ReadLengthFile(THREADID tid, string length_file)
    {
        ifstream lfile(length_file.c_str());
        ASSERT(lfile.is_open(), "Could not open length file:"+length_file);
        UINT32 lineNum = 0;
        UINT32 recordNum = 0;
        while(true)
        {
            if( lfile.eof() )
            {
                break;
            }

            CHAR record[BUFSIZE+1];
            string field;

            lfile.getline(record, BUFSIZE);
            lineNum++;

            if(strlen(record)==0) continue;

            // first letter '#' : this is a comment 
            if(record[0]=='#') continue;

            istringstream s(record);

            field.clear();
            getline(s, field);
            ASSERT(!field.empty(), "Empty length field.");
            if(recordNum == 0)
            {
                CurrentSliceSize = SliceTimer 
                    = (INT64) Uint64FromString(field);
                // cerr << "First SliceTimer " << SliceTimer << endl;
            }
            else
            {
                length_queue.push(Uint64FromString(field));
            }
            recordNum++;
        }
        lfile.close();
    }
    VOID ExecuteMemory(ADDRINT address) 
        { _ldvState.access (address & ADDRESS64_MASK); }
    VOID EmitLDV() { _ldvState.emit(LdvFile); }

    ofstream BbFile;
    ofstream LdvFile;
    INT64 GlobalInstructionCount;
    // The first time, we want a marker, but no T vector
    ADDRINT first_eip;
    BOOL first;
    BOOL active;
    // Emit the first marker immediately
    INT64 SliceTimer;
    INT64 CurrentSliceSize;
    BLOCK *last_block;
    LDV _ldvState;
    REGION_LENGTHS_QUEUE length_queue;
};

class ISIMPOINT
{
    BLOCK_MAP block_map;
    string commandLine;    
    UINT32 Pid;
    PROFILE ** profiles;
    IMG_MANAGER img_manager;
    // If KnobEmitPrevBlockCounts is enabled, this array is used 
    // to assign an ID to each block as it is executed.
    // Otherwise, the ids are assigned at instrumentation time and only 
    // the first entry in the vector is used,
    // since we don't know the thread id at instrumentation. Assigning 
    // at instrumentation time is more efficient
    // if one does not care for the ID assignment order.
    THREADID _currentId[PIN_MAX_THREADS];

  public:
    ISIMPOINT()
        :
        knob_family("pintool:isimpoint",
                    "Basic block profile knobs"),
        isimpoint_knob(
            KNOB_MODE_WRITEONCE,
            "pintool:isimpoint",
            "bbprofile",
            "0",
            "Activate bbprofile / isimpoint."),

        KnobOutputFile(
            KNOB_MODE_WRITEONCE, "pintool:isimpoint",
            "o", "out", "specify bb file name"),
        KnobSliceSize(
            KNOB_MODE_WRITEONCE,  "pintool:isimpoint",
            "slice_size", "100000000", "slice size in instructions"),
        KnobNoSymbolic(
            KNOB_MODE_WRITEONCE,  "pintool:isimpoint",
            "nosymbolic", "0", "Do not emit symbolic information for markers"),
        KnobEmitFirstSlice(
            KNOB_MODE_WRITEONCE,  "pintool:isimpoint",
            "emit_first", "1",
            "Emit the first interval (higher overhead to find out first IP)"),
        KnobEmitLastSlice(
            KNOB_MODE_WRITEONCE,  "pintool:isimpoint",
            "emit_last", "1",
            "Emit the last interval even if it is less than slice_size"),
        KnobEmitPrevBlockCounts(
            KNOB_MODE_WRITEONCE,  "pintool:isimpoint",
            "emit_previous_block_counts", "0",
            "Emit exec counts of preceding blocks in ( blk:count ... ) format"),
        KnobPid (KNOB_MODE_WRITEONCE,  "pintool:isimpoint",
                 "pid", "0", "Use PID for naming files."),
        KnobLDVType(
            KNOB_MODE_WRITEONCE,  "pintool:isimpoint",
            "ldv_type", "none",
            "Enable collection of LRU stack distance vectors "
            "(none(default), \"approx\", \"exact\" )"),
        KnobLengthFile(KNOB_MODE_APPEND, "pintool:isimpoint",
                     "lengthfile", "",
                     "Length(instruction count)  of execution regions"
                     ": must specify ':tidN' suffix."
                     )
    {
        Pid = 0;
        for (UINT32 i = 0; i < PIN_MAX_THREADS; i++)
            _currentId[i] = 1;
    }

    INT32 Usage()
    {
        cerr <<
            "This tool collects profiles for SimPoint.\n"
            "\n";
        cerr << KNOB_BASE::StringKnobSummary() << endl;
        return -1;
    }

    VOID EmitSliceStartInfo(ADDRINT endMarker, INT64 markerCount, UINT32 imgId, THREADID tid)
    {
        if(!imgId)
        {
            profiles[tid]->BbFile << "M: " << hex << endMarker << " " <<
                dec << markerCount << " " << "no_image" << " " 
                << hex  << 0 << endl;
        }
        else
        {
            IMG_INFO *img_info = img_manager.GetImageInfo(imgId);
            profiles[tid]->BbFile << "S: " << hex << endMarker << " " <<
                dec << markerCount << " " << img_info->Name() << " " <<
                hex  <<img_info->LowAddress() << " + " <<
                hex << endMarker-img_info->LowAddress() << endl;
        }    
    }
    
    
    VOID EmitSliceEnd(ADDRINT endMarker, UINT32 imgId, THREADID tid)
    {
        
        INT64 markerCount = 0;
        
        if (profiles[tid]->first == true)
        {
            // Input merging will change the name of the input
            profiles[tid]->BbFile << "I: 0" << endl;
            profiles[tid]->BbFile << "P: " << dec << tid << endl;
            profiles[tid]->BbFile << "C: sum:dummy Command:" 
                << commandLine << endl;
            EmitSliceStartInfo(profiles[tid]->first_eip, 1, imgId, tid);        
        }
        
        profiles[tid]->BbFile << "# Slice ending at " << dec 
            << profiles[tid]->GlobalInstructionCount << endl;
        
        if ( !profiles[tid]->first || KnobEmitFirstSlice )
            profiles[tid]->BbFile << "T" ;

        for (BLOCK_MAP::const_iterator bi = block_map.begin(); 
            bi != block_map.end(); bi++)
        {
            BLOCK * block = bi->second;
            const BLOCK_KEY & key = bi->first;
            
            if (key.Contains(endMarker))
            {
                markerCount += block->GlobalBlockCount(tid);
            }
            
            if ( !profiles[tid]->first || KnobEmitFirstSlice )
                block->EmitSliceEnd(tid, profiles[tid]);
        }

        if ( !profiles[tid]->first || KnobEmitFirstSlice )
            profiles[tid]->BbFile << endl;

        if (_ldv_type != LDV_TYPE_NONE )
        {
            if ( !profiles[tid]->first || KnobEmitFirstSlice )
            {
                profiles[tid]->LdvFile << "T" ;
                profiles[tid]->EmitLDV();
                profiles[tid]->LdvFile << endl;
            }
        }

        if ( profiles[tid]->active  )
        {
            if (KnobNoSymbolic)
            {
                profiles[tid]->BbFile << "M: " << hex << endMarker 
                    << " " << dec << markerCount << endl;
            }
            else
            {
                EmitSliceStartInfo(endMarker, markerCount, imgId, tid);
            }
        }

        profiles[tid]->BbFile.flush(); 
        profiles[tid]->first = false;            
    }
    
    static int GetFirstIP_If(THREADID tid, ISIMPOINT *isimpoint)
    {
        return !isimpoint->profiles[tid]->first_eip;
    }
    
    static VOID GetFirstIP_Then(VOID * ip, THREADID tid, ISIMPOINT *isimpoint)
    {
        isimpoint->profiles[tid]->first_eip = reinterpret_cast<ADDRINT>(ip);
        PIN_RemoveInstrumentation();        
    }
    
    static int CountBlock_If(BLOCK * block, THREADID tid, ISIMPOINT *isimpoint)
    {
        block->Execute(tid);
        
        isimpoint->profiles[tid]->SliceTimer -= block->StaticInstructionCount();
        isimpoint->profiles[tid]->last_block = block;
        
        return(isimpoint->profiles[tid]->SliceTimer < (INT64)0);
    }

    static int CountBlockAndTrackPrevious_If(BLOCK * block, THREADID tid, 
        ISIMPOINT *isimpoint)
    {
        block->Execute(tid, isimpoint->profiles[tid]->last_block, isimpoint);
        
        isimpoint->profiles[tid]->SliceTimer -= block->StaticInstructionCount();
        isimpoint->profiles[tid]->last_block = block;
        
        return(isimpoint->profiles[tid]->SliceTimer < 0);
    }    
    
    static VOID CountBlock_Then(BLOCK * block, THREADID tid, 
        ISIMPOINT *isimpoint)
    {
        if(isimpoint->profiles[tid]->length_queue.size())
        {
            isimpoint->profiles[tid]->GlobalInstructionCount += 
            (isimpoint->profiles[tid]->CurrentSliceSize - 
                isimpoint->profiles[tid]->SliceTimer);
            isimpoint->profiles[tid]->CurrentSliceSize = 
                isimpoint->profiles[tid]->SliceTimer = 
                    (INT64) isimpoint->profiles[tid]->length_queue.front(); 
            isimpoint->profiles[tid]->length_queue.pop(); 
            // cerr << "Next SliceTimer " 
              //    << isimpoint->profiles[tid]->SliceTimer << endl;
        }
        else
        {
            isimpoint->profiles[tid]->GlobalInstructionCount += 
                (isimpoint->profiles[tid]->CurrentSliceSize - 
                    isimpoint->profiles[tid]->SliceTimer);
            isimpoint->profiles[tid]->CurrentSliceSize = 
                isimpoint->profiles[tid]->SliceTimer = 
                    isimpoint->KnobSliceSize;
        }
        isimpoint->EmitSliceEnd(block->Key().End(), block->ImgId(), tid);
    }

    // Lookup a block by its id.
    // Return block_map.end() if not found.
    BLOCK_MAP::const_iterator LookupBlock(INT32 id) const {
        BLOCK_MAP::const_iterator bi = block_map.begin();
        for (; bi != block_map.end(); bi++)
        {
            if (bi->second->Id() == id)
                return bi;
        }
        return bi;
    }

    // Lookup a block by its BBL key.
    // Create a new one and return it if it doesn't already exist.
    BLOCK * LookupBlock(BBL bbl)
    {
        BLOCK_KEY key(INS_Address(BBL_InsHead(bbl)), 
            INS_Address(BBL_InsTail(bbl)), BBL_Size(bbl));
        BLOCK_MAP::const_iterator bi = block_map.find(key);
        
        if (bi == block_map.end())
        {
            // Block not there, add it
            RTN rtn = INS_Rtn(BBL_InsHead(bbl));
            SEC sec = SEC_Invalid();
            IMG img = IMG_Invalid();
            if(RTN_Valid(rtn))
                sec = RTN_Sec(rtn);
            if(SEC_Valid(sec))
                img = SEC_Img(sec);

            BLOCK * block;
            if ( KnobEmitPrevBlockCounts )
            {
                block = new BLOCK(key, BBL_NumIns(bbl), 0,
                    img_manager.FindImgInfoId(img));
            }
            else
            {
                block = new BLOCK(key, BBL_NumIns(bbl), _currentId[0],
                    img_manager.FindImgInfoId(img));
                _currentId[0]++;
            }
            block_map.insert(BLOCK_PAIR(key, block));
            
            return block;
        }
        else
        {
            return bi->second;
        }
    }
    
    static VOID CountMemory(ADDRINT address, THREADID tid, ISIMPOINT *isimpoint)
    {
        isimpoint->profiles[tid]->ExecuteMemory(address);
    }

    BOOL DoInsertGetFirstIpInstrumentation()
    {
        UINT32 i;
        BOOL do_instrument = false;
        
        for ( i = 0; i < PIN_MAX_THREADS; i++ )
        {
            //cerr << " " << profiles[i]->active;
            if ( profiles[i]->active )
            {
                do_instrument |= !profiles[i]->first_eip;
                //cerr << ":" << !profiles[i]->first_eip;
            }
        }
        //cerr << " -> " << do_instrument << endl;    
        return do_instrument;
    }
    
    static VOID Trace(TRACE trace, VOID *v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        
        for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl);
            bbl = BBL_Next(bbl))
        {
            // find the block in the map or add it if new.
            BLOCK * block = isimpoint->LookupBlock(bbl);
            
            // insert insturmentation to get the first IP. Every thread
            // will call PIN_RemoveInstrumentation upon creation. This
            // ensures that the thread will insert instrumentation to log
            // the first eip. Once the first eip is logged,
            // PIN_RemoveInstrumentation is called again to remove the
            // instrumentation again.
            if ( isimpoint->KnobEmitFirstSlice &&
                isimpoint->DoInsertGetFirstIpInstrumentation() )
            {
                INS_InsertIfCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                    (AFUNPTR)GetFirstIP_If, IARG_THREAD_ID, IARG_PTR,
                    isimpoint, IARG_END);
                INS_InsertThenCall(BBL_InsHead(bbl), IPOINT_BEFORE,
                     (AFUNPTR)GetFirstIP_Then, IARG_INST_PTR, IARG_THREAD_ID,
                     IARG_PTR, isimpoint, IARG_END);
            }

            if ( isimpoint->KnobEmitPrevBlockCounts )
            {
                INS_InsertIfCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlockAndTrackPrevious_If, IARG_PTR, block,
                     IARG_THREAD_ID, IARG_PTR, isimpoint, IARG_END);
            }
            else
            {
                INS_InsertIfCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlock_If, IARG_PTR, block, IARG_THREAD_ID,
                     IARG_PTR, isimpoint, IARG_END);
            }
            INS_InsertThenCall(BBL_InsTail(bbl), IPOINT_BEFORE,
                     (AFUNPTR)CountBlock_Then, IARG_PTR, block,
                     IARG_THREAD_ID, IARG_PTR, isimpoint, IARG_END);

            ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
            if (isimpoint->_ldv_type != LDV_TYPE_NONE )
            {
                for(INS ins = BBL_InsHead(bbl); ; ins = INS_Next(ins))
                {
                   if (INS_IsMemoryRead(ins) || INS_IsMemoryWrite(ins))
                   {
                      for (UINT32 i = 0; i < INS_MemoryOperandCount(ins);
                        i++)
                         INS_InsertCall(ins, IPOINT_BEFORE,
                            (AFUNPTR)CountMemory, IARG_MEMORYOP_EA, i,
                            IARG_THREAD_ID, IARG_PTR, isimpoint, IARG_END);
                   }

                   if (ins == BBL_InsTail(bbl))
                      break;
                }
            }
        }
    }
    
    static VOID Image(IMG img, VOID * v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        
        isimpoint->profiles[0]->OpenFile(0, isimpoint->Pid,
                     isimpoint->KnobOutputFile.Value(), 
                        isimpoint->_ldv_type != LDV_TYPE_NONE);
        isimpoint->img_manager.AddImage(img);
        isimpoint->profiles[0]->BbFile << "G: " << IMG_Name(img)
                     << " LowAddress: " << hex  << IMG_LowAddress(img)
                     << " LoadOffset: " << hex << IMG_LoadOffset(img) << endl;
    }


    static VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        
        ASSERTX(tid < PIN_MAX_THREADS);
        isimpoint->profiles[tid]->OpenFile(tid, isimpoint->Pid,
                     isimpoint->KnobOutputFile.Value(),
                        isimpoint->_ldv_type != LDV_TYPE_NONE);
        isimpoint->profiles[tid]->active = true;
        PIN_RemoveInstrumentation();        
    }
    
    static VOID ThreadFini(UINT32 tid, const CONTEXT *ctxt, INT32 code, VOID *v)
    {
        ISIMPOINT * isimpoint = reinterpret_cast<ISIMPOINT *>(v);
        
        if ( isimpoint->KnobEmitLastSlice &&
            isimpoint->profiles[tid]->SliceTimer != 
                isimpoint->profiles[tid]->CurrentSliceSize )
        {
            isimpoint->CountBlock_Then(isimpoint->profiles[tid]->last_block,
                 tid, isimpoint);
        }
        isimpoint->profiles[tid]->active = false;    
        isimpoint->EmitProgramEnd(tid, isimpoint);
        isimpoint->profiles[tid]->BbFile << "End of bb" << endl;
        isimpoint->profiles[tid]->BbFile.close();
    }
    
    
    VOID GetCommand(int argc, char *argv[])
    {
        for (INT32 i = 0; i < argc; i++)
        {
            commandLine += " ";
            commandLine += argv[i];
        }
    }

    VOID activate(int argc, char** argv) {
        if (isimpoint_knob)
        {
            if (KnobLDVType.Value() == "none")
                _ldv_type = LDV_TYPE_NONE;
            else if (KnobLDVType.Value() == "approx")
                _ldv_type = LDV_TYPE_APPROXIMATE;
            else if (KnobLDVType.Value() == "exact")
                _ldv_type = LDV_TYPE_EXACT;
            else
                ASSERT(0,"Invalid ldv_type: "+KnobLDVType.Value());
            AddInstrumentation(argc, argv);
        }
    }

    BOOL ParseFilenameTid(const string& str, string *fn, UINT32 *tidp)
    {
        size_t tidpos = str.find(":tid");
        if(tidpos == string::npos) return FALSE;
        string tidstr = str.substr(tidpos+4);
        *fn=str.substr(0, tidpos);
        *tidp = Uint32FromString(tidstr);
        return TRUE;
    }
    VOID AddInstrumentation(int argc, char *argv[])
    {
        GetCommand(argc, argv);
        
        //maxThread = MaxThreadsKnob.ValueInt64();
        profiles = new PROFILE* [PIN_MAX_THREADS];
        memset(profiles, 0, PIN_MAX_THREADS * sizeof(profiles[0]));
        
        if (KnobPid)
        {
            Pid = getpid();
        }
        
        PIN_AddThreadStartFunction(ThreadStart, this);
        PIN_AddThreadFiniFunction(ThreadFini, this);
        
        for (THREADID tid = 0; tid < PIN_MAX_THREADS; tid++)
        {
            profiles[tid] = new PROFILE(KnobSliceSize, _ldv_type);
        }

        UINT32 num_length_files = KnobLengthFile.NumberOfValues();
        ASSERTX(num_length_files < PIN_MAX_THREADS);
        for (UINT32 i = 0; i < num_length_files; i++)
        {
                string val = KnobLengthFile.Value(i);
                string fn;
                UINT32 tid;
                BOOL tidfound  = ParseFilenameTid(val, &fn, &tid);
                if ( !tidfound)
                {
                    // skipping 'tidN' suffix ok only if one -lengthfile is
                    // provided and then tid 0 is assumed.
                    ASSERT(num_length_files==1, 
                        "missing 'tidN' suffix to lengthfile:"+val);
                    tid = 0;
                }
                profiles[tid]->ReadLengthFile((THREADID)tid, fn);
        }
        
#if defined(TARGET_MAC)
        // On Mac, ImageLoad() works only after we call PIN_InitSymbols().
        PIN_InitSymbols();
#endif
        
        TRACE_AddInstrumentFunction(Trace, this);
        IMG_AddInstrumentFunction(Image, this);    
    }
    
    VOID EmitProgramEnd(THREADID tid, const ISIMPOINT * isimpoint)
    {
        profiles[tid]->BbFile << "Dynamic instruction count "
             << dec << profiles[tid]->GlobalInstructionCount << endl;
        profiles[tid]->BbFile << "SliceSize: " << dec << KnobSliceSize << endl;
        if ( KnobEmitPrevBlockCounts )
        {
            // Emit blocks in the order that they were first executed.
            for (UINT32 id = 1; id < _currentId[tid]; id++) {
                BLOCK_MAP::const_iterator bi = LookupBlock(id);
                if (bi != block_map.end())
                    bi->second->EmitProgramEnd(bi->first, tid, profiles[tid],
                        isimpoint);
            }
        }
        else
        {
            for (BLOCK_MAP::const_iterator bi = block_map.begin();
                 bi != block_map.end(); bi++)
            {
                bi->second->EmitProgramEnd(bi->first, tid, profiles[tid],
                     isimpoint);
            }
        }
    }

    // read-only accessor.
    THREADID getCurrentId(THREADID tid) const {
        //ASSERTX(tid < PIN_MAX_THREADS);
        return _currentId[tid];
    }

    // increment _currentId and return incremented value.
    THREADID getNextCurrentId(THREADID tid) {
        ASSERTX(tid < PIN_MAX_THREADS);
        ASSERTX(KnobEmitPrevBlockCounts);
        return _currentId[tid]++;
    }

    KNOB_COMMENT knob_family;
    KNOB<BOOL> isimpoint_knob;
    KNOB<string> KnobOutputFile;
    KNOB<INT64>  KnobSliceSize;
    KNOB<BOOL>  KnobNoSymbolic;
    KNOB<BOOL>  KnobEmitFirstSlice;
    KNOB<BOOL>  KnobEmitLastSlice;
    KNOB<BOOL>  KnobEmitPrevBlockCounts;
    KNOB<BOOL>  KnobPid;
    KNOB<string> KnobLDVType;
    KNOB<string> KnobLengthFile;
    LDV_TYPE _ldv_type;
};

VOID BLOCK::Execute(THREADID tid, const BLOCK* prev_block, ISIMPOINT *isimpoint)
{
    _sliceBlockCount[tid]++;
    if (_id == 0)
        _id = isimpoint->getNextCurrentId(tid);

    // Keep track of previous blocks and their counts only if we 
    // will be outputting them later.
    if (isimpoint->KnobEmitPrevBlockCounts) {

        // The block "previous to" the first block is denoted by
        // the special ID zero (0).
        // It should always have a count of one (1).
        UINT32 prevBlockId = prev_block ? prev_block->_id : 0;

        // Automagically add hash keys for this tid and prevBlockID 
        // as needed and increment the counter.
        _blockCountMap[tid][prevBlockId]++;
    }
}

VOID BLOCK::EmitSliceEnd(THREADID tid, PROFILE *profile)
{
    if (_sliceBlockCount[tid] == 0)
        return;
    
    profile->BbFile << ":" << dec << Id() << ":" << dec 
        << SliceInstructionCount(tid) << " ";
    _globalBlockCount[tid] += _sliceBlockCount[tid];
    _sliceBlockCount[tid] = 0;
}


BOOL operator<(const BLOCK_KEY & p1, const BLOCK_KEY & p2)
{
    if (p1.IsPoint())
        return p1._start < p2._start;

    if (p2.IsPoint())
        return p1._end <= p2._start;
    
    if (p1._start == p2._start)
        return p1._end < p2._end;
    
    return p1._start < p2._start;
}

BOOL BLOCK_KEY::Contains(ADDRINT address) const
{
    if (address >= _start && address <= _end)
        return true;
    else
        return false;
}

/* ===================================================================== */
BLOCK::BLOCK(const BLOCK_KEY & key, INT32 instructionCount, INT32 id,
     INT32 imgId)
    :
    _staticInstructionCount(instructionCount),
    _id(id),
    _key(key),
    _imgId(imgId)
{
    for (THREADID tid = 0; tid < PIN_MAX_THREADS; tid++)
    {
        _sliceBlockCount[tid] = 0;
        _globalBlockCount[tid] = 0;
    }
}

VOID BLOCK::EmitProgramEnd(const BLOCK_KEY & key, THREADID tid, 
    PROFILE *profile, const ISIMPOINT *isimpoint) const
{
    if (_globalBlockCount[tid] == 0)
        return;
    
    profile->BbFile << "Block id: " << dec << _id << " " << hex 
        << key.Start() << ":" << key.End() << dec
        << " static instructions: " << _staticInstructionCount
        << " block count: " << _globalBlockCount[tid]
        << " block size: " << key.Size();

    // Output previous blocks and their counts only if enabled.
    // Example: previous-block counts: ( 3:1 5:13 7:3 )
    if (isimpoint->KnobEmitPrevBlockCounts) {
        profile->BbFile << " previous-block counts: ( ";

        // output block-id:block-count pairs.
        for (BLOCK_COUNT_MAP::const_iterator bci = _blockCountMap[tid].begin();
             bci != _blockCountMap[tid].end();
             bci++) {
            profile->BbFile << bci->first << ':' << bci->second << ' ';
        }
        profile->BbFile << ')';
    }
    profile->BbFile << endl;
}

#endif

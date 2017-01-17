/*BEGIN_LEGAL 
BSD License 

Copyright (c) 2015 Intel Corporation. All rights reserved.
 
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

/*
 The LOOP_TRACKER class defined in this file provides functionality for a
 PinPlay tool that tracks some statistics within loops defined by
 inputting a DCFG data file.

 It can be used as a starting point for other tools that
 need to track DCFG basic blocks, edges, and/or loops.
*/

#ifndef LOOP_TRACKER_H
#define LOOP_TRACKER_H

#include "dcfg_pin_api.H"
#include "pinplay.H"

#include <iomanip>
#include <string>

#if !defined (TARGET_WINDOWS)
#  include <sys/cdefs.h>
#  if defined (PIN_CRT)
#    include <unordered_map>
#  else
#    include <tr1/unordered_map>
#  endif 
#else
#  include <unordered_map>
#endif

using namespace std;
using namespace tr1;
using namespace dcfg_api;
using namespace dcfg_pin_api;

// buffer sizes.
#define DCFG_MAX_THREADS 1024
#define DCFG_CACHELINE_SIZE 64

namespace loop_tracker {

    KNOB<string>knobDcfgFileName(KNOB_MODE_WRITEONCE,
                                 "pintool", "loop-tracker:dcfg-file", "",
                                 "Input this DCFG JSON file containing loop definitions"
                                 " and track loop statistics.");
    KNOB<string>knobStatFileName(KNOB_MODE_WRITEONCE,
                                 "pintool", "loop-tracker:loop-stat-file", "loop-stats.csv",
                                 "Write loop statistics to this file.");
    KNOB<string>knobSep(KNOB_MODE_WRITEONCE,
                        "pintool", "loop-tracker:separator", ",",
                        "Separator between columns in loop-stat-file file.");
    KNOB<string>knobTraceLoops(KNOB_MODE_WRITEONCE,
                        "pintool", "loop-tracker:trace-loops", "0",
                        "Trace statements in the given souce-level loops.\n Specify a list of 'filename:linenumber' pairs");
    KNOB<UINT32>knobDebug(KNOB_MODE_WRITEONCE,
                        "pintool", "loop-tracker:debug-level", "0",
                        "Print debug info. Levels: 0 (none), "
                          "1 (summary), 2 (+ loops & instrumentation), 3 (+ analysis).");

    // Maps to keep loop data by ID.
    typedef vector<pair<string,UINT32> > LoopLinenumber;
    typedef unordered_map<DCFG_ID, DCFG_ID_VECTOR> LoopBbsMap;

    struct BbInfo {
        ADDRINT exitAddr;
        UINT64 count;
    };

    struct StatementInfo {
        INT32 lineNumber;
        string fileName;
        ADDRINT startAddr;
        ADDRINT endAddr;
        DCFG_ID bbId;
        INT64   execCount; // For MT support; expensive 
        // INT64   execCount[DCFG_MAX_THREADS]; // For MT support; expensive 
    };
    struct LoopInfo {
        INT32 lineNumber;
        const string *fileName;
        ADDRINT entryAddr;
        BOOL insideLoop;
        struct BbInfo *lastEntrySourceInfo;
        UINT64 entryCounter;
        UINT64 tempEntryCounter;
        struct BbInfo *startEntrySourceInfo;
        UINT64 startCounter; // entryCounter value for the entry with the largest number of iterations
        UINT64 endCounter; // entryCounter value when the largest number of iterations were done
          // (endCounter - startCounter) == the largest number of iterations
          // on any entry
    };

    typedef vector < struct StatementInfo * > StatementsVector;
    typedef unordered_map<DCFG_ID, StatementsVector > BbStatementsMap;
    typedef unordered_map<DCFG_ID, struct LoopInfo * > LoopInfoMap;
    typedef unordered_map<DCFG_ID, struct BbInfo * > BbInfoMap;

    class LOOP_TRACKER {

        // Highest thread id seen during runtime.
        UINT32 highestThreadId;

        // Data from DCFG.
        DCFG_DATA* dcfg;

        // Current process in DCFG.
        DCFG_PROCESS_CPTR curProc;

        // First BB executed.
        DCFG_BASIC_BLOCK_CPTR firstBb;

        // Currently active DCFG images.
        set<DCFG_ID> activeImageIds;

        LoopLinenumber loopsOfInterest;
        LoopBbsMap loopBbsOfInterest;
        LoopBbsMap loopExitSinkBbsOfInterest;
        LoopBbsMap loopEntrySourceBbsOfInterest;
        vector<DCFG_ID> loopIdsOfInterest;
        BbStatementsMap bbStatementsMap;
        BbInfoMap entrySourceBbInfoMap;
        LoopInfoMap loopInfoMap;

        PINPLAY_ENGINE *pinplayEngine;

    public:
        LOOP_TRACKER() : highestThreadId(0), dcfg(0), curProc(0), firstBb(0) {

            // This assumes 'new' alignment is on a ptr-sized boundary so
            // pointer will not be split across cache lines and each pointer
            // will be on a separate cache line (pad may split cache lines,
            // but that is ok).
        }

        // Return input string or 'unknown' if NULL, quoted.
        string safeStr(const string* str) const {
            return string("\"") + (str ? *str : "unknown") + "\"";
        }

        // Print results.
        void printData() const {

            ofstream os;
            os.open(knobStatFileName.Value().c_str(),
                         ios_base::out);
            if (!os.is_open()) {
                cerr << "Error: cannot open '" << knobStatFileName.Value() <<
                    "' for saving statistics." << endl;
                return;
            }

            string sep = knobSep.Value();

            // output averages to 2 decimal places.
            os << setprecision(2) << fixed;

            // Header.
            os << "loop id" <<
                sep << "source file" <<
                sep << "source line number" << 
                sep << "entry-source-address" << 
                sep << "entry-source-count" << 
                sep << "entry-address" << 
                sep << "total-count" << 
                sep << "start-count" << 
                sep << "end-count" << endl;
            for(vector<DCFG_ID>::const_iterator it=loopIdsOfInterest.begin();
                it != loopIdsOfInterest.end(); it++)
            {
                DCFG_ID loopId = *it;
                LoopBbsMap::const_iterator lbi = loopBbsOfInterest.find(loopId);
                ASSERTX(lbi != loopBbsOfInterest.end());
                DCFG_ID_VECTOR loopBBs = lbi->second;
                BbStatementsMap::const_iterator bsi = bbStatementsMap.find(loopId);
                ASSERTX(bsi != bbStatementsMap.end());
                StatementsVector statements = bsi->second;
                LoopInfoMap::const_iterator lit = loopInfoMap.find(loopId);
                struct LoopInfo * linfo = lit->second;

                os << dec << loopId << sep;
                os << *(linfo->fileName) << sep; 
                os << linfo->lineNumber << sep; 
                if (linfo->startEntrySourceInfo)
                {
                    os << hex << "0x" << linfo->startEntrySourceInfo->exitAddr << sep;
                    os << dec << linfo->startEntrySourceInfo->count << sep;
                }
                else
                {
                    os << "0x*NA*" << sep;
                    os << "*NA*" << sep;
                }
                os << hex << "0x" << linfo->entryAddr << sep; 
                os << dec << linfo->entryCounter << sep; 
                os << dec << linfo->startCounter << sep; 
                os << linfo->endCounter;
                os << endl;

                    os << "startAddr" 
                    << " endAddr" 
                    << "# bbId"
                    << " source file"
                    << ":line number"
                    << " execCount" << endl;
                for(vector<DCFG_ID>::const_iterator bit=loopBBs.begin();
                    bit != loopBBs.end(); bit++)
                {
                    DCFG_ID bbId = *bit;
                    bsi = bbStatementsMap.find(bbId);
                    if(bsi == bbStatementsMap.end()) continue;
                    statements = bsi->second;
                    for(StatementsVector::const_iterator sit = statements.begin();
                        sit != statements.end(); sit++) 
                    {
                        size_t pos = (*sit)->fileName.find_last_of("/");
                        os << "0x" << hex << (*sit)->startAddr << " "; 
                        os << "0x" << hex << (*sit)->endAddr << " "; 
                        os <<  "# bbid " << dec << (*sit)->bbId << " ";
                        os <<  (*sit)->fileName.substr(pos+1) << ":"; 
                        os << dec << (*sit)->lineNumber << " "; 
                        os << dec << (*sit)->execCount; 
                        os << endl;
                    }
                }
            }
            os.close();
        }

        // Parse knobTraceLoops to find loop-ids for source loops of interest.
        void parseLoopsOfInterest() 
        {
            stringstream loopToTraceStream(knobTraceLoops);
            string looplocation;
            while(getline(loopToTraceStream, looplocation, ' '))
            {
                size_t colonpos = looplocation.find(":");
                if ( colonpos == string::npos)
                {
                    cerr << "-loop-tracker:trace-loops : expected argument format is filename:linenumber pairs separated by spaces." << endl;
                    exit(1);
                }
                string filename =  looplocation.substr(0, colonpos);
                string linestr =  looplocation.substr(colonpos+1);
                loopsOfInterest.push_back(make_pair(filename, atoi(linestr.c_str())));
            }
        }

        BOOL isLoopOfInterest(DCFG_ID loopId)
        {
            DCFG_BASIC_BLOCK_CPTR bb = curProc->get_basic_block_info(loopId);
            ASSERTX(bb);
            for(LoopLinenumber::const_iterator it = loopsOfInterest.begin();
                 it != loopsOfInterest.end(); it++) {
                    if((bb->get_source_line_number() == it->second)
                        && (bb->get_source_filename()->find(it->first) != string::npos)) // allowing for full path vs filename matching; not perfect
                    {
                        if (knobDebug.Value() >= 2)
                            cout << "found " << it->first << ":" << it->second  << " loopId " << loopId << endl;
                        return true;
                    }
                 }
            return false;
        }

        // Returns true If instruction at insAddr starts lineNumber or  was never seen
        // before for non-statment starting instructions, mark them as ending
        // the lineNumber for now.
        BOOL InsStartsStatment(DCFG_ID bbId, INT32 lineNumber, string insFileName, ADDRINT insAddr, struct StatementInfo **stInfoPtr)
        {
           for(StatementsVector::iterator it = bbStatementsMap[bbId].begin();
                it != bbStatementsMap[bbId].end(); it++) 
           {
            if ((lineNumber == (*it)->lineNumber)
                 && (insFileName.compare((*it)->fileName) == 0))
                {
                   if (knobDebug.Value() >= 2)
                     cout << "found insAddr " <<  hex << insAddr << " belongs to " << (*it)->fileName << ":" << dec <<  (*it)->lineNumber  << " bbId " << bbId << endl;
                    if ((*it)->startAddr == insAddr) // seen before and starts lineNumber
                    {
                       *stInfoPtr = *it; 
                        return true; 
                    }
                    if((*it)->endAddr < insAddr) (*it)->endAddr = insAddr; 
                                             //non-lineNuber-starting instruction, mark it as line
                                             // ending instruction 
                    return false;
                }
           }
           return true;
        }

        // Find data from DCFG.
        void processDcfg() {
            if (!dcfg)
                return;

            // Processes.
            DCFG_ID_VECTOR processIds;
            dcfg->get_process_ids(processIds);
            if (processIds.size() != 1) {
                cerr << "Error: DCFG file contains " << processIds.size() <<
                    " processes; expected exactly one." << endl;
                exit(1);
            }

            // Get one and only process.
            UINT32 pid = processIds[0];
            curProc = dcfg->get_process_info(pid);
            ASSERTX(curProc);
            if (knobDebug.Value() >= 1)
                cout << "Looking for loop data in process " << pid << "..." << endl;

            parseLoopsOfInterest();

            // process all loops.
            DCFG_ID_VECTOR loopIds;
            curProc->get_loop_ids(loopIds);
            for (size_t li = 0; li < loopIds.size(); li++) {
                DCFG_ID loopId = loopIds[li];
                DCFG_LOOP_CPTR loop = curProc->get_loop_info(loopId);
                ASSERTX(loop);
                ASSERTX(loop->get_loop_id() == loopId);

                if (isLoopOfInterest(loopId))
                {
                    loopIdsOfInterest.push_back(loopId);
                    DCFG_ID_VECTOR loopBbs;
                    DCFG_BASIC_BLOCK_CPTR loopIdData = curProc->get_basic_block_info(loopId);
                    UINT32 count = loop->get_basic_block_ids(loopBbs);
                    if (knobDebug.Value() >= 1)
                        cout << "loopId " << loopId << " #basic blocks " << count << endl;
                    loopBbsOfInterest[loopId] = loopBbs;
                    struct LoopInfo *loopInfo = new (struct LoopInfo);
                    loopInfo->lineNumber = loopIdData->get_source_line_number();
                    loopInfo->fileName = loopIdData->get_source_filename();
                    loopInfo->entryAddr = loopIdData->get_first_instr_addr();
                    loopInfo->insideLoop = FALSE;
                    loopInfo->entryCounter = 0;
                    loopInfo->startCounter = 0;
                    loopInfo->endCounter = 0;

                    // Get all the exiting edges of this loop.
                    DCFG_ID_VECTOR exitEdgeIds;
                    loop->get_exit_edge_ids(exitEdgeIds);
                    if (knobDebug.Value() >= 1)
                        cout << "  Loop " << loopId << " has " << exitEdgeIds.size() <<
                        " exit edge(s):"; 
                    for (size_t ei = 0; ei < exitEdgeIds.size(); ei++) {
                        DCFG_ID exitEdgeId = exitEdgeIds[ei];
                        if (knobDebug.Value() >= 1)
                            cout << "  - " << exitEdgeId;
                        DCFG_EDGE_CPTR exitEdgeData = curProc->get_edge_info(exitEdgeId);
                        DCFG_ID exitEdgeSink = exitEdgeData->get_target_node_id();
        ;
                        loopExitSinkBbsOfInterest[loopId].push_back(exitEdgeSink);
                        if (knobDebug.Value() >= 1)
                            cout << "  sink  " << exitEdgeSink;
                    }
                    if (knobDebug.Value() >= 1)
                         cout << endl;

                    // Get all the entering edges of this loop.
                    DCFG_ID_VECTOR entryEdgeIds;
                    loop->get_entry_edge_ids(entryEdgeIds);
                    if (knobDebug.Value() >= 1)
                        cout << "  Loop " << loopId << " has " << entryEdgeIds.size() <<
                        " entry edge(s):"; 
                    for (size_t ei = 0; ei < entryEdgeIds.size(); ei++) {
                        DCFG_ID entryEdgeId = entryEdgeIds[ei];
                        if (knobDebug.Value() >= 1)
                            cout << "  - " << entryEdgeId;
                        DCFG_EDGE_CPTR entryEdgeData = curProc->get_edge_info(entryEdgeId);
                        DCFG_ID entryEdgeSource = entryEdgeData->get_source_node_id();
                        loopEntrySourceBbsOfInterest[loopId].push_back(entryEdgeSource);
                        if (knobDebug.Value() >= 1)
                            cout << "  source  " << entryEdgeSource;
                    }
                    if (knobDebug.Value() >= 1)
                         cout << endl;

                    loopInfoMap[loopId] = loopInfo;
                }
            }

        }

        // Process DCFG and add instrumentation.
        void activate(PINPLAY_ENGINE *pinplay_engine)
        {
            pinplayEngine = pinplay_engine;
            string dcfgFilename = knobDcfgFileName.Value();
            if (dcfgFilename.length() == 0) {
                if (knobDebug.Value() >= 1)
                    cout << "Not tracking loops because no DCFG input file given." << endl;
                return;
            }

            // Make a new DCFG object.
            dcfg = DCFG_DATA::new_dcfg();

            // Read from file.
            if (knobDebug.Value() >= 1)
                cout << "Reading DCFG from '" << dcfgFilename << "'..." << endl;
            string errMsg;
            if (!dcfg->read(dcfgFilename, errMsg)) {
                cerr << "loop-tracker: " << errMsg <<
                    "; use " << knobDcfgFileName.Cmd() << endl;
                exit(1);
            }

            // Get data from DCFG.
            processDcfg();

            // Add Pin instrumentation.
            TRACE_AddInstrumentFunction(handleTrace, this);
            IMG_AddInstrumentFunction(loadImage, this);
            IMG_AddUnloadFunction(unloadImage, this);
            PIN_AddFiniFunction(printStats, this);
        }
        

        ////// Pin analysis and instrumentation routines.

        // Analysis routine for instructions starting a source-level statement
        static VOID 
        enterStatement( 
                ADDRINT insAddr,
                struct StatementInfo *si,
                THREADID tid) {

            if (knobDebug.Value() >= 2)
                cout << "insAddr " << hex << insAddr << "   Entering statement " << si->fileName << dec <<
                    ":" << si->lineNumber << hex << 
                    " startAddr=" << si->startAddr <<
                    " endAddr=" << si->endAddr << endl;
            si->execCount++;
        }

        // Analysis routine for the entry DCFG basic block for a loop
        static VOID 
        enterLoop( 
                ADDRINT insAddr,
                struct LoopInfo *li,
                THREADID tid) {

            li->entryCounter++;
            if (knobDebug.Value() >= 2)
                cout << "insAddr " << hex << insAddr << "   loop entry node" 
                << *(li->fileName) << dec <<
                ":" << li->lineNumber << 
                " entryCount " << dec <<  li->entryCounter << endl;
            if(!li->insideLoop)
            {
                // entering the loop from outside.
                li->tempEntryCounter = li->entryCounter;
                li->insideLoop = TRUE;
            }
        }

        // Analysis routine for the target DCFG basic block for an exit edge
        // for a loop
        static VOID
        enterLoopExitSink( 
                ADDRINT insAddr,
                struct LoopInfo *li,
                THREADID tid) {
            if(li->insideLoop)
            {
                // exited the loop 
                li->insideLoop = FALSE;
                if (knobDebug.Value() >= 1)
                    cout << "insAddr " << hex << insAddr << "   Exiting loop " 
                    << *(li->fileName) << dec <<
                    ":" << li->lineNumber << 
                    " last visit iterations " << dec << (li->entryCounter - li->tempEntryCounter)  << endl;
                if ((li->endCounter - li->startCounter) < (li->entryCounter - li->tempEntryCounter))
                {
                    li->startCounter = li->tempEntryCounter;
                    li->endCounter = li->entryCounter;
                    li->startEntrySourceInfo = li->lastEntrySourceInfo;
                }
            }
        }

        // Analysis routine for the source DCFG basic block for an entry edge
        // for a loop
        static VOID
        enterLoopEntrySource( 
                ADDRINT insAddr,
                struct LoopInfo *li,
                struct BbInfo *bi,
                THREADID tid) {
                // may enter the loop 
                if (knobDebug.Value() >= 1)
                    cout << "insAddr " << hex << insAddr << "   May enter loop " 
                    << *(li->fileName) << dec <<
                    ":" << li->lineNumber << endl; 
                bi->count++;
                li->lastEntrySourceInfo = bi;
        }

        // called when an image is loaded.
        static VOID loadImage(IMG img, VOID *v)
        {
            LOOP_TRACKER *lt = static_cast<LOOP_TRACKER *>(v);
            ASSERTX(lt);

            // Get Pin image data.
            UINT32 imgId = IMG_Id(img);
            if (knobDebug.Value() >= 1)
                cout << "Loading image " << imgId <<
                    ", '" << IMG_Name(img) << "' from " <<
                    (void*)IMG_LowAddress(img) << " to " <<
                    (void*)IMG_HighAddress(img) << "..." << endl;

            // Get corresponding DCFG image data.
            DCFG_IMAGE_CPTR dcfgImage = lt->curProc->get_image_info(imgId);
            if (!dcfgImage) {
                cerr << "Warning: image " << imgId << " is not in DCFG; ignoring." << endl;
                return;
            }

            // Check for consistency.
            if (ADDRINT(dcfgImage->get_base_address()) != IMG_LowAddress(img)) {
                cerr << "Warning: image " << imgId <<
                    " is not at DCFG reported address; ignoring." << endl;
                return;
            }

            // Remember.
            lt->activeImageIds.insert(imgId);
        }

        // called when an image is unloaded.
        static VOID unloadImage(IMG img, VOID *v)
        {
            LOOP_TRACKER *lt = static_cast<LOOP_TRACKER *>(v);
            ASSERTX(lt);
            UINT32 imgid = IMG_Id(img);
            lt->activeImageIds.insert(imgid);
            if (knobDebug.Value() >= 1)
                cout << "Unloading image " << imgid <<
                    ", '" << IMG_Name(img) << "'..." << endl;
            lt->activeImageIds.erase(imgid);
        }

        static BOOL isBbOfInterest(LOOP_TRACKER *lt, DCFG_ID bbId, DCFG_ID *loop_Id)
        {
           for(LoopBbsMap::const_iterator it = lt->loopBbsOfInterest.begin();
                 it != lt->loopBbsOfInterest.end(); it++) {
            for(DCFG_ID_VECTOR::const_iterator iv = it->second.begin();
                 iv != it->second.end(); iv++) {
                   if (*iv == bbId)
                    { 
                        *loop_Id = it->first; 
                        return true;
                    } 
                }
            }
            return false;
        }


        static BOOL isBbExitSink(LOOP_TRACKER *lt, DCFG_ID bbId, DCFG_ID *loop_Id)
        {
           for(LoopBbsMap::const_iterator it = lt->loopExitSinkBbsOfInterest.begin();
                 it != lt->loopExitSinkBbsOfInterest.end(); it++) {
            for(DCFG_ID_VECTOR::const_iterator iv = it->second.begin();
                 iv != it->second.end(); iv++) {
                   if (*iv == bbId)
                    { 
                        *loop_Id = it->first; 
                        return true;
                    } 
                }
            }
            return false;
        }


        static BOOL isBbEntrySource(LOOP_TRACKER *lt, DCFG_ID bbId, DCFG_ID *loop_Id)
        {
           for(LoopBbsMap::const_iterator it = lt->loopEntrySourceBbsOfInterest.begin();
                 it != lt->loopEntrySourceBbsOfInterest.end(); it++) {
            for(DCFG_ID_VECTOR::const_iterator iv = it->second.begin();
                 iv != it->second.end(); iv++) {
                   if (*iv == bbId)
                    { 
                        *loop_Id = it->first; 
                        return true;
                    } 
                }
            }
            return false;
        }

        // Add analysis routines when a trace is delivered.
        static VOID handleTrace(TRACE trace, VOID *v)
        {
            LOOP_TRACKER *lt = static_cast<LOOP_TRACKER *>(v);
            ASSERTX(lt);

            // loop thru all BBLs in this trace.
            for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
            {
                // loop thru all the instrs in this BBL.
                for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins))
                {
                    ADDRINT insAddr = INS_Address(ins);

                    // Get DCFG BBs containing this address.
                    // There will usually be one (or zero if the BB was never executed).
                    // There might be more than one under certain circumstances like
                    // image unload followed by another load.
                    DCFG_ID_VECTOR bbIds;
                    lt->curProc->get_basic_block_ids_by_addr(insAddr, bbIds);
                    for (size_t bbi = 0; bbi < bbIds.size(); bbi++) {
                        DCFG_ID bbId = bbIds[bbi];
                        DCFG_BASIC_BLOCK_CPTR bb = lt->curProc->get_basic_block_info(bbId);
                        ASSERTX(bb);
                        ASSERTX(bb->get_basic_block_id() == bbId);
                        UINT64 bbAddr = bb->get_first_instr_addr();

                        // We only want BBs in active images.
                        DCFG_ID imgId = bb->get_image_id();
                        if (!lt->activeImageIds.count(imgId)) {
                            if (knobDebug.Value() >= 2)
                                cout << "- image " << imgId << " not active" << endl;
                            continue;
                        }

                        DCFG_ID currentLoopId;

                        if (isBbOfInterest(lt, bbId, &currentLoopId)) 
                        {
                            INT32 lineNumber;
                            string insFileName;
                            PIN_GetSourceLocation(insAddr, NULL, &lineNumber, &insFileName);
                            struct StatementInfo *stInfo = NULL;
                            if (lineNumber && lt->InsStartsStatment(bbId, lineNumber, insFileName, insAddr, &stInfo)) 
                            {
                                if (knobDebug.Value() >= 1)
                                    cout << "ins@" << hex << insAddr << " bbId " << dec << bbId << " starts " << insFileName << ":" << lineNumber << endl;
                                if(!stInfo) 
                                {
                                    stInfo = new (struct StatementInfo);
                                    stInfo->lineNumber = lineNumber;
                                    stInfo->fileName = insFileName;
                                    stInfo->startAddr = insAddr;
                                    stInfo->endAddr = insAddr;
                                    stInfo->bbId = bbId;
                                    stInfo->execCount = 0;
                                    lt->bbStatementsMap[bbId].push_back(stInfo);
                                }

                                // Instrument this INS.
                                INS_InsertCall(ins, IPOINT_BEFORE,
                                           (AFUNPTR)enterStatement,
                                           IARG_ADDRINT, insAddr, 
                                           IARG_PTR, stInfo,
                                           IARG_THREAD_ID,
                                           IARG_END);
                            }

                            if((bbId == currentLoopId) && (insAddr == bbAddr))
                            {
                                // first instruction of the loop entry bb
                                if (knobDebug.Value() >= 1)
                                 cout << "ins@" << hex << insAddr << " bbId " << dec << bbId << " entry for loop " << insFileName << ":" << lineNumber << endl;
                                // bb is the loop head
                                INS_InsertCall(ins, IPOINT_BEFORE,
                                    (AFUNPTR)enterLoop,
                                    IARG_ADDRINT, insAddr, 
                                    IARG_PTR, lt->loopInfoMap[currentLoopId],
                                    IARG_THREAD_ID,
                                    IARG_END);
                             }
                        }

                        if ((insAddr == bbAddr) && isBbExitSink(lt, bbId, &currentLoopId)) 
                        {
                            //  ins it the first instruction of bb and
                            // bb is the  sink(target) of a loop exit edge
                            if (knobDebug.Value() >= 1)
                                cout << "ins@" << hex << insAddr << " bbId " << dec << bbId << " exit-sync for loop " << *(lt->loopInfoMap[currentLoopId]->fileName) << ":" << lt->loopInfoMap[currentLoopId]->lineNumber << endl;
                            INS_InsertCall(ins, IPOINT_BEFORE,
                                (AFUNPTR)enterLoopExitSink,
                                IARG_ADDRINT, insAddr, 
                                IARG_PTR, lt->loopInfoMap[currentLoopId],
                                IARG_THREAD_ID,
                                IARG_END);
                        }

                        if (isBbEntrySource(lt, bbId, &currentLoopId) && (insAddr == bb->get_first_instr_addr())) 
                        {
                            //  ins it the first instruction of bb and
                            // bb is the  source of a loop entry edge
                            // Find bbInfo for this bb if exists, allocate
                            // otherwise.
                            struct BbInfo * bbInfo;
                            BbInfoMap::const_iterator bi = lt->entrySourceBbInfoMap.find(bbId);
                            if(bi != lt->entrySourceBbInfoMap.end())
                            {
                                bbInfo = bi->second;
                            }
                            else
                            {
                                bbInfo = new (struct BbInfo);
                                bbInfo->exitAddr = insAddr;
                                bbInfo->count = 0;
                                lt->entrySourceBbInfoMap[bbId] = bbInfo;
                            }
                            if (knobDebug.Value() >= 1)
                                cout << "ins@" << hex << insAddr << " bbId " << dec << bbId << " entry-source for loop " << *(lt->loopInfoMap[currentLoopId]->fileName) << ":" << lt->loopInfoMap[currentLoopId]->lineNumber << endl;
                            INS_InsertCall(ins, IPOINT_BEFORE,
                                (AFUNPTR)enterLoopEntrySource,
                                IARG_ADDRINT, insAddr, 
                                IARG_PTR, lt->loopInfoMap[currentLoopId],
                                IARG_PTR, bbInfo,
                                IARG_THREAD_ID,
                                IARG_END);
                        }
                    }
                } // INS.
            } // BBL.
        }

        // End of program.
        static VOID printStats(int n, VOID *v)
        {
            LOOP_TRACKER *lt = static_cast<LOOP_TRACKER *>(v);
            ASSERTX(lt);

            if (knobDebug.Value() >= 1)
                cout << "End of program." << endl;
            lt->printData();
        }
    };

} // namespace
#endif

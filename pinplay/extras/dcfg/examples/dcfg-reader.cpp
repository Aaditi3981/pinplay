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

// Simple example program to read a DCFG and print some statistics.

#include "dcfg_api.H"
#include "dcfg_trace_api.H"

#include <stdlib.h>
#include <assert.h>
#include <iomanip>
#include <iostream>
#include <string>

using namespace std;
using namespace dcfg_api;
using namespace dcfg_trace_api;

// Class to collect and print some simple statistics.
class Stats {
    UINT64 _count, _sum, _max, _min;

public:
    Stats() : _count(0), _sum(0), _max(0), _min(0) { }

    void addVal(UINT64 val, UINT64 num=1) {
        _sum += val;
        if (!_count || val > _max)
            _max = val;
        if (!_count || val < _min)
            _min = val;
        _count += num;
    }

    UINT64 getCount() const {
        return _count;
    }

    UINT64 getSum() const {
        return _sum;
    }

    float getAve() const {
        return _count ? (float(_sum) / _count) : 0.f;
    }

    void print(int indent, string valueName, string containerName) {
        for (int i = 0; i < indent; i++)
            cout << " ";
        cout << "Num " << valueName << " = " << getSum();
        if (_count)
            cout << ", ave " << valueName << "/" <<
                containerName << " = " << getAve() <<
                " (max = " << _max << ", min = " << _min << ")";
        cout << endl;
    }
};

// Summarize DCFG contents.
void summarizeDcfg(DCFG_DATA_CPTR dcfg) {

    // output averages to 2 decimal places.
    cout << setprecision(2) << fixed;

    cout << "Summary of DCFG:" << endl;

    // processes.
    DCFG_ID_VECTOR proc_ids;
    dcfg->get_process_ids(proc_ids);
    cout << " Num processes           = " << proc_ids.size() << endl;
    for (size_t pi = 0; pi < proc_ids.size(); pi++) {
        DCFG_ID pid = proc_ids[pi];

        // Get info for this process.
        DCFG_PROCESS_CPTR pinfo = dcfg->get_process_info(pid);
        assert(pinfo);
        UINT32 numThreads = pinfo->get_highest_thread_id() + 1;

        cout << " Process " << pid << endl;
        cout << "  Num threads = " << numThreads << endl;
        cout << "  Instr count = " << pinfo->get_instr_count() << endl;
        if (numThreads > 1) {
            for (UINT32 t = 0; t < numThreads; t++)
                cout << "  Instr count on thread " << t <<
                    " = " << pinfo->get_instr_count_for_thread(t) << endl;
        }

        // Edge IDs.
        DCFG_ID_SET edge_ids;
        pinfo->get_internal_edge_ids(edge_ids);
        cout << "  Num edges   = " << edge_ids.size() << endl;

        // Overall stats.
        Stats bbStats, bbSizeStats, bbCountStats, bbInstrCountStats,
            routineStats, routineCallStats, loopStats, loopTripStats;

        // Images.
        DCFG_ID_VECTOR image_ids;
        pinfo->get_image_ids(image_ids);
        cout << "  Num images  = " << image_ids.size() << endl;
        for (size_t ii = 0; ii < image_ids.size(); ii++) {
            DCFG_IMAGE_CPTR iinfo = pinfo->get_image_info(image_ids[ii]);
            assert(iinfo);

            // Basic block, routine and loop IDs for this image.
            DCFG_ID_VECTOR bb_ids, routine_ids, loop_ids;
            iinfo->get_basic_block_ids(bb_ids);
            iinfo->get_routine_ids(routine_ids);
            iinfo->get_loop_ids(loop_ids);

            cout << "  Image " << image_ids[ii] << endl;
            cout << "   Load addr        = 0x" << hex << iinfo->get_base_address() << dec << endl;
            cout << "   Size             = " << iinfo->get_size() << endl;
            cout << "   File             = '" << *iinfo->get_filename() << "'" << endl;
            cout << "   Num basic blocks = " << bb_ids.size() << endl;
            cout << "   Num routines     = " << routine_ids.size() << endl;
            cout << "   Num loops        = " << loop_ids.size() << endl;

            // Basic blocks.
            bbStats.addVal(bb_ids.size());
            for (size_t bi = 0; bi < bb_ids.size(); bi++) {
                if (pinfo->is_special_node(bb_ids[bi]))
                    continue;
                DCFG_BASIC_BLOCK_CPTR bbinfo = pinfo->get_basic_block_info(bb_ids[bi]);
                assert(bbinfo);

                bbSizeStats.addVal(bbinfo->get_num_instrs());
                bbCountStats.addVal(bbinfo->get_exec_count());
                bbInstrCountStats.addVal(bbinfo->get_instr_count(), bbinfo->get_exec_count());
            }
                
            // Routines.
            routineStats.addVal(routine_ids.size());
            for (size_t ri = 0; ri < routine_ids.size(); ri++) {
                DCFG_ROUTINE_CPTR rinfo = iinfo->get_routine_info(routine_ids[ri]);
                assert(rinfo);
                routineCallStats.addVal(rinfo->get_entry_count());
            }

            // Loops.
            loopStats.addVal(loop_ids.size());
            for (size_t li = 0; li < loop_ids.size(); li++) {
                DCFG_LOOP_CPTR linfo = iinfo->get_loop_info(loop_ids[li]);
                assert(linfo);
                loopTripStats.addVal(linfo->get_iteration_count());
            }
        }

        cout << " Process " << pid << " summary:" << endl;
        routineStats.print(2, "routines", "image");
        routineCallStats.print(2, "routine calls", "routine");
        loopStats.print(2, "loops", "image");
        loopTripStats.print(2, "loop iterations", "loop");
        bbStats.print(2, "basic blocks", "image");
        bbSizeStats.print(2, "static instrs", "basic block");
        bbCountStats.print(2, "basic-block executions", "basic block");
        bbInstrCountStats.print(2, "dynamic instrs", "basic block execution");
    }
}

// Summarize DCFG trace contents.
void summarizeTrace(DCFG_DATA_CPTR dcfg, string tracefile) {

    // processes.
    DCFG_ID_VECTOR proc_ids;
    dcfg->get_process_ids(proc_ids);
    for (size_t pi = 0; pi < proc_ids.size(); pi++) {
        DCFG_ID pid = proc_ids[pi];

        // Get info for this process.
        DCFG_PROCESS_CPTR pinfo = dcfg->get_process_info(pid);
        assert(pinfo);

        // Make a new reader.
        DCFG_TRACE_READER* traceReader = DCFG_TRACE_READER::new_reader(pid);

        // threads.
        for (UINT32 tid = 0; tid <= pinfo->get_highest_thread_id(); tid++) {

            // Open file.
            cerr << "Reading DCFG trace for PID " << pid <<
                " and TID " << tid << " from '" << tracefile << "'..." << endl;
            string errMsg;
            if (!traceReader->open(tracefile, tid, errMsg)) {
                cerr << "error: " << errMsg << endl;
                return;
            }

            // Header.
            cout << "edge id,basic-block id,basic-block addr,basic-block symbol,num instrs in BB" << endl;

            // Read until done.
            size_t nRead = 0;
            bool done = false;
            DCFG_ID_VECTOR edge_ids;
            while (!done) {

                if (!traceReader->get_edge_ids(edge_ids, done, errMsg))
                {
                    cerr << "error: " << errMsg << endl;
                    done = true;
                }
                nRead += edge_ids.size();
                for (size_t j = 0; j < edge_ids.size(); j++) {
                    DCFG_ID edgeId = edge_ids[j];

                    // Get edge.
                    DCFG_EDGE_CPTR edge = pinfo->get_edge_info(edgeId);
                    if (!edge) continue;
                    if (edge->is_exit_edge_type()) {
                        cout << edgeId << ",end" << endl;
                        continue;
                    }

                    // Get BB at target.
                    DCFG_ID bbId = edge->get_target_node_id();
                    DCFG_BASIC_BLOCK_CPTR bb = pinfo->get_basic_block_info(bbId);
                    if (!bb) continue;
                    const string* symbol = bb->get_symbol_name();
                    
                    // print info.
                    cout << edgeId << ',' << bbId << ',' <<
                        (void*)bb->get_first_instr_addr() << ',' <<
                        '"' << (symbol ? *symbol : "unknown") << '"' << ',' <<
                        bb->get_num_instrs() << endl;
                }
                edge_ids.clear();
            }
            cerr << "Done reading " << nRead << " edges." << endl;
        }
    }
}

// Print usage and exit.
void usage(const char* cmd) {
    cerr << "This program inputs a DCFG file in JSON format and outputs summary data and statistics." << endl <<
        "It optionally inputs a DCFG-Trace file and outputs a sequence of edges." << endl <<
        "Usage:" << endl <<
        cmd << " <dcfg-file> [<dcfg-trace-file]" << endl;
    exit(1);
}

int main(int argc, char* argv[]) {

    if (argc < 2)
        usage(argv[0]);

    // First argument should be a DCFG file.
    string filename = argv[1];

    // Make a new DCFG object.
    DCFG_DATA* dcfg = DCFG_DATA::new_dcfg();
    
    // Read from file.
    cerr << "Reading DCFG from '" << filename << "'..." << endl;
    string errMsg;
    if (!dcfg->read(filename, errMsg)) {
        cerr << "error: " << errMsg << endl;
        return 1;
    }
    
    // write some summary data from DCFG.
    summarizeDcfg(dcfg);

    // Second argument should be a DCFG-trace file.
    if (argc > 2) {
        string tracefile = argv[2];

        summarizeTrace(dcfg, tracefile);        
    }

    // free memory.
    delete dcfg;

    return 0;
}


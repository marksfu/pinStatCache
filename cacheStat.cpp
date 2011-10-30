//
// This tool counts the number of times a routine is executed and 
// the number of instructions executed in a routine
//

#include <fstream>
#include <iomanip>
#include <stdlib.h>
#include <iostream>
#include <string.h>
#include "pin.H"


FILE * trace;
#define MAX_ADDR 10
#define DEFAULT_CUT_OFF "1000000"
#define DEFAULT_PERIOD  "100000"



VOID* address[MAX_ADDR];
VOID* last_address;
int tracking;
long long readCnt;
long long readStamp[MAX_ADDR];
int unavailable;
int handleCnt;
int max_track;
int cut_off;
int sample; 
int sampleCtr;

typedef struct RtnName
{
    string _name;
    string _image;
    ADDRINT _address;
    RTN _rtn;
    struct RtnName * _next;
} RTN_NAME;

// Linked list of instruction names
RTN_NAME * RtnList = 0;


// Pin command line arguments
KNOB<UINT32>   KnobMaxTrack(KNOB_MODE_WRITEONCE,  "pintool",
               "m", "4", "Maximum number of addresses to track");

KNOB<UINT32>   KnobReadSampleRate(KNOB_MODE_WRITEONCE,  "pintool",
		       "s", DEFAULT_PERIOD, "Average sample rate");

KNOB<BOOL>   KnobRandomSample(KNOB_MODE_WRITEONCE,  "pintool",
			   "r", "1", "Random sample");
		
KNOB<UINT32>   KnobThreshold(KNOB_MODE_WRITEONCE,  "pintool",
			   "t", DEFAULT_CUT_OFF, "Max tracking length");

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,         "pintool",
		               "o", "statCache.out", "specify output file");


const char * StripPath(const char * path) {
    const char * file = strrchr(path,'/');
    if (file)
        return file+1;
    else
        return path;
}


VOID funcBefore(ADDRINT* address) {
	// use 0 to signal function entry
	fprintf(trace, "0 %lu\n",*address);
}

VOID funcAfter(ADDRINT* address) {
	// use 1 to signal function exit
	fprintf(trace, "1 %lu\n",*address);
}
    


// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID *v)
{
    
    // Allocate a counter for this routine
    RtnName * rc = new RTN_NAME;

    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    rc->_name = RTN_Name(rtn);
    rc->_image = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());
    rc->_address = RTN_Address(rtn);
	fprintf(trace, "%lu\t%s\t%s\n", rc->_address, rc->_image.c_str(), rc->_name.c_str());
    // Add to list of routines
    rc->_next = RtnList;
    RtnList = rc;
            
    RTN_Open(rtn);
            
    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)funcBefore, IARG_PTR, &(rc->_address), IARG_END);
	RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)funcAfter, IARG_PTR, &(rc->_address), IARG_END);
    RTN_Close(rtn);
}


void handler() {
	// reset the sample counter
	if (KnobRandomSample) {
		// multiply by two so that the average sample rate stays the same
		sampleCtr = rand() % (2*sample) + 1; 
	} else {
		sampleCtr = sample;
	}
	handleCnt++;

	// check to see if there is an available spot to track the address
	if (!(tracking < max_track)) {
		unavailable++;
		return;
	}

	if (last_address == NULL) {
		return;
	}

	// find the first available free tracking spot
	// available spots are defined by NULL
	for (int i = 0; i < max_track; i++) {
		if (address[i] == NULL) {
			readStamp[i] = readCnt;
			address[i] = last_address;
			tracking++;
			return;
		}
	}
	std::cout << "Error, shouldn't be here\n";
	exit(0);
}

void init() {
	trace = fopen(KnobOutputFile.Value().c_str(), "w");
	max_track = KnobMaxTrack;
	cut_off = KnobThreshold;
	sample = KnobReadSampleRate;
	if (KnobRandomSample) {
		// multiply by two so that the average sample rate stays the same
		sampleCtr = rand() % (2*sample) + 1; 
	} else {
		sampleCtr = sample;
	}
	
	if (max_track > MAX_ADDR) {
		std::cout << "Error, max track needs to be less than " << MAX_ADDR << "\n";
		exit(0);
	}

	for(int i = 0; i < max_track; i++) {
		address[i] = NULL;
		readStamp[i] = 0;
	}
	unavailable = 0;
	handleCnt = 0;
	tracking = 0;
	last_address = NULL;
}

VOID RecordMemRead(VOID * ip, VOID * addr) {
	if (sampleCtr <= 0) {
		handler();
	}
	readCnt++;
	sampleCtr--;
	last_address = addr;
	for (int i = 0; i < max_track; i++) {
		// check to see if address has been reused
		// or if any of the addresses being tracked have exceeded
		// tracking threshold
		if ((address[i] == addr) || ((address[i] != NULL) && (readCnt - readStamp[i]) > cut_off)) {
			address[i] = NULL;
			tracking--;
			fprintf(trace, "%i\n", (int)(readCnt - readStamp[i]));
			break;
		}
	}
}
VOID Instruction(INS ins, VOID *v)
{

    // Instruments memory accesses using a predicated call, i.e.
    // the instrumentation is called iff the instruction will actually be executed.
    //
    // The IA-64 architecture has explicitly predicated instructions. 
    // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
    // prefixed instructions appear as predicated instructions in Pin.
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
}



// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID *v) {
	printf("Unavailable: %i\n", unavailable);
	printf("handle calls: %i\n", handleCnt);
	if (KnobRandomSample) {
		printf("Using random sample\n");
	} else {
		printf("Using regualr samples\n");
	}

}




/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This Pintool computes the memory reuse distance for" << endl;
    cerr << "read instructions. " << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // Initialize symbol table code, needed for rtn instrumentation
    PIN_InitSymbols();

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

	
	// Call our own intializer
	init();
	
    // Register Routine to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);
	INS_AddInstrumentFunction(Instruction, 0);
    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}
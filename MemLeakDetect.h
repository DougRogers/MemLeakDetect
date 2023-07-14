#pragma warning(disable : 4996)
/*************************************************************
Version 1.1
Feb 2011 - Updated by Doug Rogers  (Rogers.Doug@gmail.com)

  Win64 bit support added
  UNICODE support
  Fixed display issue with uninitialized memory



Author		: David A. Jones
File Name	: MemLeakDetect.h
Date		: July 30, 2004
Synopsis	:
A trace memory feature for source code to trace and
find memory related bugs.

Future		:
1) Memory corruption
2) Freeing memory without allocating
3) Freeing memory twice
4) Not Freeing memory at all
5) over running memory boundaries


****************************************************************/
#if !defined(MEMLEAKDETECT_H)
#define MEMLEAKDETECT_H

#ifndef _CRTDBG_MAP_ALLOC
#define _CRTDBG_MAP_ALLOC
#endif
#define CRTDBG_MAP_ALLOC

#include <list>
#include <map>
// #include "tbb/concurrent_hash_map.h"
// #include "tbb/concurrent_map.h"
// #include "tbb/concurrent_queue.h"
// #include "tbb/concurrent_set.h"
// #/include "tbb/concurrent_unordered_set.h"
// #include "tbb/parallel_sort.h"
// #include "tbb/parallel_for_each.h"
#include <tbb/concurrent_unordered_map.h>

#define _CRTBLD

#include <windows.h>

#include "dbgint.h"
#include <ImageHlp.h>
#include <crtdbg.h> // Probably in "Microsoft Visual Studio 9.0\VC\crt\src"

#pragma comment(lib, "imagehlp.lib")

using namespace std;

// if you want to use the custom stackwalker otherwise
// comment this line out
#define MLD_CUSTOMSTACKWALK 1
//

#define MLD_MAX_TRACEINFO      256
#define MLD_TRACEINFO_EMPTY    ("")
#define MLD_TRACEINFO_NOSYMBOL ("?(?)")

#define AfxTrace debugPrint

#include <set>
#include <string>
#include <vector>
#include <mutex>

#define MAX_THREADS 65536

class MemoryLeakDetect
{
public:
    struct AllocBlockInfo
    {
        long requestNumber;
        DWORD size;
        std::string fileName;
        DWORD lineNumber;
        DWORD occurance;
        ULONG stackHash;

        std::vector<ADDRESS> traceinfo;

        bool valid = true;
    };

    typedef tbb::concurrent_unordered_map<ULONG, AllocBlockInfo> AllocationByHash;
    typedef tbb::concurrent_unordered_map<LONG, AllocBlockInfo> AllocationByRequest;

    MemoryLeakDetect();
    ~MemoryLeakDetect();

    class MapMemory
    {
    public:
        inline BOOL lookupByRequest(ULONG requestNumber)
        {
            checkInitialize();
            if (!_mapByRequest->contains(requestNumber))
            {
                return false;
            }

            return (*_mapByRequest)[requestNumber].valid;
        };

        inline void removeKey(LONG requestNumber)
        {
            checkInitialize();
            AllocBlockInfo empty;
            empty.valid = false;

            //_mapByRequest.erase(request);
            checkInitialize();
            (*_mapByRequest)[requestNumber] = empty;
        };

        inline void removeAll()
        {

            if (_mapByRequest)
            {
                _mapByRequest->clear();
            }
        };

        void setAt(LONG request, AllocBlockInfo const &blockInfo)
        {
            checkInitialize();

            (*_mapByRequest)[request] = blockInfo;
        };

        void checkInitialize()
        {
            if (!_mapByRequest)
            {
                _mapByRequest = new AllocationByRequest;
            }
        }

        void initalizeHashTable(int preAllocEntries, BOOL flag)
        {
            preAllocEntries = 0;
            flag            = 0;
        };
        static AllocationByRequest *_mapByRequest;
    };

    static void dumpMemoryTrace();

    static void freeRemainingAllocations();

    static void start();
    static void enable();
    static void disable();

    static void insert(void *p);
    static void remove(void *p);

private:
    static int debugPrint(const char *lpszFormat, ...);

    static void initialize();
    static void shutdown();

    static void pause();
    static void resume();
    static bool isPaused();

    static int _catchMemoryAllocHook(int allocType, void *userData, size_t size, int blockType, long requestNumber, const unsigned char *filename,
                                     int lineNumber);

    static BOOL initSymInfo(char *lpUserPath);
    static BOOL cleanupSymInfo();
    static void symbolPaths(char *lpszSymbolPaths);
    static void symStackTrace(AllocBlockInfo &allocBlockInfo);

    static BOOL symFunctionInfoFromAddresses(DWORD64 fnAddress, char *lpszSymbol);
    static BOOL symSourceInfoFromAddress(DWORD64 address, char *lpszSourceInfo);
    static BOOL symModuleNameFromAddress(DWORD64 address, char *lpszModule);

    static void addMemoryTrace(long requestNumber, DWORD asize, char *fname, DWORD lnum);
    static void redoMemoryTrace(long requestNumber, long prevRequestNumber, DWORD asize, char *fname, DWORD lnum);
    static void removeMemoryTrace(long requestNumber, void *realdataptr);
    static void cleanupMemoryTrace();

    static _CRT_ALLOC_HOOK _prevCrtAllocHookFunction;

    static HANDLE _processHandle;
    static PIMAGEHLP_SYMBOL _symbol;
    static DWORD _symbolBufferSize;

    static uint8_t _pause[MAX_THREADS];
    static bool _started; // don't collect static initializations

    static MapMemory _tracker;
    static DWORD _memoryOccuranceCount;

    static void checkInitialize();

    // valid
    static tbb::concurrent_unordered_map<void *, bool> *_memoryLocations;
};
#endif
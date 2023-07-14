#define MLD_MAX_NAME_LENGTH 1024

#if _DEBUG
#define VC_EXTRALEAN // Exclude rarely-used stuff from Windows headers
//
#pragma warning(disable : 4312)
#pragma warning(disable : 4313)
#pragma warning(disable : 4267)
#pragma warning(disable : 4100)

#include "MemLeakDetect.h"
#include "stdafx.h"
#include <chrono>
#include <mutex>
#include <thread>

#include <assert.h>

#define VERBOSE 0

HANDLE MemoryLeakDetect::_processHandle;
PIMAGEHLP_SYMBOL MemoryLeakDetect::_symbol;
DWORD MemoryLeakDetect::_symbolBufferSize = 0;
uint8_t MemoryLeakDetect::_pause[MAX_THREADS];

bool MemoryLeakDetect::_started = false; // don't collect static initializations

_CRT_ALLOC_HOOK MemoryLeakDetect::_prevCrtAllocHookFunction = nullptr;

MemoryLeakDetect::MapMemory MemoryLeakDetect::_tracker;
DWORD MemoryLeakDetect::_memoryOccuranceCount;

tbb::concurrent_unordered_map<void *, bool> *MemoryLeakDetect::_memoryLocations = nullptr;

MemoryLeakDetect::AllocationByRequest *MemoryLeakDetect::MapMemory::_mapByRequest = nullptr;

MemoryLeakDetect::MemoryLeakDetect()
{
}

MemoryLeakDetect::~MemoryLeakDetect()
{
    shutdown();
}

int MemoryLeakDetect::debugPrint(const char *lpszFormat, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, lpszFormat);
    vsprintf(buffer, lpszFormat, args);

    return _CrtDbgReport(_CRT_WARN, nullptr, 0, nullptr, buffer);
}

void MemoryLeakDetect::initialize()
{
    memset(_pause, 0, sizeof(uint8_t) * MAX_THREADS);
    _symbolBufferSize = (MLD_MAX_NAME_LENGTH + sizeof(PIMAGEHLP_SYMBOL));
    _processHandle    = GetCurrentProcess();
    _symbol           = (PIMAGEHLP_SYMBOL)GlobalAlloc(GMEM_FIXED, _symbolBufferSize);

    _tracker.initalizeHashTable(10211, true);
    initSymInfo(nullptr);

    _prevCrtAllocHookFunction = _CrtSetAllocHook(_catchMemoryAllocHook);
}

void MemoryLeakDetect::start()
{
    _started = true;
    initialize();
}
void MemoryLeakDetect::enable()
{
    _CrtSetAllocHook(_catchMemoryAllocHook);
}
void MemoryLeakDetect::disable()
{
    _CrtSetAllocHook(_prevCrtAllocHookFunction);
}

int MemoryLeakDetect::_catchMemoryAllocHook(int allocType, void *userData, size_t size, int blockType, long requestNumber,
                                            const unsigned char *filename, int lineNumber)
{

    // Internal C library internal allocations
    if (blockType == _CRT_BLOCK)
    {
#if VERBOSE
        AfxTrace("Allocation Request (%d): CRT BLOCK\n", requestNumber);
#endif
        return true;
    }
    // check if someone has turned off mem tracing
    if (((_CRTDBG_ALLOC_MEM_DF & _crtDbgFlag) == 0) && ((allocType == _HOOK_ALLOC) || (allocType == _HOOK_REALLOC)))
    {
        AfxTrace("Allocation Request (%d): TRACE OFF\n", requestNumber);

        if (_prevCrtAllocHookFunction)
        {
            _prevCrtAllocHookFunction(allocType, userData, size, blockType, requestNumber, filename, lineNumber);
        }
        return true;
    }

    // Protect internal mem trace allocs
    if (isPaused())
    {
        // AfxTrace("Allocation Request (%d): INTERNAL\n", requestNumber);
        if (_prevCrtAllocHookFunction)
        {
            _prevCrtAllocHookFunction(allocType, userData, size, blockType, requestNumber, filename, lineNumber);
        }
        return true;
    }

    // lock the function
    pause();

    if (allocType == _HOOK_ALLOC)
    {
        addMemoryTrace(requestNumber, size, (char *)filename, lineNumber);
    }
    else if (allocType == _HOOK_REALLOC)
    {
        if (_CrtIsValidHeapPointer(userData))
        {
            _CrtMemBlockHeader *pCrtHead = pHdr(userData);
            long prevRequestNumber       = pCrtHead->lRequest;
            //
            if (pCrtHead->nBlockUse == _IGNORE_BLOCK && _prevCrtAllocHookFunction)
            {
                AfxTrace("Allocation Request (%d) : _IGNORE_BLOCK\n", requestNumber);
                _prevCrtAllocHookFunction(allocType, userData, size, blockType, requestNumber, filename, lineNumber);
            }
            else
            {
                redoMemoryTrace(requestNumber, prevRequestNumber, size, (char *)filename, lineNumber);
            }
        }
    }
    else if (allocType == _HOOK_FREE)
    {
        if (_CrtIsValidHeapPointer(userData))
        {
            _CrtMemBlockHeader *pCrtHead = pHdr(userData);
            requestNumber                = pCrtHead->lRequest;
            //
            if (pCrtHead->nBlockUse == _IGNORE_BLOCK && _prevCrtAllocHookFunction)
            {
                _prevCrtAllocHookFunction(allocType, userData, size, blockType, requestNumber, filename, lineNumber);
            }
            else
            {
                removeMemoryTrace(requestNumber, userData);
            }
        }
    }

    // unlock the function
    resume();

    return true;
}

void MemoryLeakDetect::addMemoryTrace(long requestNumber, DWORD asize, char *fname, DWORD lnum)
{

    AllocBlockInfo allocBlockInfo;

    //
    allocBlockInfo.requestNumber = requestNumber;
    allocBlockInfo.lineNumber    = lnum;
    allocBlockInfo.size          = asize;
    allocBlockInfo.occurance     = _memoryOccuranceCount++;

    symStackTrace(allocBlockInfo);

#if VERBOSE
    AfxTrace("Allocation Request (%d)\n", requestNumber);
#endif

    if (_tracker.lookupByRequest(requestNumber))
    {
        // already allocated
        AfxTrace("ERROR! addMemoryTrace() Request (%d) already allocated\n", requestNumber);
        return;
    }

    //
    if (fname)
    {
        allocBlockInfo.fileName = fname;
    }

    _tracker.setAt(requestNumber, allocBlockInfo);
}

void MemoryLeakDetect::redoMemoryTrace(long requestNumber, long prevRequestNumber, DWORD asize, char *fname, DWORD lnum)
{

    _tracker.checkInitialize();
    if (_tracker._mapByRequest->contains(prevRequestNumber))
    {
        _tracker.removeKey(prevRequestNumber);
    }
    else
    {
        // this happens when a static variable in initialized using an allocator, then reallocated at run time.
        // AfxTrace(("ERROR! MemLeakDetect::redoMemoryTrace() didnt find request (%d) to free\n"), prevRequestNumber);
    }
    //

    AllocBlockInfo allocBlockInfo;

    allocBlockInfo.requestNumber = requestNumber;
    allocBlockInfo.lineNumber    = lnum;
    allocBlockInfo.size          = asize;
    allocBlockInfo.occurance     = _memoryOccuranceCount++;

    symStackTrace(allocBlockInfo);

    if (fname)
    {
        allocBlockInfo.fileName = fname;
    }

    _tracker.setAt(requestNumber, allocBlockInfo);
};
void MemoryLeakDetect::removeMemoryTrace(long requestNumber, void *realdataptr)
{

    _tracker.checkInitialize();
    if (_tracker._mapByRequest->contains(requestNumber))
    {
        _tracker.removeKey(requestNumber);
    }
    else
    {
        // freeing unallocated memory.  Can happen when we did not intercept the allocation
        // AfxTrace(("ERROR! MemLeakDetect::removeMemoryTrace() didnt find request (%d) to free\n"), requestNumber);
    }
}

void MemoryLeakDetect::cleanupMemoryTrace()
{
    _tracker.removeAll();
}

void MemoryLeakDetect::dumpMemoryTrace()
{
    LPVOID addr;
    char buf[MLD_MAX_NAME_LENGTH];
    char symInfo[MLD_MAX_NAME_LENGTH];
    char srcInfo[MLD_MAX_NAME_LENGTH];
    int totalSize = 0;
    int numLeaks  = 0;

    //
    strcpy(symInfo, MLD_TRACEINFO_NOSYMBOL);
    strcpy(srcInfo, MLD_TRACEINFO_NOSYMBOL);

    AllocationByHash mapByHash;

    // get uniques stack traces by using hash value
    for (auto iter : *_tracker._mapByRequest)
    {
        AllocBlockInfo &ainfo = iter.second;

        if (ainfo.valid)
        {
            mapByHash[ainfo.stackHash] = ainfo;
        }
    }

    for (auto iter : mapByHash)
    {
        AllocBlockInfo &ainfo = iter.second;

        numLeaks++;
        sprintf(buf, "\n\n\n********************************** Memory Leak(%d) ********************\n", numLeaks);
        AfxTrace(buf);

        if (ainfo.fileName.length() > 0)
        {
            sprintf(buf, "Memory Leak: bytes(%d) occurance(%d) %s(%d)\n", ainfo.size, ainfo.occurance, ainfo.fileName.c_str(), ainfo.lineNumber);
        }
        else
        {
            sprintf(buf, "Memory Leak: bytes(%d) occurance(%d)\n", ainfo.size, ainfo.occurance);
        }
        //
        AfxTrace(buf);
        //

        for (auto &address : ainfo.traceinfo)
        {
            // symFunctionInfoFromAddresses(p[0].addrPC.Offset, p[0].addrFrame.Offset, symInfo);
            symFunctionInfoFromAddresses(address.Offset, symInfo);
            symSourceInfoFromAddress(address.Offset, srcInfo);
            AfxTrace("%s->%s()\n", srcInfo, symInfo);
            // p++;
        }
        totalSize += ainfo.size;
    }
    sprintf(buf, ("\n-----------------------------------------------------------\n"));
    AfxTrace(buf);
    if (!totalSize)
    {
        sprintf(buf, ("No Memory Leaks Detected for %d Allocations\n\n"), _memoryOccuranceCount);
        AfxTrace(buf);
    }
    else
    {
        sprintf(buf, ("Total %d Memory Leaks: %d bytes Total Allocations %d\n\n"), numLeaks, totalSize, _memoryOccuranceCount);
        AfxTrace(buf);
    }
}

void MemoryLeakDetect::shutdown()
{
    pause();
    dumpMemoryTrace();
    cleanupMemoryTrace();
    cleanupSymInfo();
    GlobalFree(_symbol);
}

void MemoryLeakDetect::symbolPaths(char *lpszSymbolPath)
{
    char lpszPath[MLD_MAX_NAME_LENGTH];

    // Creating the default path where the dgbhelp.dll is located
    // ".;%_NT_SYMBOL_PATH%;%_NT_ALTERNATE_SYMBOL_PATH%;%SYSTEMROOT%;%SYSTEMROOT%\System32;"
    strcpy(lpszSymbolPath, (".;..\\;..\\..\\"));

    // environment variable _NT_SYMBOL_PATH
    if (GetEnvironmentVariableA(("_NT_SYMBOL_PATH"), lpszPath, MLD_MAX_NAME_LENGTH))
    {
        strcat(lpszSymbolPath, (";"));
        strcat(lpszSymbolPath, lpszPath);
    }

    // environment variable _NT_ALTERNATE_SYMBOL_PATH
    if (GetEnvironmentVariableA(("_NT_ALTERNATE_SYMBOL_PATH"), lpszPath, MLD_MAX_NAME_LENGTH))
    {
        strcat(lpszSymbolPath, (";"));
        strcat(lpszSymbolPath, lpszPath);
    }

    // environment variable SYSTEMROOT
    if (GetEnvironmentVariableA("SYSTEMROOT", lpszPath, MLD_MAX_NAME_LENGTH))
    {
        strcat(lpszSymbolPath, (";"));
        strcat(lpszSymbolPath, lpszPath);
        strcat(lpszSymbolPath, (";"));

        // SYSTEMROOT\System32
        strcat(lpszSymbolPath, lpszPath);
        strcat(lpszSymbolPath, ("\\System32"));
    }
}

BOOL MemoryLeakDetect::cleanupSymInfo()
{
    return SymCleanup(GetCurrentProcess());
}

// Initializes the symbol files
BOOL MemoryLeakDetect::initSymInfo(char *lpszUserSymbolPath)
{
    CHAR lpszSymbolPath[MLD_MAX_NAME_LENGTH];
    DWORD symOptions = SymGetOptions();

    symOptions |= SYMOPT_LOAD_LINES;
    symOptions &= ~SYMOPT_UNDNAME;
    SymSetOptions(symOptions);

    // Get the search path for the symbol files
    symbolPaths(lpszSymbolPath);
    //
    if (lpszUserSymbolPath)
    {
        strcat(lpszSymbolPath, (";"));
        strcat(lpszSymbolPath, lpszUserSymbolPath);
    }
    return SymInitialize(GetCurrentProcess(), lpszSymbolPath, true);
}

void MemoryLeakDetect::symStackTrace(AllocBlockInfo &allocBlockInfo)
{
    // ADDR			FramePtr				= nullptr;
    // ADDR			InstructionPtr			= nullptr;
    // ADDR			OriFramePtr				= nullptr;
    // ADDR			PrevFramePtr			= nullptr;
    // long			StackIndex				= nullptr;

    ULONG FramesToSkip = 3;

    VOID *BackTrace[MLD_MAX_TRACEINFO];

    int nFrames = RtlCaptureStackBackTrace(FramesToSkip, MLD_MAX_TRACEINFO - 1, BackTrace, &allocBlockInfo.stackHash);

    allocBlockInfo.traceinfo.resize(nFrames);
    for (int i = 0; i < nFrames; i++)
    {
        ADDRESS &address = allocBlockInfo.traceinfo[i];

        address.Offset  = (DWORD64)BackTrace[i];
        address.Segment = 0;
        address.Mode    = AddrModeFlat;
    }
}

BOOL MemoryLeakDetect::symFunctionInfoFromAddresses(DWORD64 fnAddress, char *lpszSymbol)
{
    DWORD64 dwDisp = 0;

    ::ZeroMemory(_symbol, _symbolBufferSize);
    _symbol->SizeOfStruct  = _symbolBufferSize;
    _symbol->MaxNameLength = _symbolBufferSize - sizeof(IMAGEHLP_SYMBOL);

    // Set the default to unknown
    strcpy(lpszSymbol, MLD_TRACEINFO_NOSYMBOL);

    // Get symbol info for IP
    if (SymGetSymFromAddr(_processHandle, fnAddress, &dwDisp, _symbol))
    {
        strcpy(lpszSymbol, _symbol->Name);
        return true;
    }
    // create the symbol using the address because we have no symbol
    sprintf(lpszSymbol, "0x%I64X", fnAddress);
    return false;
}

BOOL MemoryLeakDetect::symSourceInfoFromAddress(DWORD64 address, char *lpszSourceInfo)
{
    BOOL ret = false;
    IMAGEHLP_LINE lineInfo;
    DWORD dwDisp;
    char lpModuleInfo[MLD_MAX_NAME_LENGTH] = MLD_TRACEINFO_EMPTY;

    strcpy(lpszSourceInfo, MLD_TRACEINFO_NOSYMBOL);

    memset(&lineInfo, 0, sizeof(IMAGEHLP_LINE));
    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE);

    if (SymGetLineFromAddr(_processHandle, address, &dwDisp, &lineInfo))
    {
        // Using the "sourcefile(linenumber)" format
        sprintf(lpszSourceInfo, ("%s(%d): 0x%I64X"), lineInfo.FileName, lineInfo.LineNumber, address);
        ret = true;
    }
    else
    {
        // Using the "modulename!address" format
        symModuleNameFromAddress(address, lpModuleInfo);

        if (lpModuleInfo[0] == ('?') || lpModuleInfo[0] == ('\0'))
        {
            // Using the "address" format
            sprintf(lpszSourceInfo, ("0x%p 0x%I64X"), lpModuleInfo, address);
        }
        else
        {
            sprintf(lpszSourceInfo, ("%sdll! 0x%I64X"), lpModuleInfo, address);
        }
        ret = false;
    }
    //
    return ret;
}

BOOL MemoryLeakDetect::symModuleNameFromAddress(DWORD64 address, char *lpszModule)
{
    BOOL ret = false;
    IMAGEHLP_MODULE moduleInfo;

    ::ZeroMemory(&moduleInfo, sizeof(IMAGEHLP_MODULE));
    moduleInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE);

    if (SymGetModuleInfo(_processHandle, (DWORD)address, &moduleInfo))
    {
        strcpy(moduleInfo.ModuleName, lpszModule);
        ret = true;
    }
    else
    {
        strcpy(lpszModule, MLD_TRACEINFO_NOSYMBOL);
    }

    return ret;
}
#endif

unsigned char PearsonHashing[] = {
    49,  118, 63,  252, 13,  155, 114, 130, 137, 40,  210, 62,  219, 246, 136, 221, 174, 106, 37,  227, 166, 25,  139, 19,  204, 212, 64,  176, 70,
    11,  170, 58,  146, 24,  123, 77,  184, 248, 108, 251, 43,  171, 12,  141, 126, 41,  95,  142, 167, 46,  178, 235, 30,  75,  45,  208, 110, 230,
    226, 50,  32,  112, 156, 180, 205, 68,  202, 203, 82,  7,   247, 217, 223, 71,  116, 76,  6,   31,  194, 183, 15,  102, 97,  215, 234, 240, 53,
    119, 52,  47,  179, 99,  199, 8,   101, 35,  65,  132, 154, 239, 148, 51,  216, 74,  93,  192, 42,  86,  165, 113, 89,  48,  100, 195, 29,  211,
    169, 38,  57,  214, 127, 117, 59,  39,  209, 88,  1,   134, 92,  163, 0,   66,  237, 22,  164, 200, 85,  9,   190, 129, 111, 172, 231, 14,  181,
    206, 128, 23,  187, 73,  149, 193, 241, 236, 197, 159, 55,  125, 196, 60,  161, 238, 245, 94,  87,  157, 122, 158, 115, 207, 17,  20,  145, 232,
    107, 16,  21,  185, 33,  225, 175, 253, 81,  182, 67,  243, 69,  220, 153, 5,   143, 3,   26,  213, 147, 222, 105, 188, 229, 191, 72,  177, 250,
    135, 152, 121, 218, 44,  120, 140, 138, 28,  84,  186, 198, 131, 54,  2,   56,  78,  173, 151, 83,  27,  255, 144, 249, 189, 104, 4,   168, 98,
    162, 150, 254, 242, 109, 34,  133, 224, 228, 79,  103, 201, 160, 90,  18,  61,  10,  233, 91,  80,  124, 96,  244, 36};

uint8_t digest8(char unsigned const *buf, size_t len)
{
    uint8_t key = len % 256;

    const unsigned char *ptr = (const unsigned char *)buf;
    while (len--)
    {
        unsigned char byte = *ptr++;
        key                = PearsonHashing[(key + byte) % 256];
    }
    return key;
}

uint16_t fasthash16(const void *data, size_t len)
{
    unsigned char const *buf = static_cast<unsigned char const *>(data);

    uint16_t key = 0;

    const unsigned char *ptr = (const unsigned char *)buf;

    while (len--)
    {
        key += digest8(ptr++, len);
    }
    return key;
}

void MemoryLeakDetect::pause()
{
    auto tid     = std::this_thread::get_id();
    uint16_t key = fasthash16(&tid, sizeof(tid));
    _pause[key]  = 1;
}

void MemoryLeakDetect::resume()
{
    auto tid     = std::this_thread::get_id();
    uint16_t key = fasthash16(&tid, sizeof(tid));
    _pause[key]  = 0;
}

bool MemoryLeakDetect::isPaused()
{
    auto tid = std::this_thread::get_id();

    uint16_t key = fasthash16(&tid, sizeof(tid));

    return _pause[key];
}

// extern "C" void g_thread_win32_thread_detach(void);

void MemoryLeakDetect::freeRemainingAllocations()
{
    // g_thread_win32_thread_detach();

    /*for (auto *ptr : _memoryLocations)
    {
        free(ptr);
    }*/

    if (_memoryLocations)
    {
        _memoryLocations->clear();
    }
}

void MemoryLeakDetect::checkInitialize()
{
    if (!_memoryLocations)
    {
        _memoryLocations = new tbb::concurrent_unordered_map<void *, bool>;
    }
}

void MemoryLeakDetect::insert(void *p)
{
    // pause();
    {
        auto tid     = std::this_thread::get_id();
        uint16_t key = fasthash16(&tid, sizeof(tid));

        _pause[key] = 1;
    }
    checkInitialize();

    (*_memoryLocations)[p] = true;

    // resume();
    {
        //_allocationMutex.lock();

        auto tid = std::this_thread::get_id();

        uint16_t key = fasthash16(&tid, sizeof(tid));

        _pause[key] = 0;
    }
}

void MemoryLeakDetect::remove(void *p)
{
    if (_memoryLocations == nullptr)
    {
        return;
    }
    // pause();
    {

        auto tid = std::this_thread::get_id();

        uint16_t key = fasthash16(&tid, sizeof(tid));

        _pause[key] = 1;

        //_allocationMutex.unlock();
    }

    //_memoryLocations.erase(p);
    (*_memoryLocations)[p] = false;

    // resume();
    {
        auto tid     = std::this_thread::get_id();
        uint16_t key = fasthash16(&tid, sizeof(tid));
        _pause[key]  = 0;
    }
}
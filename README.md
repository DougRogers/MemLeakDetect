# MemLeakDetect
Derivative work from Code Project's Memory Leak Detection.

https://www.codeproject.com/Articles/8448/Memory-Leak-Detection

https://www.codeproject.com/Tips/161454/Windows-Memory-leak-detection-update-to-existing-a

MemLeakDetect is a memory leak detector for Visual Studio originally posted in Code Project.  I ported MemLeakDetect to x64 a while ago and have upgraded it over time.  It still is relevant since it is so easy to incorporate into MSVC and Microsoft does not support leak detection in ASan yet.

I have only tested Visual Studio 2022 and x64.  Full stack trace is provided.  You can double-click on lines with file name to navigate to file location.

Example output:
   
********************************** Memory Leak(1) ********************
Memory Leak: bytes(16) occurrence(2)
minkernel\crts\ucrt\src\appcrt\heap\debug_heap.cpp(322): 0x7FF68BAFB580->heap_alloc_dbg_internal()
minkernel\crts\ucrt\src\appcrt\heap\debug_heap.cpp(450): 0x7FF68BAFB43D->heap_alloc_dbg()
minkernel\crts\ucrt\src\appcrt\heap\debug_heap.cpp(496): 0x7FF68BAFF1FF->_malloc_dbg()
minkernel\crts\ucrt\src\appcrt\heap\malloc.cpp(27): 0x7FF68BB04A7E->malloc()
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\heap\new_scalar.cpp(35): 0x7FF68BA84EA3->operator new()
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.36.32532\include\xmemory(78): 0x7FF68BA79034->std::_Default_allocate_traits::_Allocate()
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.36.32532\include\xmemory(235): 0x7FF68BA6FC13->std::_Allocate<16,std::_Default_allocate_traits,0>()
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.36.32532\include\xmemory(952): 0x7FF68BA7A301->std::allocator<std::_Container_proxy>::allocate()
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.36.32532\include\xmemory(1181): 0x7FF68BA6FB7E->std::_Container_base12::_Alloc_proxy<std::allocator<std::_Container_proxy> >()
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.36.32532\include\xstring(2519): 0x7FF68BA75AD0->std::basic_string<char,std::char_traits<char>,std::allocator<char> >::basic_string<char,std::char_traits<char>,std::allocator<char> >()
0x0000000F382FE010 0x7FF68BA80F17->CLeakMem::CLeakMem()
C:\Users\Roger\source\repos\Projects\ExternalProjects\MemLeakDetect\TestMemLeakConsoleApp.cpp(26): 0x7FF68BA80E2B->wmain()
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl(91): 0x7FF68BA86729->invoke_main()
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl(288): 0x7FF68BA865CE->__scrt_common_main_seh()
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl(331): 0x7FF68BA8648E->__scrt_common_main()
D:\a\_work\1\s\src\vctools\crt\vcstartup\src\startup\exe_wmain.cpp(17): 0x7FF68BA867BE->wmainCRTStartup()
0x0000000F382FE010 0x7FFD8C6126AD->BaseThreadInitThunk()
0x0000000F382FE010 0x7FFD8D4AAA68->RtlUserThreadStart()


This project requires tbb.  Microsoft's implementation of concurrent_unordered_map does not have the 'contains' function.
I recommend using vcpkg to install tbb.  https://vcpkg.io/en/



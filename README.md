# MemLeakDetect
Derivative work from Code Project's Memory Leak Detection.

https://www.codeproject.com/Articles/8448/Memory-Leak-Detection

https://www.codeproject.com/Tips/161454/Windows-Memory-leak-detection-update-to-existing-a

MemLeakDetect is a memory leak detector for Visual Studio originally posted in Code Project.  I ported MemLeakDetect to x64 a while ago and have upgraded it over time.  It still is relevant since it is so easy to incorporate into MSVC and Microsoft does not support leak detection in ASan yet.

I have only tested Visual Studio 2022 and x64.  

This project requires tbb.  Microsoft's implementation of concurrent_unordered_map does not have the 'contains' function.



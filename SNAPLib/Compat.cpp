/*++

Module Name:

    compat.cpp

Abstract:

    Functions that provide compatibility between the Windows and Linux versions,
    and mostly that serve to keep #ifdef's out of the main code in order to
    improve readibility.

Authors:

    Bill Bolosky, November, 2011

Environment:

    User mode service.

Revision History:


--*/
#include "stdafx.h"
#include "Compat.h"

using std::min;
using std::max;

#ifdef  _MSC_VER

const void* memmem(const void* data, const size_t dataLength, const void* pattern, const size_t patternLength)
{
    if (dataLength < patternLength) {
        return NULL;
    }
    const void* p = data;
    const char first = *(char*)pattern;
    size_t count = dataLength - patternLength + 1;
    while (count > 0) {
        const void* q = memchr(p, first, count);
        if (q == NULL) {
            return NULL;
        }
        if (0 == memcmp(q, pattern, patternLength)) {
            return q;
        }
        count -= ((char*)q - (char*)p) + 1;
        p = (char*)q + 1;
    }
    return NULL;
}

_int64 timeInMillis()
/**
 * Get the current time in milliseconds since some arbitrary starting point
 * (e.g. system boot or epoch)
 */
{
    return GetTickCount64();
}

_int64 timeInNanos()
{
    static _int64 performanceFrequency = 0;

    if (0 == performanceFrequency) {
        QueryPerformanceFrequency((PLARGE_INTEGER)&performanceFrequency);
    }

    LARGE_INTEGER perfCount;
    QueryPerformanceCounter(&perfCount);
    return perfCount.QuadPart * 1000000000 / performanceFrequency;
}

void AcquireExclusiveLock(ExclusiveLock *lock) {
    EnterCriticalSection(lock);
}

void ReleaseExclusiveLock(ExclusiveLock *lock) {
    LeaveCriticalSection(lock);
}

bool InitializeExclusiveLock(ExclusiveLock *lock) {
    InitializeCriticalSection(lock);
    return true;
}

bool DestroyExclusiveLock(ExclusiveLock *lock) {
    DeleteCriticalSection(lock);
    return true;
}

bool CreateSingleWaiterObject(SingleWaiterObject *newWaiter)
{
    *newWaiter = CreateEvent(NULL,TRUE,FALSE,NULL);
    if (NULL == *newWaiter) {
        return false;
    }
    return true;
}

void DestroySingleWaiterObject(SingleWaiterObject *waiter)
{
    CloseHandle(*waiter);
}

void SignalSingleWaiterObject(SingleWaiterObject *singleWaiterObject) {
    SetEvent(*singleWaiterObject);
}

bool WaitForSingleWaiterObject(SingleWaiterObject *singleWaiterObject) {
    DWORD retVal = WaitForSingleObject(*singleWaiterObject,INFINITE);
    if (WAIT_OBJECT_0 != retVal) {
        return false;
    }
    return true;
}


void BindThreadToProcessor(unsigned processorNumber) // This hard binds a thread to a processor.  You can no-op it at some perf hit.
{
    if (!SetThreadAffinityMask(GetCurrentThread(),1 << processorNumber)) {
        fprintf(stderr,"Binding thread to processor %d failed, %d\n",processorNumber,GetLastError());
    }
}

_uint32 InterlockedIncrementAndReturnNewValue(volatile _uint32 *valueToIncrement)
{
    return InterlockedIncrement((volatile long *)valueToIncrement);
}

int InterlockedDecrementAndReturnNewValue(volatile int *valueToDecrement)
{
    return InterlockedDecrement((volatile long *)valueToDecrement);
}

_uint32 InterlockedCompareExchange32AndReturnOldValue(volatile _uint32 *valueToUpdate, _uint32 replacementValue, _uint32 desiredPreviousValue)
{
    return (_uint32) InterlockedCompareExchange(valueToUpdate, replacementValue, desiredPreviousValue);
}

_uint64 InterlockedCompareExchange64AndReturnOldValue(volatile _uint64 *valueToUpdate, _uint64 replacementValue, _uint64 desiredPreviousValue)
{
    return (_uint64) InterlockedCompareExchange(valueToUpdate, replacementValue, desiredPreviousValue);
}

struct WrapperThreadContext {
    ThreadMainFunction      mainFunction;
    void                    *mainFunctionParameter;
};

    DWORD WINAPI
WrapperThreadMain(PVOID Context)
{
    WrapperThreadContext *context = (WrapperThreadContext *)Context;

    (*context->mainFunction)(context->mainFunctionParameter);
    delete context;
    context = NULL;
    return 0;
}


bool StartNewThread(ThreadMainFunction threadMainFunction, void *threadMainFunctionParameter)
{
    WrapperThreadContext *context = new WrapperThreadContext;
    if (NULL == context) {
        return false;
    }
    context->mainFunction = threadMainFunction;
    context->mainFunctionParameter = threadMainFunctionParameter;

    HANDLE hThread;
    DWORD threadId;
    hThread = CreateThread(NULL,0,WrapperThreadMain,context,0,&threadId);
    if (NULL == hThread) {
        fprintf(stderr,"Create thread failed, %d\n",GetLastError());
        delete context;
        context = NULL;
        return false;
    }

    CloseHandle(hThread);
    hThread = NULL;
    return true;
}
unsigned GetNumberOfProcessors()
{
    SYSTEM_INFO systemInfo[1];
    GetSystemInfo(systemInfo);

    return systemInfo->dwNumberOfProcessors;
}

_int64 QueryFileSize(const char *fileName) {
    HANDLE hFile = CreateFile(fileName,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (INVALID_HANDLE_VALUE == hFile) {
        fprintf(stderr,"Unable to open file for QueryFileSize, %d\n",GetLastError());
        exit(1);
    }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile,&fileSize)) {
        fprintf(stderr,"GetFileSize failed, %d\n",GetLastError());
        exit(1);
    }
    CloseHandle(hFile);

    return fileSize.QuadPart;
}


class LargeFileHandle
{
public:
    HANDLE  handle;
};

    LargeFileHandle*
OpenLargeFile(
    const char* filename,
    const char* mode)
{
    _ASSERT(strlen(mode) == 1 && (*mode == 'r' || *mode == 'w' || *mode == 'a'));
    LargeFileHandle* result = new LargeFileHandle();
    result->handle = CreateFile(filename,
        *mode == 'r' ? GENERIC_READ :
            *mode == 'a' ? FILE_APPEND_DATA
            : GENERIC_WRITE,
        0 /* exclusive */,
        NULL,
        *mode == 'w' ? CREATE_ALWAYS : OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (result->handle == NULL) {
        fprintf(stderr, "open large file %s failed with 0x%x\n", filename, GetLastError());
        delete (void*) result;
        return NULL;
    }
    return result;
}

    size_t
WriteLargeFile(
    LargeFileHandle* file,
    void* buffer,
    size_t bytes)
{
    size_t count = bytes;
    while (count > 0) {
        DWORD step = 0;
        if ((! WriteFile(file->handle, buffer, (DWORD) min(count, (size_t) 0x2000000), &step, NULL)) || step == 0) {
            fprintf(stderr, "WriteLargeFile failed at %lu of %lu bytes with 0x%x\n", bytes - count, bytes, GetLastError());
            return bytes - count;
        }
        count -= step;
        buffer = ((char*) buffer) + step;
    }
    return bytes;
}


    size_t
ReadLargeFile(
    LargeFileHandle* file,
    void* buffer,
    size_t bytes)
{
    size_t count = bytes;
    while (count > 0) {
        DWORD step = 0;
        if ((! ReadFile(file->handle, buffer, (DWORD) min(count, (size_t) 0x1000000), &step, NULL)) || step == 0) {
            fprintf(stderr, "ReadLargeFile failed at %lu of %lu bytes with 0x%x\n", bytes - count, bytes, GetLastError());
            return bytes - count;
        }
        count -= step;
        buffer = ((char*) buffer) + step;
    }
    return bytes;
}

    void
CloseLargeFile(
    LargeFileHandle* file)
{
    if (CloseHandle(file->handle)) {
        delete (void*) file;
    }
}

class MemoryMappedFile
{
public:
    HANDLE  fileHandle;
    HANDLE  fileMapping;
    void*   mappedAddress;
};


    MemoryMappedFile*
OpenMemoryMappedFile(
    const char* filename,
    size_t offset,
    size_t length,
    void** o_contents)
{
    MemoryMappedFile* result = new MemoryMappedFile();
    result->fileHandle = CreateFile(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (result->fileHandle == NULL) {
        printf("unable to open mapped file %s error 0x%x\n", filename, GetLastError());
        delete result;
        return NULL;
    }
    result->fileMapping = CreateFileMapping(result->fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (result->fileMapping == NULL) {
        printf("unable to create file mapping %s error 0x%x\n", filename, GetLastError());
        delete result;
        return NULL;
    }
    *o_contents = MapViewOfFile(result->fileMapping, FILE_MAP_READ, (DWORD) (offset >> (8 * sizeof(DWORD))), (DWORD) offset, length);
    if (*o_contents == NULL) {
        printf("unable to map file %s error 0x%x\n", filename, GetLastError());
        delete result;
        return NULL;
    }
    return result;
}

    void
CloseMemoryMappedFile(
    MemoryMappedFile* mappedFile)
{
    bool ok = UnmapViewOfFile(mappedFile->mappedAddress) &&
        CloseHandle(mappedFile->fileMapping) &&
        CloseHandle(mappedFile->fileHandle);
    if (ok) {
        delete (void*) mappedFile;
    } else {
        printf("unable to close memory mapped file, error 0x%x\n", GetLastError());
    }
}


_int64 InterlockedAdd64AndReturnNewValue(volatile _int64 *valueToWhichToAdd, _int64 amountToAdd)
{
    return InterlockedAdd64((volatile LONGLONG *)valueToWhichToAdd,(LONGLONG)amountToAdd);
}

int _fseek64bit(FILE *stream, _int64 offset, int origin)
{
    return _fseeki64(stream,offset,origin);
}

#else   // _MSC_VER

#if defined(__MACH__)
#include <mach/clock.h>
#include <mach/mach.h>
#endif

_int64 timeInMillis()
/**
 * Get the current time in milliseconds since some arbitrary starting point
 * (e.g. system boot or epoch)
 */
{
    timeval t;
    gettimeofday(&t, NULL);
    return ((_int64) t.tv_sec) * 1000 + ((_int64) t.tv_usec) / 1000;
}

_int64 timeInNanos()
{
    timespec ts;
#if defined(__linux__)
    clock_gettime(CLOCK_REALTIME, &ts); // Works on Linux
#elif defined(__MACH__)
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    ts.tv_sec = mts.tv_sec;
    ts.tv_nsec = mts.tv_nsec;
#else
    #error "Don't know how to get time in nanos on your platform"
#endif
    return ((_int64) ts.tv_sec) * 1000000000 + (_int64) ts.tv_nsec;
}

void AcquireExclusiveLock(ExclusiveLock *lock)
{
    pthread_mutex_lock(lock);
}

void ReleaseExclusiveLock(ExclusiveLock *lock)
{
    pthread_mutex_unlock(lock);
}

bool InitializeExclusiveLock(ExclusiveLock *lock)
{
    return pthread_mutex_init(lock, NULL) == 0;
}

bool DestroyExclusiveLock(ExclusiveLock *lock)
{
    return pthread_mutex_destroy(lock) == 0;
}

class SingleWaiterObjectImpl {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool set;

public:
    bool init() {
        if (pthread_mutex_init(&lock, NULL) != 0) {
            return false;
        }
        if (pthread_cond_init(&cond, NULL) != 0) {
            pthread_mutex_destroy(&lock);
            return false;
        }
        set = false;
        return true;
    }

    void signal() {
        pthread_mutex_lock(&lock);
        set = true;
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&lock);
    }

    void wait() {
        pthread_mutex_lock(&lock);
        while (!set) {
            pthread_cond_wait(&cond, &lock);
        }
        pthread_mutex_unlock(&lock);
    }

    bool destroy() {
        pthread_cond_destroy(&cond);
        pthread_mutex_destroy(&lock);
    }
};

bool CreateSingleWaiterObject(SingleWaiterObject *waiter)
{
    SingleWaiterObjectImpl *obj = new SingleWaiterObjectImpl;
    if (obj == NULL) {
        return false;
    }
    if (!obj->init()) {
        delete obj;
        return false;
    }
    *waiter = obj;
    return true;
}

void DestroySingleWaiterObject(SingleWaiterObject *waiter)
{
    (*waiter)->destroy();
    delete *waiter;
}

void SignalSingleWaiterObject(SingleWaiterObject *waiter)
{
    (*waiter)->signal();
}

bool WaitForSingleWaiterObject(SingleWaiterObject *waiter)
{
    (*waiter)->wait();
    return true;
}

_uint32 InterlockedIncrementAndReturnNewValue(volatile _uint32 *valueToDecrement)
{
    return (_uint32) __sync_fetch_and_add((volatile int*) valueToDecrement, 1) + 1;
}

int InterlockedDecrementAndReturnNewValue(volatile int *valueToDecrement)
{
    return __sync_fetch_and_sub(valueToDecrement, 1) - 1;
}

_int64 InterlockedAdd64AndReturnNewValue(volatile _int64 *valueToWhichToAdd, _int64 amountToAdd)
{
    return __sync_fetch_and_add(valueToWhichToAdd, amountToAdd) + amountToAdd;
}

_uint32 InterlockedCompareExchange32AndReturnOldValue(volatile _uint32 *valueToUpdate, _uint32 replacementValue, _uint32 desiredPreviousValue)
{
  return __sync_val_compare_and_swap(valueToUpdate, desiredPreviousValue, replacementValue);
}

_uint64 InterlockedCompareExchange64AndReturnOldValue(volatile _uint64 *valueToUpdate, _uint64 replacementValue, _uint64 desiredPreviousValue)
{
    fprintf(stderr, "missing implementation for InterlockedCompareExchange64AndReturnNewValue");
    exit(1);
}

namespace {

// POSIX thread functions need to return void*, so we wrap the ThreadMainFunction in our API
struct ThreadInfo {
    ThreadMainFunction function;
    void *parameter;

    ThreadInfo(ThreadMainFunction f, void *p): function(f), parameter(p) {}
};

void* runThread(void* infoVoidPtr) {
    ThreadInfo *info = (ThreadInfo*) infoVoidPtr;
    info->function(info->parameter);
    delete info;
    return NULL;
}

}

bool StartNewThread(ThreadMainFunction threadMainFunction, void *threadMainFunctionParameter)
{
    ThreadInfo *info = new ThreadInfo(threadMainFunction, threadMainFunctionParameter);
    pthread_t thread;
    return pthread_create(&thread, NULL, runThread, info) == 0;
}

void BindThreadToProcessor(unsigned processorNumber)
{
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(processorNumber, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
    }
#endif
}

unsigned GetNumberOfProcessors()
{
    return (unsigned) sysconf(_SC_NPROCESSORS_ONLN);
}

_int64 QueryFileSize(const char *fileName)
{
    int fd = open(fileName, O_RDONLY);
    _ASSERT(fd != -1);
    struct stat sb;
    int r = fstat(fd, &sb);
    _ASSERT(r != -1);
    _int64 fileSize = sb.st_size;
    close(fd);
    return fileSize;
}

class LargeFileHandle
{
public:
    FILE* file;
};

    LargeFileHandle*
OpenLargeFile(
    const char* filename,
    const char* mode)
{
    _ASSERT(strlen(mode) == 1 && (*mode == 'r' || *mode == 'w' || *mode == 'a'));
    char fmode[3];
    fmode[0] = mode[0]; fmode[1] = 'b'; fmode[2] = '\0';
    FILE* file = fopen(filename, fmode);
    if (file == NULL) {
        return NULL;
    }
    LargeFileHandle* result = new LargeFileHandle();
    result->file = file;
    return result;
}

    size_t
WriteLargeFile(
    LargeFileHandle* file,
    void* buffer,
    size_t bytes)
{
    return fwrite(buffer, 1, bytes, file->file);
}


    size_t
ReadLargeFile(
    LargeFileHandle* file,
    void* buffer,
    size_t bytes)
{
    return fread(buffer, 1, bytes, file->file);    
}

    void
CloseLargeFile(
    LargeFileHandle* file)
{
    if (0 == fclose(file->file)) {
        delete (void*) file;
    }
}

class MemoryMappedFile
{
    // todo: implement
};

    MemoryMappedFile*
OpenMemoryMappedFile(
    const char* filename,
    size_t offset,
    size_t length,
    void** o_contents)
{
    printf("not implemented: OpenMemoryMappedFile\n");
    _ASSERT(false);
    *o_contents = NULL;
    return NULL;
}

    void
CloseMemoryMappedFile(
    MemoryMappedFile* mappedFile)
{
    printf("not implemented: CloseMemoryMappedFile\n");
    _ASSERT(false);
}

int _fseek64bit(FILE *stream, _int64 offset, int origin)
{
#ifdef __APPLE__
    // Apple's file pointers are already 64-bit so just use fseeko.
    fseeko(stream, offset, origin);
#else
    return fseeko64(stream, offset, origin);
#endif
}

#endif  // _MSC_VER

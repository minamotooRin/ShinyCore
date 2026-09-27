#include "process_memory.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#elif defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#include <unistd.h>
#endif

ScProcessMemory sc_process_memory() {
    ScProcessMemory result;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof counters)) {
        result.resident=counters.WorkingSetSize;
        result.peak_resident=counters.PeakWorkingSetSize;
    }
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS)
        result.resident=info.resident_size;
    rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0) result.peak_resident=static_cast<std::uint64_t>(usage.ru_maxrss);
#elif defined(__linux__)
    std::uint64_t size{},resident{};
    const auto page=sysconf(_SC_PAGESIZE);
    std::ifstream statm("/proc/self/statm");
    if(statm>>size>>resident && page>0)
        result.resident=resident*static_cast<std::uint64_t>(page);
    rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0) result.peak_resident=static_cast<std::uint64_t>(usage.ru_maxrss)*1024;
#endif
    return result;
}

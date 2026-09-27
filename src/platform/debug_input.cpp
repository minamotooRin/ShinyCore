#include "debug_input.h"
#include <stdexcept>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif
int sc_debug_input(std::span<char> destination) {
#ifdef _WIN32
    const auto handle=GetStdHandle(STD_INPUT_HANDLE);
    const auto type=GetFileType(handle);
    if(type!=FILE_TYPE_PIPE&&type!=FILE_TYPE_DISK) throw std::runtime_error("debug-stdio requires redirected stdin");
    DWORD available=static_cast<DWORD>(destination.size());
    if(type==FILE_TYPE_PIPE) {
        if(!PeekNamedPipe(handle,nullptr,0,nullptr,&available,nullptr)) {
            if(GetLastError()==ERROR_BROKEN_PIPE) return -1;
            throw std::runtime_error("debug stdin polling failed");
        }
        if(!available) return 0;
    }
    DWORD count=0;
    if(!ReadFile(handle,destination.data(),static_cast<DWORD>(std::min(destination.size(),static_cast<std::size_t>(available))),&count,nullptr)) {
        if(GetLastError()==ERROR_BROKEN_PIPE) return -1;
        throw std::runtime_error("debug stdin read failed");
    }
    return count?static_cast<int>(count):-1;
#else
    pollfd input{STDIN_FILENO,POLLIN,0};
    const int ready=poll(&input,1,0);
    if(ready<0) { if(errno==EINTR) return 0; throw std::runtime_error("debug stdin polling failed"); }
    if(!ready) return 0;
    const auto count=read(STDIN_FILENO,destination.data(),destination.size());
    if(count<0) { if(errno==EINTR||errno==EAGAIN) return 0; throw std::runtime_error("debug stdin read failed"); }
    return count?static_cast<int>(count):-1;
#endif
}

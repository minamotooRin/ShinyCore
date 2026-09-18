#include "shiny/platform.h"
#include <array>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>
namespace {
HWND window{};
WNDPROC original{};
std::array<char,4096> composition{};
LRESULT CALLBACK input_proc(HWND handle,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_IME_STARTCOMPOSITION||message==WM_IME_ENDCOMPOSITION||message==WM_KILLFOCUS) composition.fill(0);
    if(message==WM_IME_COMPOSITION&&(lparam&GCS_COMPSTR)) {
        if(auto context=ImmGetContext(handle)) {
            std::array<wchar_t,2048> wide{};
            LONG bytes=ImmGetCompositionStringW(context,GCS_COMPSTR,wide.data(),static_cast<DWORD>((wide.size()-1)*sizeof(wchar_t)));
            composition.fill(0);
            if(bytes>=0&&bytes<static_cast<LONG>(wide.size()*sizeof(wchar_t)))
                WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),bytes/static_cast<int>(sizeof(wchar_t)),composition.data(),static_cast<int>(composition.size()-1),nullptr,nullptr);
            ImmReleaseContext(handle,context);
        }
    }
    // Preserve GLFW's committed Unicode handling and all original window events.
    return CallWindowProcW(original,handle,message,wparam,lparam);
}
}
void sc_platform_open(void* handle) {
    window=static_cast<HWND>(handle); SetLastError(0);
    original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(input_proc)));
    if(!original) { window=nullptr; throw std::runtime_error("cannot attach Windows text input handler"); }
}
void sc_platform_close() noexcept {
    if(window&&original) SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));
    window=nullptr; original=nullptr; composition.fill(0);
}
void sc_platform_input(ScDeviceInput& input) { input.composition=composition; }
void sc_platform_text_position(int x,int y,bool enabled) {
    if(!window||!enabled) return;
    if(auto context=ImmGetContext(window)) {
        COMPOSITIONFORM form{}; form.dwStyle=CFS_POINT; form.ptCurrentPos={x,y}; ImmSetCompositionWindow(context,&form);
        CANDIDATEFORM candidate{}; candidate.dwStyle=CFS_CANDIDATEPOS; candidate.ptCurrentPos={x,y}; ImmSetCandidateWindow(context,&candidate);
        ImmReleaseContext(window,context);
    }
}
#else
void sc_platform_open(void*) {}
void sc_platform_close() noexcept {}
void sc_platform_input(ScDeviceInput&) {}
void sc_platform_text_position(int,int,bool) {}
#endif

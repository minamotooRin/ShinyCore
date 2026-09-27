#include "shiny/platform.h"
#include "ime_text.h"
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
ScDeviceInput preedit;
void clear_preedit() {
    preedit.composition.fill(0); preedit.composition_edit={};
    preedit.composition_segment_count=0; preedit.composition_segments_truncated=false;
}
LRESULT CALLBACK input_proc(HWND handle,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_IME_STARTCOMPOSITION||message==WM_IME_ENDCOMPOSITION||message==WM_KILLFOCUS) clear_preedit();
    if(message==WM_IME_COMPOSITION&&(lparam&(GCS_COMPSTR|GCS_COMPATTR|GCS_COMPCLAUSE|GCS_CURSORPOS|GCS_RESULTSTR))) {
        if(auto context=ImmGetContext(handle)) {
            std::array<char16_t,2048> wide{};
            std::array<unsigned char,2048> attributes{};
            std::array<std::uint32_t,2048> clauses{};
            const LONG bytes=ImmGetCompositionStringW(context,GCS_COMPSTR,wide.data(),static_cast<DWORD>((wide.size()-1)*sizeof(char16_t)));
            const LONG count=ImmGetCompositionStringW(context,GCS_COMPATTR,attributes.data(),static_cast<DWORD>(attributes.size()));
            const LONG clause_bytes=ImmGetCompositionStringW(context,GCS_COMPCLAUSE,clauses.data(),static_cast<DWORD>(sizeof clauses));
            const LONG cursor=ImmGetCompositionStringW(context,GCS_CURSORPOS,nullptr,0);
            clear_preedit();
            if(bytes>=0&&bytes%2==0&&bytes<=4094) {
                const auto size=static_cast<std::size_t>(bytes)/2;
                const auto flags=count>=0&&count<=2048?static_cast<std::size_t>(count):0;
                static_assert(ATTR_INPUT==0&&ATTR_TARGET_CONVERTED==1&&ATTR_CONVERTED==2&&
                    ATTR_TARGET_NOTCONVERTED==3&&ATTR_INPUT_ERROR==4&&ATTR_FIXEDCONVERTED==5);
                const auto clause_count=clause_bytes>=0&&clause_bytes%4==0&&clause_bytes<=static_cast<LONG>(sizeof clauses)
                    ?static_cast<std::size_t>(clause_bytes)/4:0;
                sc_ime_text(preedit,{wide.data(),size},static_cast<int>(cursor),{attributes.data(),flags},{clauses.data(),clause_count});
            }
            ImmReleaseContext(handle,context);
        }
    }
    if(message==WM_IME_COMPOSITION&&lparam==0) clear_preedit();
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
    window=nullptr; original=nullptr; clear_preedit();
}
void sc_platform_input(ScDeviceInput& input) {
    input.composition=preedit.composition; input.composition_edit=preedit.composition_edit;
    input.composition_segments=preedit.composition_segments;
    input.composition_segment_count=preedit.composition_segment_count;
    input.composition_segments_truncated=preedit.composition_segments_truncated;
}
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

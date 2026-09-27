#include "debug_stdio.h"
#include "inspect.h"
#include "lua_inspect.h"
#include "../platform/debug_input.h"
#include "shiny/script.h"
#include <cmath>
#include <lua.hpp>
#include <filesystem>
#include <chrono>
#include <thread>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <csignal>
#endif
namespace {
using V=ScValue;
V text(const char* value) { return V{std::string(value)}; }
V number(std::uint64_t value) { return V{static_cast<double>(value)}; }
void send(V::Object value) { std::cout<<sc_json_write(V{std::move(value)})<<'\n'<<std::flush; }
std::size_t integer(const V& request,const char* name,std::size_t fallback,std::size_t maximum) {
    const auto* value=request.get(name); if(!value) return fallback;
    const double n=value->number(NAN);
    if(!std::isfinite(n)||n<0||n>static_cast<double>(maximum)||std::floor(n)!=n) throw std::runtime_error(std::string("invalid ")+name);
    return static_cast<std::size_t>(n);
}
V preview(const V& value) {
    if(const auto* object=std::get_if<V::Object>(&value.data)) return V{V::Object{{"type",text("object")},{"size",number(object->size())}}};
    if(const auto* array=std::get_if<V::Array>(&value.data)) return V{V::Object{{"type",text("array")},{"size",number(array->size())}}};
    if(const auto* string=std::get_if<std::string>(&value.data);string&&string->size()>512) {
        std::size_t end=512;
        while(end&&((static_cast<unsigned char>((*string)[end])&0xc0)==0x80)) --end;
        return V{V::Object{{"type",text("string")},{"bytes",number(string->size())},{"preview",V{string->substr(0,end)}}}};
    }
    return value;
}
V inspect(const V& root,const V& request) {
    const V* value=&root;
    if(const auto* path=request.get("path")) {
        const auto* parts=std::get_if<V::Array>(&path->data);
        if(!parts||parts->size()>16) throw std::runtime_error("path requires at most 16 keys or zero-based indices");
        for(const auto& part:*parts) {
            if(const auto* key=std::get_if<std::string>(&part.data)) value=value->get(*key);
            else {
                const auto* array=std::get_if<V::Array>(&value->data); const double at=part.number(NAN);
                if(!array||!std::isfinite(at)||at<0||std::floor(at)!=at||at>=static_cast<double>(array->size())) value=nullptr;
                else value=&(*array)[static_cast<std::size_t>(at)];
            }
            if(!value) throw std::runtime_error("inspection path not found");
        }
    }
    const auto offset=integer(request,"offset",0,1048576),limit=integer(request,"limit",64,128);
    if(!limit) throw std::runtime_error("limit must be 1..128");
    V::Array items; std::size_t total=0;
    if(const auto* object=std::get_if<V::Object>(&value->data)) {
        total=object->size(); std::size_t at=0;
        for(const auto& [key,item]:*object) {
            if(at++<offset) continue;
            if(items.size()==limit) break;
            items.emplace_back(V::Object{{"key",preview(V{key})},{"value",preview(item)}});
        }
    } else if(const auto* array=std::get_if<V::Array>(&value->data)) {
        total=array->size();
        for(std::size_t at=offset;at<total&&items.size()<limit;++at)
            items.emplace_back(V::Object{{"key",number(at)},{"value",preview((*array)[at])}});
    } else return V{V::Object{{"value",preview(*value)}}};
    return V{V::Object{{"items",V{std::move(items)}},{"total",number(total)},{"offset",number(offset)}}};
}
int stack_depth(lua_State* L) {
    lua_Debug frame{}; int depth=0;
    while(depth<1024&&lua_getstack(L,depth,&frame)) ++depth;
    return depth;
}

}
ScDebugStdio::ScDebugStdio(bool break_on_entry):break_on_entry_(break_on_entry) {
#ifndef _WIN32
    std::signal(SIGPIPE,SIG_IGN);
#endif
    input_.reserve(16384);
    send({{"event",text("ready")},{"protocol",number(1)},{"paused",V{true}},
        {"loading_breakpoints",V{true}},{"break_on_entry",V{break_on_entry}},
        {"inspection",V{V::Object{{"local_tables",V{true}},{"path_limit",number(SC_DEBUG_PATH_LIMIT)},
                                  {"entry_budget",number(SC_DEBUG_SCAN_LIMIT)}}}},
        {"commands",V{V::Array{text("status"),text("pause"),text("continue"),text("step"),text("entities"),text("state"),text("watches"),text("breakpoints"),text("step_in"),text("step_over"),text("step_out"),text("stack"),text("locals"),text("ui"),text("resources"),text("metrics"),text("panel"),text("quit")}}}});
}
void ScDebugStdio::event(const char* name,std::uint64_t frame) {
    send({{"event",text(name)},{"frame",number(frame)},{"paused",V{paused_}}});
}
void ScDebugStdio::request(std::string_view line,ScScript& script,std::uint64_t frame) {
    V id;
    try {
        auto parsed=sc_json_read(line,16384,20); if(!parsed) throw std::runtime_error(parsed.error());
        const auto* request_id=parsed->get("id");
        if(!request_id) throw std::runtime_error("request id required");
        if(const auto* s=std::get_if<std::string>(&request_id->data);s&&s->size()<=64) id=*request_id;
        else { const double n=request_id->number(NAN);
            if(!std::isfinite(n)||n<0||n>4503599627370495.0||std::floor(n)!=n) throw std::runtime_error("invalid request id");
            id=*request_id;
        }
        const auto* cmd=parsed->get("command"); if(!cmd) throw std::runtime_error("command required");
        const auto name=cmd->text(); V result;
        if(name=="pause") { paused_=true; steps_=0; }
        else if(name=="continue") { paused_=false; steps_=0; line_step_=LineStep::none; }
        else if(name=="step") {
            if(stopped_vm_) throw std::runtime_error("use source stepping or continue while stopped in Lua");
            if(!paused_||steps_) throw std::runtime_error("step requires a paused idle debugger");
            steps_=static_cast<std::uint32_t>(integer(*parsed,"count",1,1000));
            if(!steps_) throw std::runtime_error("step count must be 1..1000");
        } else if(name=="breakpoints") {
            const auto* file=parsed->get("file"); const auto* lines=parsed->get("lines");
            if(!file||!lines) throw std::runtime_error("breakpoints requires file and lines");
            const auto path=file->text(); const auto* array=std::get_if<V::Array>(&lines->data);
            if(path.empty()||path.size()>255||!sc_script_validate_path(path.c_str())||!path.ends_with(".lua")||!array||array->size()>128)
                throw std::runtime_error("invalid breakpoint path or line array (max 128)");
            std::set<int> requested;
            for(const auto& requested_line:*array) {
                const double n=requested_line.number(NAN);
                if(!std::isfinite(n)||n<1||n>1000000||std::floor(n)!=n) throw std::runtime_error("invalid breakpoint line");
                requested.insert(static_cast<int>(n));
            }
            auto canonical=std::filesystem::path(path).lexically_normal().generic_string();
            if(!requested.empty()&&!breakpoints_.contains(canonical)&&breakpoints_.size()==64) throw std::runtime_error("breakpoint file capacity exhausted");
            if(requested.empty()) breakpoints_.erase(canonical); else breakpoints_.insert_or_assign(canonical,std::move(requested));
            result=V{V::Object{{"file",V{canonical}},{"verified",V{false}}}};
        } else if(name=="step_in"||name=="step_over"||name=="step_out") {
            if(!stopped_vm_||!paused_) throw std::runtime_error("source stepping requires a stopped Lua frame");
            step_depth_=stack_depth(stopped_vm_);
            line_step_=name=="step_in"?LineStep::into:name=="step_over"?LineStep::over:LineStep::out;
            paused_=false; steps_=0;
        } else if(name=="stack"||name=="locals") {
            if(!stopped_vm_||!paused_) throw std::runtime_error("stack inspection requires a stopped Lua frame");
            result=sc_debug_stack(script,stopped_vm_,*parsed,name=="locals");
        } else if(name=="ui"||name=="resources"||name=="metrics"||name=="entities") result=sc_debug_inspect(script,name,*parsed);
        else if(name=="panel") {
            if(!panel) throw std::runtime_error("panel requires an initialized graphical host");
            result=panel(*parsed);
        } else if(name=="quit") { exiting_=true; script.world->exit_requested=true; }
        else if(name=="state") result=inspect(script.state,*parsed);
        else if(name=="watches") {
            // Values were explicitly copied by watch(); no Lua evaluation or metamethods.
            result=inspect(V{script.watches},*parsed);
        } else if(name!="status") throw std::runtime_error("unsupported debug command");
        send({{"id",id},{"ok",V{true}},{"frame",number(frame)},{"tick",number(script.world->tick)},
            {"room",text(script.entry)},{"candidate",V{script.candidate}},{"phase",number(static_cast<std::uint64_t>(script.phase))},
            {"paused",V{paused_}},{"pending_steps",number(steps_)},{"result",std::move(result)}});
    } catch(const std::exception& error) {
        send({{"id",id},{"ok",V{false}},{"code",text("debug_request")},{"error",text(error.what())},{"frame",number(frame)}});
    }
}
void ScDebugStdio::attach(ScScript& script,std::uint64_t frame) {
    frame_=frame;
    script.debug_hook=hook; script.debug_context=this;
}
void ScDebugStdio::service(ScScript& script,std::uint64_t frame) {
    if(!polling_hook_) attach(script,frame);
    if(!connected_) return;
    char bytes[4096]; const int count=sc_debug_input(bytes);
    if(count<0) { connected_=false; paused_=true; steps_=0; input_.clear(); event("disconnected",frame); return; }
    for(int i=0;i<count;++i) {
        if(bytes[i]=='\n') {
            if(!discard_) request(input_,script,frame);
            input_.clear(); discard_=false;
        } else if(!discard_) {
            if(input_.size()==16384) {
                input_.clear(); discard_=true;
                send({{"id",V{}},{"ok",V{false}},{"code",text("debug_request")},{"error",text("request exceeds 16384 bytes")}});
            } else input_+=bytes[i];
        }
    }
}
void ScDebugStdio::completed(std::uint64_t frame) { if(steps_&&--steps_==0) event("stopped",frame); }
void ScDebugStdio::finish(std::uint64_t frame) { event("terminated",frame); }

bool ScDebugStdio::hook(void* context,ScScript& script,lua_State* L,lua_Debug* ar) noexcept {
    auto& debugger=*static_cast<ScDebugStdio*>(context);
    try { return debugger.on_hook(script,L,ar); }
    catch(const std::exception& error) { std::snprintf(script.error,sizeof script.error,"debug hook: %s",error.what()); }
    catch(...) { std::snprintf(script.error,sizeof script.error,"debug hook failed"); }
    debugger.stopped_vm_=nullptr;
    debugger.polling_hook_=false;
    return false;
}
bool ScDebugStdio::on_hook(ScScript& script,lua_State* L,lua_Debug* ar) {
    bool hit=false;
    if(ar->event==LUA_HOOKCOUNT) {
        const bool was_running=running();
        polling_hook_=true;
        try { service(script,frame_); } catch(...) { polling_hook_=false; throw; }
        polling_hook_=false;
        hit=was_running&&!running();
    } else if(ar->event==LUA_HOOKRET) {
        // At the outermost callback there is no Lua caller to stop in. Resume
        // stepping at the next callback rather than running forever.
        if(line_step_==LineStep::out&&stack_depth(L)<=step_depth_) line_step_=LineStep::into;
    } else if(ar->event==LUA_HOOKLINE&&(break_on_entry_||!breakpoints_.empty()||line_step_!=LineStep::none)) {
        lua_getinfo(L,"S",ar);
        const auto path=sc_debug_source_path(script,ar->source);
        const auto found=breakpoints_.find(path);
        hit=break_on_entry_||(found!=breakpoints_.end()&&found->second.contains(ar->currentline));
        break_on_entry_=false;
        if(line_step_!=LineStep::none) {
            const int depth=stack_depth(L);
            hit=hit||line_step_==LineStep::into||(line_step_==LineStep::over&&depth<=step_depth_)||(line_step_==LineStep::out&&depth<step_depth_);
        }
    }
    if(hit) {
        paused_=true; steps_=0; line_step_=LineStep::none; stopped_vm_=L;
        lua_getinfo(L,"Sl",ar);
        V::Object stopped{{"event",text("breakpoint")},{"frame",number(frame_)},{"paused",V{true}},
            {"room",text(script.entry)},{"candidate",V{script.candidate}},
            {"phase",number(static_cast<std::uint64_t>(script.phase))}};
        const auto path=sc_debug_source_path(script,ar->source);
        if(!path.empty()) stopped.emplace("file",V{path});
        if(ar->currentline>0) stopped.emplace("line",number(static_cast<uint64_t>(ar->currentline)));
        send(std::move(stopped));
        try {
            while(paused_&&!script.world->exit_requested) {
                polling_hook_=true; service(script,frame_); polling_hook_=false;
                if(pump) pump(script);
                if(paused_) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch(...) { polling_hook_=false; stopped_vm_=nullptr; throw; }
        stopped_vm_=nullptr;
    }
    if(script.world->exit_requested) { exiting_=true; std::snprintf(script.error,sizeof script.error,"debugger requested exit"); return false; }
    return true;
}

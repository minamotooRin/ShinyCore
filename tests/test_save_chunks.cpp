#include "shiny/save.h"
#include "shiny/save_io.h"
#include "shiny/path.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>
#include <utility>

namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct Directory {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("shiny-chunk-save-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { std::filesystem::create_directory(root); }
    ~Directory() { std::error_code error; std::filesystem::remove_all(root,error); }
    std::string slot() const { return (root/"slot.json").string(); }
};
ScValue state(double revision) { return ScValue{ScValue::Object{{"revision",ScValue{revision}}}}; }
ScValue record(double revision) {
    return ScValue{ScValue::Object{{"format",ScValue{double(SC_SAVE_FORMAT)}},{"project",ScValue{std::string("test")}},
        {"data_version",ScValue{1.0}},{"scene",ScValue{std::string("main.lua")}},{"state",state(revision)}}};
}
ScValue read(const std::string& path) {
    auto value=sc_save_read(path,"test",1);
    if(!value) throw std::runtime_error(value.error());
    return std::move(*value);
}
double revision(const ScValue& value) { return value.get("state")->get("revision")->number(); }
double chunk_revision(const std::string& path,const ScValue& snapshot,const char* key) {
    auto value=sc_save_read_chunk(path,snapshot,key);
    check(value&&value->has_value(),"chunk exists in selected snapshot");
    return (**value).get("revision")->number();
}
std::string file_name(const ScValue& value) {
    char hash[17]; std::snprintf(hash,sizeof hash,"%016llx",static_cast<unsigned long long>(sc_data_hash(value)));
    return std::string(hash)+"-0.json";
}
void write_text(const std::filesystem::path& path,const char* text) {
    std::ofstream file(path,std::ios::binary); file<<text; check(bool(file),"fixture write");
}
std::size_t count(const std::string& path) {
    std::size_t result=0;
    if(std::filesystem::exists(path+".chunks")) for(const auto& file:std::filesystem::directory_iterator(path+".chunks"))
        if(file.is_regular_file()) ++result;
    return result;
}
void lifecycle() {
    Directory directory; auto path=directory.slot();
    check(bool(sc_save_write(path,record(1),"test",1,{{"main/forest:-1:0",state(1)},{"main/cave:1:0",state(10)}})),"initial commit");
    auto first=read(path); check(revision(first)==1,"initial state");
    check(chunk_revision(path,first,"main/forest:-1:0")==1,"negative coordinate key");
    auto missing=sc_save_read_chunk(path,first,"absent"); check(missing&&!*missing,"absent chunk distinct from failure");
    check(bool(sc_save_write(path,record(2),"test",1,{{"main/forest:-1:0",state(2)}})),"incremental commit");
    auto second=read(path);
    check(chunk_revision(path,second,"main/cave:1:0")==10,"unchanged chunk retained");
    check(chunk_revision(path,first,"main/forest:-1:0")==1,"previous snapshot still readable");
    check(count(path)==3,"current and previous references retained");
    check(bool(sc_save_write(path,record(3),"test",1)),"plain save retains chunks");
    check(count(path)==2,"unreachable revision collected");
    check(bool(sc_save_write(path,record(4),"test",1,{{"main/cave:1:0",ScValue{}}})),"delete chunk atomically");
    check(count(path)==2,"deleted record retained by backup");
    check(bool(sc_save_write(path,record(5),"test",1)),"next checkpoint");
    check(count(path)==1,"deleted record eventually collected");
    write_text(path+".chunks/user-note.txt","keep");
    check(bool(sc_save_delete(path)),"slot deletion");
    check(!std::filesystem::exists(path)&&!std::filesystem::exists(path+".bak"),"both indices removed");
    check(count(path)==1&&std::filesystem::exists(path+".chunks/user-note.txt"),"deletion leaves unrelated files alone");
}
void interruption() {
    for(int boundary=0;boundary<3;++boundary) {
        Directory directory; auto path=directory.slot();
        check(bool(sc_save_write(path,record(1),"test",1,{{"a",state(1)},{"b",state(11)}})),"failure fixture");
        // Fail after the first new chunk, before backup replacement, or before
        // primary replacement. No production fault hook is needed.
        auto blocked=boundary==0?path+".chunks/"+file_name(state(12))+".tmp":path+(boundary==1?".bak.tmp":".tmp");
        std::filesystem::create_directory(blocked);
        check(!sc_save_write(path,record(2),"test",1,{{"a",state(2)},{"b",state(12)}}),"injected IO failure returned");
        auto restored=read(path);
        check(revision(restored)==1&&chunk_revision(path,restored,"a")==1&&chunk_revision(path,restored,"b")==11,"failure keeps one complete snapshot");
        std::filesystem::remove(blocked);
        check(bool(sc_save_write(path,record(3),"test",1,{{"a",state(3)},{"b",state(13)}})),"retry after failure");
        check(count(path)==4,"retry collects orphan chunks but preserves backup");
    }
}
void corruption() {
    Directory directory; auto path=directory.slot();
    check(bool(sc_save_write(path,record(1),"test",1,{{"a",state(1)},{"b",state(11)}})),"old checkpoint");
    check(bool(sc_save_write(path,record(2),"test",1,{{"a",state(2)},{"b",state(12)}})),"new checkpoint");
    auto selected=read(path);
    write_text(path+".chunks/"+file_name(state(2)),"{\"revision\":9.0}");
    check(!sc_save_read_chunk(path,selected,"a"),"pinned snapshot corruption does not silently read another generation");
    auto recovered=read(path);
    check(revision(recovered)==1&&chunk_revision(path,recovered,"a")==1&&chunk_revision(path,recovered,"b")==11,"corrupt record recovers entire previous checkpoint");
    check(bool(sc_save_write(path,record(3),"test",1,{{"a",state(3)}})),"commit after recovery");
    check(chunk_revision(path,read(path),"b")==11,"recovery does not retain a newer sibling chunk");
    write_text(path,"broken");
    check(revision(read(path))==1,"good backup was not overwritten by corrupt primary");
    write_text(path+".chunks/"+file_name(state(1)),"broken");
    check(!sc_save_read(path,"test",1),"both incomplete snapshots rejected");
    check(!sc_save_write(path,record(4),"test",1),"cannot silently erase unseen chunks of corrupt world");
}
void bounds_and_scale() {
    Directory directory; auto path=directory.slot();
    check(!sc_save_write(path,record(1),"test",1,{{"a",state(1)},{"../escape",state(2)}}),"whole-batch name validation");
    check(!std::filesystem::exists(path)&&count(path)==0,"preflight created nothing");
    auto oversized=ScValue{ScValue::Object{{"text",ScValue{std::string(SC_STATE_BYTES,'x')}}}};
    check(!sc_save_write(path,record(1),"test",1,{{"a",oversized}}),"chunk byte budget");
    ScValue::Object changes;
    for(int i=0;i<1024;++i) changes.emplace("world:"+std::to_string(i),state(i));
    check(bool(sc_save_write(path,record(1),"test",1,changes)),"1024-record world commit");
    auto selected=read(path);
    check(std::get<ScValue::Object>(selected.get("chunks")->data).size()==1024,"full index count");
    for(int i=0;i<1024;i+=31) check(chunk_revision(path,selected,("world:"+std::to_string(i)).c_str())==i,"bounded individual read");
    for(int i=0;i<12;++i) check(bool(sc_save_write(path,record(i+2),"test",1,{{"world:0",state(i+2000)}})),"repeated revisits");
    check(count(path)==1025,"repeated updates do not grow record count");
    auto invalid=record(1); std::get<ScValue::Object>(invalid.data)["format"]=ScValue{2.0};
    check(!sc_save_validate(invalid,"test",1),"old format rejected");
    auto& references=std::get<ScValue::Object>(std::get<ScValue::Object>(selected.data).at("chunks").data);
    references["world:0"]=ScValue{ScValue::Object{{"file",ScValue{std::string("../escape")}}}};
    check(!sc_save_validate(selected,"test",1)&&!sc_save_read_chunk(path,selected,"world:0"),"invalid native reference rejected safely");
}
void immutable_names() {
    Directory directory; auto path=directory.slot();
    std::filesystem::create_directory(path+".chunks");
    write_text(path+".chunks/"+file_name(state(1)),"{\"revision\":9.0}");
    check(bool(sc_save_write(path,record(1),"test",1,{{"a",state(1)},{"b",state(1)}})),"orphan collision handled");
    const auto saved=read(path);
    check(saved.get("chunks")->get("a")->get("file")->text().ends_with("-1.json"),"existing differing content is never overwritten");
    check(count(path)==1&&chunk_revision(path,saved,"a")==1&&chunk_revision(path,saved,"b")==1,"equal records share immutable payload");
}
struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{},released{};
    void wait() {
        std::unique_lock lock(mutex); entered=true; condition.notify_all();
        condition.wait(lock,[&]{return released;});
    }
    void await_entry() {
        std::unique_lock lock(mutex);
        check(condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"writer entered gate");
    }
    void release() { std::lock_guard lock(mutex); released=true; condition.notify_all(); }
};
struct Release { Gate& gate; ~Release() { gate.release(); } };
ScResult<bool> await(ScSaveIo& writer,std::uint64_t request) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;) {
        auto result=writer.advance(request);
        if(!result||*result) return result;
        check(std::chrono::steady_clock::now()<deadline,"save completion timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
template<class F> void rejected(F action,const char* message) {
    bool failed=false; try { action(); } catch(const std::exception&) { failed=true; }
    check(failed,message);
}
void asynchronous() {
    Directory directory; const auto path=directory.slot(); Gate gate;
    check(bool(sc_save_write(path,record(1),"test",1,{{"a",state(1)}})),"initial async checkpoint");
    std::filesystem::create_directory(path+".tmp"); // Actual primary-index commit failure.
    std::thread::id worker; std::atomic_bool throw_once{};
    ScSaveIo writer([&](const ScSaveWriteRequest& request) {
        worker=std::this_thread::get_id(); gate.wait();
        if(throw_once.exchange(false)) throw std::runtime_error("injected writer exception");
        return sc_save_write(request.path,request.record,request.project,request.data_version,request.changes);
    });
    Release cleanup{gate};
    ScSaveWriteRequest request{path,"test",1,record(2),{{"a",state(2)}}};
    const auto id=writer.submit(request); gate.await_entry();
    request.record=record(99); request.changes["a"]=state(99);
    check(writer.active()&&id==1,"one accepted transaction");
    auto pending=writer.advance(id); check(pending&&!*pending,"poll does not wait on disk");
    rejected([&]{ writer.submit(request); },"unreleased transaction prevents overlapping writes");
    rejected([&]{ writer.release(id); },"pending write cannot be discarded");
    rejected([&]{ writer.retry(id); },"pending write cannot be retried");
    rejected([&]{ (void)writer.advance(id+1); },"unknown request rejected");
    gate.release(); check(!await(writer,id),"real IO failure published");
    check(revision(read(path))==1&&chunk_revision(path,read(path),"a")==1,"failed async write preserves complete old checkpoint");
    std::filesystem::remove(path+".tmp"); throw_once=true; writer.retry(id);
    const auto thrown=await(writer,id);
    check(!thrown&&thrown.error()=="injected writer exception","writer exceptions become retryable diagnostics");
    writer.retry(id);
    check(bool(await(writer,id)),"retry commits original frozen payload");
    check(revision(read(path))==2&&chunk_revision(path,read(path),"a")==2,"caller mutations never cross worker boundary");
    check(worker!=std::this_thread::get_id(),"disk work executes off the owner thread");
    check(writer.advance(id)==ScResult<bool>{true},"observed success is stable");
    rejected([&]{ writer.retry(id); },"successful write cannot be retried");
    writer.release(id); check(!writer.active(),"completion explicitly released");
    rejected([&]{ (void)writer.advance(id); },"released request cannot revive");
    const auto next=writer.submit(ScSaveWriteRequest{path,"test",1,record(3),{}});
    check(next==id+1&&bool(await(writer,next)),"writer accepts subsequent checkpoint"); writer.release(next);
    check(chunk_revision(path,read(path),"a")==2,"async plain checkpoint retains chunks");
    // Shutdown drains an accepted write even when its result was never observed.
    { ScSaveIo closing; closing.submit(ScSaveWriteRequest{path,"test",1,record(4),{}}); }
    check(revision(read(path))==4,"application shutdown does not drop accepted save");
    ScSaveWriteRequest invalid{path,"test",1,record(5),{{"../escape",state(5)}}};
    rejected([&]{ writer.submit(invalid); },"invalid chunk rejected before queueing");
    invalid.changes.clear();
    for(int i=0;i<9;++i) invalid.changes.emplace(std::to_string(i),ScValue{ScValue::Object{
        {"text",ScValue{std::string(SC_STATE_BYTES/2,'x')}}}});
    rejected([&]{ writer.submit(std::move(invalid)); },"native async payload budget enforced");
    check(!writer.active()&&revision(read(path))==4,"preflight failure leaves writer and disk unchanged");
}
void asynchronous_reads() {
    Directory directory; const auto path=directory.slot();
    Gate gate; std::thread::id thread; bool throw_once=true;
    ScSaveIo gated({},[&](const ScSaveReadRequest& request)->ScResult<ScSaveReadResult> {
        thread=std::this_thread::get_id(); gate.wait();
        if(std::exchange(throw_once,false)) throw std::runtime_error("read fault");
        return ScSaveReadResult{request.snapshot,{{request.keys.front(),state(7)}}};
    });
    Release cleanup{gate};
    ScSaveReadRequest frozen{path,"test",1,{"a"},record(1)};
    const auto id=gated.submit(frozen); gate.await_entry(); frozen.snapshot=record(99);
    check(gated.reading()&&gated.advance(id)==ScResult<bool>{false},"read polling never waits for IO");
    rejected([&]{ gated.submit(ScSaveWriteRequest{path,"test",1,record(2),{}}); },"read excludes writes");
    rejected([&]{ gated.release(id); },"pending read retained");
    gate.release(); check(!await(gated,id),"read exception observed"); gated.retry(id);
    check(bool(await(gated,id))&&thread!=std::this_thread::get_id(),"read runs on worker");
    check(revision(gated.outcome(id)->value().snapshot)==1,"read request owns frozen snapshot");
    gated.release(id);

    check(bool(sc_save_write(path,record(1),"test",1,{{"a",state(1)},{"b",state(10)}})),"old read snapshot");
    const auto first=read(path);
    check(bool(sc_save_write(path,record(2),"test",1,{{"a",state(2)},{"b",state(20)}})),"new read snapshot");
    const auto damaged=std::filesystem::path(path+".chunks")/file_name(state(20));
    write_text(damaged,"{broken"); // An unrequested corrupt chunk must select the whole backup.
    ScSaveIo io;
    auto request=io.submit(ScSaveReadRequest{path,"test",1,{"absent","a"},{}});
    check(bool(await(io,request)),"complete backup selected in worker");
    const auto& result=io.outcome(request)->value();
    check(revision(result.snapshot)==1&&result.chunks.size()==1&&result.chunks.at("a").get("revision")->number()==1,
        "read result uses one whole snapshot and omits absent chunks");
    io.release(request);
    write_text(damaged,sc_json_write(state(20)).c_str());
    request=io.submit(ScSaveReadRequest{path,"test",1,{"a"},first});
    check(bool(await(io,request))&&revision(io.outcome(request)->value().snapshot)==1,"pinned snapshot retained after primary repair");
    io.release(request);
    const auto pinned_file=std::filesystem::path(path+".chunks")/file_name(state(1));
    write_text(pinned_file,"{broken");
    request=io.submit(ScSaveReadRequest{path,"test",1,{"a"},first});
    check(!await(io,request),"damaged pinned snapshot never mixes in newer data");
    write_text(pinned_file,sc_json_write(state(1)).c_str()); io.retry(request);
    check(bool(await(io,request))&&io.outcome(request)->value().chunks.at("a").get("revision")->number()==1,"retry reads repaired frozen index");
    io.release(request);
    request=io.submit(ScSaveReadRequest{path+"-missing","test",1,{"a"},{}});
    check(bool(await(io,request))&&std::holds_alternative<std::monostate>(io.outcome(request)->value().snapshot.data),"missing slot is successful absence");
    io.release(request);
    rejected([&]{ io.submit(ScSaveReadRequest{path,"test",1,{"a","a"},{}}); },"duplicate keys rejected");
    rejected([&]{ io.submit(ScSaveReadRequest{path,"test",1,{"../bad"},{}}); },"unsafe keys rejected");
    ScValue::Object large; std::vector<std::string> keys;
    for(int i=0;i<9;++i) {
        keys.push_back("large"+std::to_string(i));
        large.emplace(keys.back(),ScValue{ScValue::Object{{"text",ScValue{std::string(SC_STATE_BYTES/2,'x')}}}});
    }
    check(bool(sc_save_write(path,record(3),"test",1,large)),"large valid source checkpoint");
    request=io.submit(ScSaveReadRequest{path,"test",1,keys,{}});
    auto bounded=await(io,request);check(!bounded&&bounded.error().find("1 MiB")!=std::string::npos,"read batch bounded without partial result");
    io.release(request);
}
void asynchronous_delete() {
    Directory directory; const auto path=directory.slot();
    check(bool(sc_save_write(path,record(1),"test",1,{{"a",state(1)}})),"deletion source checkpoint");
    bool fail_once=true; std::thread::id worker;
    ScSaveIo io({}, {}, [&](const ScSaveDeleteRequest& request)->ScResult<void> {
        worker=std::this_thread::get_id();
        if(std::exchange(fail_once,false)) return std::unexpected("injected delete failure");
        return sc_save_delete(request.path);
    });
    const auto id=io.submit(ScSaveDeleteRequest{path,"test",1});
    check(io.deleting()&&!io.reading(),"delete request identified separately from read/write");
    rejected([&]{ io.submit(ScSaveReadRequest{path,"test",1,{},{}}); },"delete excludes reads");
    auto failed=await(io,id);
    check(!failed&&failed.error()=="injected delete failure"&&bool(sc_save_read(path,"test",1)),
        "failed delete retains source before retry");
    io.retry(id);
    check(bool(await(io,id))&&worker!=std::this_thread::get_id(),"retry deletes on worker");
    check(!std::filesystem::exists(path)&&!std::filesystem::exists(path+".bak")
        &&!std::filesystem::exists(path+".chunks"),"delete removes slot, backup and owned chunks");
    io.release(id);
    const auto missing=io.submit(ScSaveDeleteRequest{path,"test",1});
    check(bool(await(io,missing)),"missing slot deletion is idempotent"); io.release(missing);
    std::filesystem::create_directories(path+".chunks");
    const auto unrelated=std::filesystem::path(path+".chunks")/"keep.txt";
    write_text(unrelated,"unrelated");
    const auto blocked=io.submit(ScSaveDeleteRequest{path,"test",1});
    check(!await(io,blocked)&&std::filesystem::exists(unrelated),"nonempty chunk directory reports incomplete delete");
    std::filesystem::remove(unrelated);
    io.retry(blocked);
    check(bool(await(io,blocked))&&!std::filesystem::exists(path+".chunks"),"delete retry removes empty chunk directory");
    io.release(blocked);
}
}
int main(int argc,char** argv) {
    try {
        const bool async_only=argc==2&&!std::strcmp(argv[1],"--async");
        check(argc==1||async_only,"expected optional --async");
        asynchronous(); asynchronous_reads(); asynchronous_delete(); interruption();
        if(!async_only) { lifecycle(); corruption(); bounds_and_scale(); immutable_names(); }
        std::cout<<"chunk saves: selected atomic snapshot and asynchronous writer checks passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

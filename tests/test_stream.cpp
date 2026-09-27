#include "shiny/stream.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("shiny-stream-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::size_t charge{};
    Fixture(double gid=1) {
        std::filesystem::create_directory(root); ScValue::Array entries;
        for(int x=0;x<3;++x) {
            auto chunk=ScValue{ScValue::Object{{"x",ScValue{double(x)}},{"y",ScValue{0.0}},
                {"objects",ScValue{ScValue::Array{}}},{"layers",ScValue{ScValue::Object{{"0",ScValue{ScValue::Array(1024,ScValue{gid})}}}}}}};
            auto bytes=sc_json_write(chunk); auto name=std::to_string(x)+".json";
            write(name,bytes); charge=bytes.size()*64;
            entries.emplace_back(ScValue::Object{{"x",ScValue{double(x)}},{"y",ScValue{0.0}},
                {"path",ScValue{name}},{"bytes",ScValue{double(bytes.size())}}});
        }
        write("index.json",sc_json_write(ScValue{ScValue::Object{{"format",ScValue{3.0}},{"tilewidth",ScValue{8.0}},{"tileheight",ScValue{8.0}},{"layers",ScValue{ScValue::Array{ScValue{ScValue::Object{{"type",ScValue{std::string{"tilelayer"}}}}},ScValue{ScValue::Object{{"type",ScValue{std::string{"objectgroup"}}}}}}}},
            {"chunk_size",ScValue{32.0}},{"chunks",ScValue{entries}}}}));
    }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(root,error); }
    void write(const std::string& name,const std::string& bytes) {
        std::ofstream out(root/name,std::ios::binary); out<<bytes; check(bool(out),"fixture write");
    }
    std::string index() const { return (root/"index.json").string(); }
};
ScResult<bool> await(ScStream& stream,std::uint64_t frame) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;) {
        auto result=stream.advance(frame);
        if(!result||*result) return result;
        check(std::chrono::steady_clock::now()<deadline,"stream completion timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
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
        check(condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"reader entered controlled gate");
    }
    void release() { std::lock_guard lock(mutex); released=true; condition.notify_all(); }
};
struct Release { Gate& gate; ~Release() { gate.release(); } }; // Always unblock before stream destruction.
void application_loader_lifetime() {
    Fixture old_map,new_map(7); Gate gate;
    std::vector<std::thread::id> threads;
    std::vector<std::string> reads;
    ScContentLoader loader([&](const std::string& path,std::size_t bytes) {
        threads.push_back(std::this_thread::get_id()); reads.push_back(path);
        if(std::filesystem::path(path).parent_path()==old_map.root) gate.wait();
        return sc_json_file(path,bytes,20);
    },[&](const ScImageRequest& request)->ScResult<ScImagePixels> {
        threads.push_back(std::this_thread::get_id()); reads.push_back(request.path);
        return ScImagePixels{1,1,{255,0,0,255}};
    });
    Release cleanup{gate};
    auto old_room=std::make_unique<ScStream>(old_map.index(),loader);
    old_room->request(0,0,10); gate.await_entry();
    auto candidate=std::make_unique<ScStream>(old_map.index(),loader);
    candidate->request(1,0,0);
    // A regressed joining destructor must fail, not deadlock the test on its gate.
    auto destroyed=std::async(std::launch::async,[old=std::move(old_room),candidate=std::move(candidate)]() mutable {
        candidate.reset(); old.reset();
    });
    const bool prompt=destroyed.wait_for(std::chrono::seconds(1))==std::future_status::ready;
    if(!prompt) gate.release();
    destroyed.get(); check(prompt,"room and candidate destruction must not wait for disk");
    const auto image=loader.submit_image({"survivor.png",1,1});
    ScStream room(new_map.index(),loader);
    check(room.request(0,0,0)==1,"new room has an independent request sequence");
    auto ready=room.advance(0); check(ready&&!*ready,"new room waits without publishing cancelled data");
    gate.release(); check(bool(await(room,0)),"surviving room completes on the shared loader");
    const auto chunk=room.get(0,0);
    const auto& cells=std::get<ScValue::Array>((**chunk).get("layers")->get("0")->data);
    check(cells[0].number()==7,"same coordinates cannot resurrect old room data");
    std::optional<ScResult<ScImagePixels>> pixels;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!(pixels=loader.take_image(image))) {
        check(std::chrono::steady_clock::now()<deadline,"image result observation timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(pixels&&*pixels&&(**pixels).rgba[0]==255,"image survives room cancellation on same worker");
    check(reads.size()==3&&reads[1]=="survivor.png"&&std::filesystem::path(reads.back()).parent_path()==new_map.root,
        "cancelled queued candidate is never read");
    check(threads.size()==3&&threads[0]==threads[1]&&threads[1]==threads[2]&&threads[0]!=std::this_thread::get_id(),
        "one application worker serves map and image requests across both rooms");
}
void order_and_nonblocking() {
    Fixture fixture; Gate gate;
    ScStream stream(fixture.index(),1024*1024,[&](const std::string& path,std::size_t bytes) {
        if(std::filesystem::path(path).filename()=="0.json") gate.wait();
        return sc_json_file(path,bytes,20);
    });
    Release cleanup{gate};
    check(stream.request(1,0,0)==1,"first sequence"); check(bool(await(stream,0)),"warm later chunk"); stream.release(1,0);
    check(stream.request(0,0,10)==2,"slow request");
    check(stream.request(1,0,10)==3,"already cached later request"); gate.await_entry();
    const auto begin=std::chrono::steady_clock::now();
    for(int i=0;i<100;++i) {
        auto ready=stream.advance(10); check(ready&&!*ready,"due gate returns pending immediately");
        auto first=stream.get(0,0),second=stream.get(1,0);
        check(first&&!*first&&second&&!*second,"cached later completion cannot overtake slow predecessor");
    }
    check(std::chrono::steady_clock::now()-begin<std::chrono::milliseconds(500),"no disk wait on owner thread");
    check(stream.statistics().get("loaded")->number()==0,"worker timing hidden from gameplay stats");
    gate.release(); check(bool(await(stream,10)),"whole due batch commits");
    check(stream.get(0,0)->has_value()&&stream.get(1,0)->has_value(),"both records visible together");
    check(stream.statistics().get("last_sequence")->number()==3,"request sequence independent of IO completion");
    check(!stream.advance(9),"backwards frame rejected");
}
void capacity_and_cancellation() {
    Fixture fixture; Gate gate;
    ScStream stream(fixture.index(),512*1024,[&](const std::string& path,std::size_t bytes) {
        gate.wait(); return sc_json_file(path,bytes,20);
    });
    Release cleanup{gate};
    stream.request(0,0,5); gate.await_entry();
    check(stream.statistics().get("resident_bytes")->number()==fixture.charge,"memory reserved before disk completes");
    bool exhausted=false;
    try { stream.request(1,0,5); } catch(const std::exception&) { exhausted=true; }
    check(exhausted&&stream.statistics().get("pinned")->number()==1,"capacity failure does not pin another chunk");
    stream.release(0,0);
    check(!stream.get(0,0),"cancelled reference unavailable");
    auto pending=stream.advance(5); check(pending&&!*pending,"cancellation drains at deterministic boundary");
    gate.release(); check(bool(await(stream,5)),"cancelled IO drained");
    check(stream.statistics().get("resident_bytes")->number()==0,"cancelled reservation freed");
    stream.request(1,0,6); check(bool(await(stream,6)),"capacity reusable after cancellation");
    stream.release(1,0);
    for(int i=0;i<12;++i) {
        stream.request(i%3,0,static_cast<std::uint64_t>(7+i));
        check(bool(await(stream,static_cast<std::uint64_t>(7+i))),"revisit commits"); stream.release(i%3,0);
        check(stream.statistics().get("resident_bytes")->number()<=512*1024,"revisits stay in budget");
    }
}
void failure_and_schedule() {
    Fixture fixture;
    ScStream stream(fixture.index());
    stream.request(0,0,3);
    bool out_of_order=false;
    try { stream.request(1,0,2); } catch(const std::exception&) { out_of_order=true; }
    check(out_of_order,"nonmonotonic deadline rejected");
    check(stream.request(-3,-4,3)==0&&stream.get(-3,-4)->has_value(),"known sparse area immediate");
    check(bool(await(stream,2))&&!*stream.get(0,0),"early completed data still hidden");
    check(bool(await(stream,3)),"visible only at deadline");
    stream.request(0,0,4); stream.release(0,0); check(stream.get(0,0)->has_value(),"balanced duplicate pin retains visibility");
    std::filesystem::remove(fixture.root/"1.json");
    stream.request(1,0,4); stream.request(2,0,4);
    auto failed=await(stream,4); check(!failed&&failed.error().find("request 2")!=std::string::npos,"error identifies ordered request");
    check(!*stream.get(2,0)&&stream.get(0,0)->has_value(),"failed batch leaves previous visible set unchanged");
    stream.release(1,0); check(bool(await(stream,4))&&stream.get(2,0)->has_value(),"cancel failed chunk and commit remaining batch");
    std::ifstream in(fixture.root/"index.json"); std::string text((std::istreambuf_iterator<char>(in)),{}); in.close();
    auto index=sc_json_read(text); std::get<ScValue::Object>(index->data)["format"]=ScValue{2.0}; fixture.write("index.json",sc_json_write(*index));
    bool old=false; try { ScStream unsupported(fixture.index()); } catch(const std::exception&) { old=true; }
    check(old,"old index format rejected");
}
void retry_failed_batch() {
    Fixture fixture; Gate retry_gate; std::atomic<int> attempts{};
    ScStream stream(fixture.index(),1024*1024,[&](const std::string& path,std::size_t bytes) {
        if(std::filesystem::path(path).filename()=="1.json") {
            if(++attempts==1) throw std::runtime_error("injected read failure");
            retry_gate.wait();
        }
        return sc_json_file(path,bytes,20);
    });
    Release cleanup{retry_gate};
    stream.request(0,0,0); check(bool(await(stream,0)),"warm active world");
    const auto sequence=stream.request(1,0,10); stream.request(1,0,10); stream.request(2,0,10);
    check(!stream.failure().get("sequence"),"no early failure observation");
    const auto failed=await(stream,10); check(!failed,"failed batch held");
    auto info=stream.failure();
    check(info.get("sequence")->number()==double(sequence)&&info.get("frame")->number()==10&&
        info.get("x")->number()==1&&info.get("y")->number()==0&&
        info.get("message")->text()=="injected read failure","failure identifies scheduled request");
    const auto before=stream.statistics();
    bool rejected=false; try { stream.retry(sequence+1); } catch(const std::exception&) { rejected=true; }
    check(rejected&&stream.failure().get("sequence"),"stale retry leaves failure intact");
    stream.retry(sequence); retry_gate.await_entry();
    check(!stream.failure().get("sequence"),"retry clears only reported failure");
    auto ready=stream.advance(10); check(ready&&!*ready,"retry does not block owner");
    check(stream.get(0,0)->has_value()&&!*stream.get(1,0)&&!*stream.get(2,0),"retry retains old visible set and batch atomicity");
    rejected=false; try { stream.retry(sequence); } catch(const std::exception&) { rejected=true; }
    check(rejected,"pending retry cannot be duplicated");
    for(const auto* key:{"resident_bytes","pinned","queued","last_sequence"})
        check(stream.statistics().get(key)->number()==before.get(key)->number(),"retry preserves reservation, references, order and sequence");
    retry_gate.release(); check(bool(await(stream,10)),"retry recovers at original boundary");
    check(stream.get(1,0)->has_value()&&stream.get(2,0)->has_value(),"recovered batch publishes together");
    stream.release(1,0); check(stream.get(1,0)->has_value(),"duplicate reference survives retry");
    stream.release(1,0); check(!stream.get(1,0),"retry did not add a reference");
    check(attempts==2&&!stream.failure().get("sequence"),"exactly one retry and no stale failure");
}
void object_validation() {
    Fixture fixture;
    for(int mode=0;mode<5;++mode) {
        ScStream stream(fixture.index(),1024*1024,[&](const std::string& path,std::size_t bytes) {
            auto result=sc_json_file(path,bytes,20);
            if(!result||std::filesystem::path(path).filename()!="1.json") return result;
            auto item=ScValue{ScValue::Object{{"persistent_id",ScValue{std::string{"actors:7"}}},
                {"layer",ScValue{mode==3?0.0:1.0}},{"x",ScValue{mode==2?0.0:256.0}},{"y",ScValue{0.0}}}};
            ScValue::Array objects{item};
            if(mode==1) objects.push_back(item);
            if(mode==4) std::get<ScValue::Object>(objects[0].data)["persistent_id"]=ScValue{std::string{"bad id"}};
            std::get<ScValue::Object>(result->data)["objects"]=ScValue{objects};
            return result;
        });
        stream.request(0,0,0); check(bool(await(stream,0)),"warm previous visible chunk");
        stream.request(1,0,1);
        auto ready=await(stream,1);
        check(bool(ready)==(mode==0),"object validation rejects duplicate ID, wrong owner, layer and ID syntax");
        check(stream.get(0,0)->has_value(),"object failure retains previous visible chunk");
        auto empty=stream.get(-1,-1);
        check(empty&&*empty&&(**empty).get("objects"),"known empty chunk includes object array");
        if(mode==0) {
            auto chunk=stream.get(1,0);
            const auto& objects=std::get<ScValue::Array>((**chunk).get("objects")->data);
            check(objects.size()==1&&objects[0].get("persistent_id")->text()=="actors:7","object payload preserved");
        }
    }
}
}
int main(int argc,char** argv) {
    try {
        const bool retry_only=argc==2&&!std::strcmp(argv[1],"--retry");
        const bool lifetime_only=argc==2&&!std::strcmp(argv[1],"--lifetime");
        check(argc==1||retry_only||lifetime_only,"expected optional --retry or --lifetime");
        if(!retry_only) application_loader_lifetime();
        if(!lifetime_only) retry_failed_batch();
        if(argc==1) { order_and_nonblocking(); capacity_and_cancellation(); failure_and_schedule(); object_validation(); }
        std::cout<<"stream: planned publication, controlled slow IO, cancellation and bounded cache passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

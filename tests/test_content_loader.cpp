#include "shiny/content_loader.h"
#include "shiny/path.h"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message) {
    bool rejected=false; try { action(); } catch(const std::exception&) { rejected=true; }
    check(rejected,message);
}
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("shiny-png-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { std::filesystem::create_directory(root); }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(root,error); }
    std::string write(const std::string& name,const std::vector<unsigned char>& bytes) {
        const auto path=root/sc_path(name);
        std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        check(bool(out),"write PNG fixture");
        const auto text=path.generic_u8string(); return {reinterpret_cast<const char*>(text.data()),text.size()};
    }
};
ScResult<ScImagePixels> await(ScContentLoader& loader,std::uint64_t ticket) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;) {
        if(auto result=loader.take_image(ticket)) return std::move(*result);
        check(std::chrono::steady_clock::now()<deadline,"image completion timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{},released{};
    void wait() { std::unique_lock lock(mutex); entered=true; condition.notify_all(); condition.wait(lock,[&]{return released;}); }
    void await_entry() {
        std::unique_lock lock(mutex);
        check(condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"image reader entered gate");
    }
    void release() { std::lock_guard lock(mutex); released=true; condition.notify_all(); }
};
struct Release { Gate& gate; ~Release() { gate.release(); } };

void pixels_and_errors() {
    Fixture fixture; ScContentLoader loader;
    const std::vector<unsigned char> rgba={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,8,6,0,0,0,244,34,127,138,0,0,0,15,73,68,65,84,120,156,99,248,207,192,240,31,8,29,0,16,57,3,62,173,124,225,154,0,0,0,0,73,69,78,68,174,66,96,130};
    const std::vector<unsigned char> palette={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,8,3,0,0,0,195,252,143,184,0,0,0,6,80,76,84,69,255,0,0,0,255,0,210,135,239,113,0,0,0,2,116,82,78,83,255,64,147,107,113,218,0,0,0,11,73,68,65,84,120,156,99,96,96,4,0,0,4,0,2,191,122,63,74,0,0,0,0,73,69,78,68,174,66,96,130};
    const auto path=fixture.write("图集.png",rgba),indexed=fixture.write("palette.png",palette);
    for(const auto& file:{path,indexed}) {
        auto result=await(loader,loader.submit_image({file,2,1}));
        check(result&&result->width==2&&result->height==1&&result->rgba==std::vector<unsigned char>({255,0,0,255,0,255,0,64}),
            "RGBA and palette transparency decode identically");
    }
    ScImagePixels retained;
    {
        ScContentLoader temporary;
        auto result=await(temporary,temporary.submit_image({path,2,1}));
        check(bool(result),"standalone decode completes"); retained=std::move(*result);
    }
    check(retained.rgba[7]==64,"taken pixels outlive their loader");
    check(!await(loader,loader.submit_image({path,1,1})),"changed dimensions rejected before decode");
    auto short_data=rgba; short_data.resize(45);
    auto broken=fixture.write("truncated.png",short_data);
    check(!await(loader,loader.submit_image({broken,2,1})),"truncated pixels fail without partial result");
    auto signature=rgba; signature[0]=0;
    broken=fixture.write("signature.png",signature);
    check(!await(loader,loader.submit_image({broken,2,1})),"non-PNG input rejected");
    broken=fixture.write("huge.png",rgba);
    std::filesystem::resize_file(sc_path(broken),32u*1024u*1024u+1);
    check(!await(loader,loader.submit_image({broken,2,1})),"encoded size limit checked before allocation");
    std::filesystem::remove(sc_path(path));
    check(!await(loader,loader.submit_image({path,2,1})),"missing file is a recoverable failure");
    fixture.write("图集.png",rgba);
    auto repaired=await(loader,loader.submit_image({path,2,1}));
    check(bool(repaired),"worker remains usable after errors and source repair");
}
void budgets_and_cancellation() {
    Gate gate; unsigned calls{};
    ScContentLoader loader({},[&](const ScImageRequest& request)->ScResult<ScImagePixels> {
        ++calls;
        if(request.path=="slow") gate.wait();
        if(request.width>1) return std::unexpected("injected read failure");
        return ScImagePixels{1,1,{1,2,3,4}};
    });
    Release cleanup{gate};
    const auto running=loader.submit_image({"slow",4096,4096}); gate.await_entry();
    auto queued=loader.submit_image({"cancelled",4096,4096});
    rejects([&]{loader.submit_image({"over",1,1});},"reserve decoded bytes before reading");
    check(!loader.take_image(running),"polling slow decode must not wait for disk");
    loader.cancel(running);
    rejects([&]{loader.submit_image({"over",1,1});},"running cancellation retains allocation charge until finished");
    loader.cancel(queued);
    rejects([&]{(void)await(loader,queued);},"queued cancellation invalidates ticket");
    const auto small=loader.submit_image({"small",1,1});
    gate.release();
    auto result=await(loader,small);
    check(result&&result->rgba==std::vector<unsigned char>({1,2,3,4})&&calls==2,"cancelled queued image never decoded");
    rejects([&]{(void)await(loader,small);},"consumed result expires");
    const auto whole=loader.submit_image({"whole",8192,4096});
    auto failed=await(loader,whole); check(!failed&&failed.error()=="injected read failure","cancelled jobs return their entire reservation");
    rejects([&]{loader.submit_image({"too-large",8192,8192});},"reject oversized decoded image");
    rejects([&]{loader.submit_image({"zero",0,1});},"reject invalid dimensions");
    rejects([&]{loader.submit_image({std::string("bad\0path",8),1,1});},"reject embedded path NUL");
    ScContentLoader invalid({},[](const ScImageRequest&)->ScResult<ScImagePixels>{return ScImagePixels{1,1,{}};});
    check(!await(invalid,invalid.submit_image({"invalid",1,1})),"reader cannot return incomplete pixel storage");
    ScContentLoader throwing({},[](const ScImageRequest&)->ScResult<ScImagePixels>{throw 7;});
    check(!await(throwing,throwing.submit_image({"throw",1,1})),"unknown callback exceptions become failed results");
}
}
int main() {
    try {
        pixels_and_errors(); budgets_and_cancellation();
        std::cout<<"content loader: PNG pixels, errors, budgets and cancellation passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

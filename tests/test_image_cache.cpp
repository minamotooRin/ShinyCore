#include "shiny/image_cache.h"
#include "shiny/room_images.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
ScImageId acquire(ScImageCache& cache,const std::string& path) {
    auto result=cache.acquire({path,128,128}); check(bool(result),"acquire image"); return *result;
}
ScResult<bool> await(ScImageCache& cache,ScImageId id) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;) {
        auto ready=cache.poll(id); if(!ready||*ready) return ready;
        check(std::chrono::steady_clock::now()<deadline,"cache observation timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
ScImagePixels pixels(const ScImageRequest& request) {
    return {request.width,request.height,std::vector<unsigned char>(static_cast<std::size_t>(request.width)*static_cast<std::size_t>(request.height)*4,127)};
}
void residency() {
    std::atomic<unsigned> reads{};
    ScContentLoader loader({},[&](const ScImageRequest& request)->ScResult<ScImagePixels>{++reads; return pixels(request);});
    ScImageCache cache(loader,128*1024,2);
    const auto first=acquire(cache,"first"),second=acquire(cache,"second");
    check(bool(await(cache,first))&&bool(await(cache,second)),"both active images loaded");
    check(acquire(cache,"first")==first&&reads==2,"duplicate reference reuses decoded pixels");
    check(!cache.acquire({"first",1,1}),"same path cannot silently change dimensions");
    check(!cache.acquire({"third",128,128}),"pinned capacity cannot evict active images");
    check(bool(cache.release(first))&&cache.pinned(first),"one remaining reference stays pinned");
    check(bool(cache.release(first))&&!cache.pinned(first)&&cache.contains(first),"released image remains cached");
    check(!cache.release(first)&&!cache.poll(first),"unbalanced release and unretained poll rejected");
    const auto third=acquire(cache,"third");
    check(third!=first&&!cache.contains(first)&&cache.pixels(first)==nullptr,"eviction expires old ID");
    check(bool(await(cache,third))&&cache.pixels(second),"replacement preserves other active image");
    check(!cache.retry(second),"successful image cannot be retried");
    check(bool(cache.release(second))&&bool(cache.release(third)),"release references");
    check(acquire(cache,"second")==second&&reads==3,"unreferenced ready data reuses without IO");
    check(bool(cache.release(second)),"release reused image");
    const auto fourth=acquire(cache,"fourth");
    check(!cache.contains(third)&&cache.contains(second),"only the least recently used unpinned image is evicted");
    check(bool(await(cache,fourth)),"LRU replacement loads");
    auto stats=cache.statistics();
    check(stats.capacity==2&&stats.resident==2&&stats.pinned==1&&stats.pending==0&&stats.resident_bytes==128*1024,"reservation and pin statistics");
    check(!cache.acquire({"oversized",8192,8192})&&cache.contains(fourth),"byte budget failure preserves active cache");
}
void retry() {
    std::atomic<unsigned> attempts{};
    ScContentLoader loader({},[&](const ScImageRequest& request)->ScResult<ScImagePixels> {
        if(++attempts==1) return std::unexpected("decode fault");
        return pixels(request);
    });
    ScImageCache cache(loader,65536,1); const auto id=acquire(cache,"repair");
    auto failed=await(cache,id); check(!failed&&failed.error()=="decode fault","failure is retained");
    check(!cache.poll(id)&&attempts==1,"failure does not implicitly retry");
    check(cache.pinned(id)&&!cache.pixels(id)&&cache.statistics().resident_bytes==65536,"failure keeps reservation and reference");
    check(bool(cache.retry(id))&&bool(await(cache,id))&&cache.pixels(id)&&attempts==2,"explicit retry preserves ID");
}
void revisions() {
    std::atomic<unsigned char> shade{11}; std::atomic<bool> broken{};
    ScContentLoader loader({},[&](const ScImageRequest& request)->ScResult<ScImagePixels> {
        if(broken) return std::unexpected("broken revision");
        auto result=pixels(request); result.rgba[0]=shade.load(); return result;
    });
    ScImageCache cache(loader,3*65536,3);
    const auto old=acquire(cache,"image"); check(bool(await(cache,old)),"initial revision loads");
    shade=22;
    auto fresh=cache.refresh(old); check(bool(fresh)&&*fresh!=old&&bool(await(cache,*fresh)),"reload creates a new revision");
    check(cache.pixels(old)->rgba[0]==11&&cache.pixels(*fresh)->rgba[0]==22,"both pixel versions remain intact");
    check(acquire(cache,"image")==old&&bool(cache.release(old)),"unpublished revision is invisible to acquire");
    check(bool(cache.release(*fresh))&&!cache.contains(*fresh),"cancelled ready revision is discarded");
    broken=true; fresh=cache.refresh(old); check(bool(fresh)&&!await(cache,*fresh),"failed replacement is private");
    check(acquire(cache,"image")==old&&bool(cache.release(old)),"failed replacement cannot poison lookup");
    broken=false; check(bool(cache.retry(*fresh))&&bool(await(cache,*fresh)),"retry retains fresh ID");
    const std::array conflict{old,*fresh}; check(!cache.publish(conflict),"mixed revisions fail whole publication");
    const std::array invalid{*fresh,ScImageId{999}}; check(!cache.publish(invalid),"invalid batch fails before promotion");
    check(acquire(cache,"image")==old&&bool(cache.release(old)),"failed publication preserves preference");
    check(bool(cache.publish(std::array{*fresh})),"publish replacement");
    check(acquire(cache,"image")==*fresh&&bool(cache.release(*fresh)),"new acquisitions see committed pixels");
    check(cache.pixels(old)->rgba[0]==11,"existing owner keeps old pixels");
    check(bool(cache.release(old))&&!cache.contains(old),"retired revision disappears after last release");
    ScImageCache small(loader,65536,1); auto pinned=acquire(small,"small"); check(bool(await(small,pinned)),"small cache setup");
    check(!small.refresh(pinned)&&small.pixels(pinned),"reload cannot exceed old-plus-new budget");

    ScRoomImages room(cache,1),other(cache,2);
    auto wait_room=[](ScRoomImages& value) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        for(;;) {
            auto result=value.advance(); check(bool(result),"room image preparation"); if(*result) return;
            check(std::chrono::steady_clock::now()<deadline,"room preparation timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    {
        ScImageCache limited(loader,3*65536,3); ScRoomImages batch(limited,3);
        auto initial=batch.prepare({{"left",{"left",128,128}},{"right",{"right",128,128}}});
        check(bool(initial),"prepare limited batch"); wait_room(batch); check(bool(batch.commit(*initial)),"commit limited batch");
        const auto left=batch.image("left"),right=batch.image("right");
        check(!batch.reload()&&!batch.busy(),"second reload reservation fails atomically");
        check(limited.statistics().resident==2&&limited.statistics().pinned==2&&batch.image("left")==left&&batch.image("right")==right,
            "partial reload rollback releases every fresh reference and preserves both active images");
    }
    auto request=room.prepare({{"a",{"image",128,128}},{"alias",{"image",128,128}}});
    check(bool(request),"prepare room aliases"); wait_room(room); check(bool(room.commit(*request)),"publish aliases");
    auto second=other.prepare({{"a",{"image",128,128}}}); check(bool(second),"other room pins current revision");
    wait_room(other); check(bool(other.commit(*second)),"commit other room");
    const auto prior=room.image("a"); shade=33;
    request=room.reload(); check(bool(request),"stage alias reload"); wait_room(room);
    check(bool(room.commit(*request))&&room.image("a")==room.image("alias")&&room.image("a")!=prior,"aliases share one refreshed image");
    check(other.image("a")==prior&&cache.pixels(prior)->rgba[0]==22,"other room retains its prior version");
    check(cache.pixels(room.image("a"))->rgba[0]==33,"reloading room sees new pixels");
    check(bool(cache.release(*fresh)),"release native test reference");
}
void deferred_admission() {
    std::mutex mutex; std::condition_variable condition; bool entered{},released{};
    ScContentLoader loader({},[&](const ScImageRequest& request)->ScResult<ScImagePixels> {
        if(request.path=="cancelled") {
            std::unique_lock lock(mutex); entered=true; condition.notify_all();
            condition.wait(lock,[&]{return released;});
            return std::unexpected("cancelled test read");
        }
        return pixels(request);
    });
    auto unblock=[&] { std::lock_guard lock(mutex); released=true; condition.notify_all(); };
    struct Release { decltype(unblock)& callback; ~Release() { callback(); } } cleanup{unblock};
    // Reserve the loader budget without allocating a large image in this test.
    const auto old=loader.submit_image({"cancelled",8192,4096});
    { std::unique_lock lock(mutex); check(condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"cancelled reader enters gate"); }
    loader.cancel(old);
    ScImageCache cache(loader,65536,1);
    const auto next=acquire(cache,"next");
    auto ready=cache.poll(next);
    check(ready&&!*ready&&cache.pinned(next)&&cache.statistics().pending==1,"busy loader defers admission without failing the replacement");
    unblock();
    check(bool(await(cache,next))&&cache.pixels(next),"same reserved ID loads after cancelled read releases budget");
}
void destruction() {
    std::mutex mutex; std::condition_variable condition; bool entered{},released{};
    std::atomic<unsigned> reads{};
    ScContentLoader loader({},[&](const ScImageRequest& request)->ScResult<ScImagePixels> {
        ++reads;
        if(request.path=="slow") {
            std::unique_lock lock(mutex); entered=true; condition.notify_all(); condition.wait(lock,[&]{return released;});
        }
        return pixels(request);
    });
    auto unblock=[&] { std::lock_guard lock(mutex); released=true; condition.notify_all(); };
    struct Release { decltype(unblock)& callback; ~Release() { callback(); } } cleanup{unblock};
    auto cache=std::make_unique<ScImageCache>(loader);
    auto slow=acquire(*cache,"slow");
    check(bool(cache->poll(slow)),"queue controlled read");
    { std::unique_lock lock(mutex); check(condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"reader enters controlled gate"); }
    auto queued=acquire(*cache,"queued");
    check(bool(cache->poll(queued)),"queue second read");
    check(!*cache->poll(slow)&&!cache->pixels(slow),"poll pending without waiting");
    check(bool(cache->release(queued))&&!cache->contains(queued),"last pending reference cancels queued work");
    auto destroyed=std::async(std::launch::async,[old=std::move(cache)]() mutable { old.reset(); });
    const bool prompt=destroyed.wait_for(std::chrono::seconds(1))==std::future_status::ready;
    if(!prompt) unblock();
    destroyed.get(); check(prompt,"cache destruction must not wait for disk");
    ScImageCache next(loader); const auto surviving=acquire(next,"new");
    unblock(); check(bool(await(next,surviving))&&reads==2,"shared loader discards cancelled data and serves next cache");
}
}
int main() {
    try { residency(); retry(); revisions(); deferred_admission(); destruction(); std::cout<<"image cache: references, revisions, retry, deferred admission and lifetime passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

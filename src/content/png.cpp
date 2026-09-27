#include "png.h"
#include "shiny/path.h"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace {
// stb's inflate workspace can grow beyond the declared pixel count on malformed
// input. Bound all its allocations as well as file/output bytes. No allocation
// made by this allocator escapes the decode call or is freed on another thread.
struct alignas(std::max_align_t) Allocation { std::size_t bytes; };
thread_local std::size_t allocated{};
constexpr std::size_t workspace_budget=256u*1024u*1024u;
void* png_resize(void* data,std::size_t bytes) noexcept {
    auto* old=data?static_cast<Allocation*>(data)-1:nullptr;
    const auto previous=old?old->bytes:0;
    if(bytes>workspace_budget-(allocated-previous)) return nullptr;
    auto* next=static_cast<Allocation*>(std::realloc(old,sizeof(Allocation)+bytes));
    if(!next) return nullptr;
    next->bytes=bytes; allocated=allocated-previous+bytes; return next+1;
}
void png_free(void* data) noexcept {
    if(!data) return;
    auto* block=static_cast<Allocation*>(data)-1;
    allocated-=block->bytes; std::free(block);
}
}
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(bytes) png_resize(nullptr,bytes)
#define STBI_REALLOC(data,bytes) png_resize(data,bytes)
#define STBI_FREE(data) png_free(data)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

ScResult<ScImagePixels> sc_read_png(const ScImageRequest& request) try {
    if(request.width<1||request.width>8192||request.height<1||request.height>8192)
        return std::unexpected("PNG dimensions must be 1..8192: "+request.path);
    const auto charge=static_cast<std::size_t>(request.width)*static_cast<std::size_t>(request.height)*4;
    if(charge>ScContentLoader::image_budget) return std::unexpected("PNG exceeds decoded image budget: "+request.path);
    std::ifstream file(sc_path(request.path),std::ios::binary|std::ios::ate);
    if(!file) return std::unexpected("cannot open PNG: "+request.path);
    const auto size=file.tellg();
    if(size<33||size>32*1024*1024) return std::unexpected("PNG file must be 33 bytes..32 MiB: "+request.path);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()),size);
    if(!file||file.peek()!=std::char_traits<char>::eof()) return std::unexpected("PNG changed or could not be read: "+request.path);
    if(std::memcmp(bytes.data(),"\x89PNG\r\n\x1a\n",8)) return std::unexpected("image must be PNG: "+request.path);
    int width{},height{},channels{};
    if(!stbi_info_from_memory(bytes.data(),static_cast<int>(bytes.size()),&width,&height,&channels))
        return std::unexpected("invalid PNG header: "+request.path);
    if(width!=request.width||height!=request.height)
        return std::unexpected("PNG dimensions changed since declaration: "+request.path);
    const std::unique_ptr<unsigned char,decltype(&png_free)> decoded{
        stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&width,&height,&channels,4),png_free};
    if(!decoded) return std::unexpected("PNG decode failed (invalid data or workspace budget): "+request.path);
    return ScImagePixels{width,height,{decoded.get(),decoded.get()+charge}};
} catch(const std::exception& error) { return std::unexpected("PNG read failed: "+request.path+": "+error.what()); }

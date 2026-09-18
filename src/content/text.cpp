#include "shiny/text.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <algorithm>
#include <fstream>
#include <set>

namespace {
// ASCII metrics of the pinned raylib default bitmap font (base height 10).
// Adapted from raylib rtext.c; copyright Ramon Santamaria, licenses/raylib.txt.
// Keeping the metrics here lets headless layout use the same glyph advances.
constexpr unsigned char default_widths[]={
    3,1,4,6,5,7,6,2,3,3,5,5,2,4,1,7,5,2,5,5,5,5,5,5,5,5,1,1,3,4,3,6,
    7,6,6,6,6,6,6,6,6,3,5,6,5,7,6,6,6,6,6,6,7,6,7,7,6,6,6,2,7,2,3,5,
    2,5,5,5,5,5,4,5,5,1,2,5,2,5,5,5,5,5,5,5,4,5,5,5,5,5,5,3,1,3,4
};
}

ScResult<std::vector<int>> sc_utf8(std::string_view text) {
    std::vector<int> result;
    for(size_t i=0;i<text.size();) {
        unsigned char b=static_cast<unsigned char>(text[i++]); int code=b,count=0,minimum=0;
        if(b>=0xf0&&b<=0xf4) { code=b&7; count=3; minimum=0x10000; }
        else if(b>=0xe0&&b<=0xef) { code=b&15; count=2; minimum=0x800; }
        else if(b>=0xc2&&b<=0xdf) { code=b&31; count=1; minimum=0x80; }
        else if(b>=0x80) return std::unexpected("invalid UTF-8 leading byte");
        for(int j=0;j<count;++j) {
            if(i==text.size()) return std::unexpected("truncated UTF-8 sequence");
            unsigned char next=static_cast<unsigned char>(text[i++]);
            if((next&0xc0)!=0x80) return std::unexpected("invalid UTF-8 continuation");
            code=(code<<6)|(next&63);
        }
        if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff)) return std::unexpected("invalid Unicode codepoint");
        result.push_back(code);
    }
    return result;
}
ScResult<void> sc_font_load(ScResource& resource,const std::string& root) {
    std::ifstream input(root+"/"+resource.path,std::ios::binary|std::ios::ate);
    if(!input) return std::unexpected("font unavailable: "+resource.path);
    auto length=input.tellg(); if(length<12||length>32*1024*1024) return std::unexpected("invalid font size");
    std::vector<unsigned char> bytes(static_cast<size_t>(length)); input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()),length);
    // Accept standalone sfnt fonts, with bounded table directories before stb.
    auto u16=[&](size_t at) { return (static_cast<unsigned>(bytes[at])<<8)|bytes[at+1]; };
    auto u32=[&](size_t at) { return (static_cast<uint32_t>(u16(at))<<16)|u16(at+2); };
    if(!input||(u32(0)!=0x00010000&&u32(0)!=0x4f54544f)) return std::unexpected("font must be standalone TTF/OTF");
    size_t tables=u16(4);
    if(tables==0||12+tables*16>bytes.size()) return std::unexpected("truncated font table directory");
    for(size_t i=0;i<tables;++i) {
        size_t at=12+i*16,offset=u32(at+8),size=u32(at+12);
        if(offset>bytes.size()||size>bytes.size()-offset) return std::unexpected("font table exceeds file bounds");
    }
    stbtt_fontinfo info{}; int offset=stbtt_GetFontOffsetForIndex(bytes.data(),0);
    if(offset<0||!stbtt_InitFont(&info,bytes.data(),offset)) return std::unexpected("invalid TrueType font: "+resource.path);
    auto chars=sc_utf8(resource.characters); if(!chars) return std::unexpected(chars.error());
    std::set<int> codes(chars->begin(),chars->end()); for(int c=32;c<127;++c) codes.insert(c);
    if(codes.size()>8192) return std::unexpected("font exceeds 8192 declared glyphs");
    float scale=stbtt_ScaleForPixelHeight(&info,static_cast<float>(resource.size));
    resource.glyphs.clear();
    for(int code:codes) {
        if(!stbtt_FindGlyphIndex(&info,code)) continue;
        int advance=0,bearing=0; stbtt_GetCodepointHMetrics(&info,code,&advance,&bearing);
        resource.glyphs.push_back({code,static_cast<int>(static_cast<float>(advance)*scale)});
    }
    resource.font_bytes=std::move(bytes);
    return {};
}
const ScGlyph* sc_font_glyph(const ScResource& resource,int codepoint) {
    auto at=std::lower_bound(resource.glyphs.begin(),resource.glyphs.end(),codepoint,
        [](const ScGlyph& glyph,int code){ return glyph.codepoint<code; });
    if(at!=resource.glyphs.end()&&at->codepoint==codepoint) return &*at;
    if(resource.font_bytes.empty()) return nullptr;
    stbtt_fontinfo info{};
    if(!stbtt_InitFont(&info,resource.font_bytes.data(),0)||!stbtt_FindGlyphIndex(&info,codepoint)) return nullptr;
    if(resource.glyphs.size()>=8192) throw std::runtime_error("font metric cache exhausted (8192 glyphs): "+resource.name);
    int advance=0,bearing=0;
    stbtt_GetCodepointHMetrics(&info,codepoint,&advance,&bearing);
    const float scale=stbtt_ScaleForPixelHeight(&info,static_cast<float>(resource.size));
    at=resource.glyphs.insert(at,{codepoint,static_cast<int>(static_cast<float>(advance)*scale)});
    return &*at;
}
ScTextLayout sc_text_layout(const ScWorld* world,std::string_view text,float size,std::string_view font,float wrap,int align) {
    ScTextLayout out; auto codes=sc_utf8(text); if(!codes) { out.missing=true; return out; }
    float x=0,y=0; size_t line=0;
    auto finish=[&]() {
        out.width=std::max(out.width,x);
        float offset=wrap>0&&align?std::max(0.0f,wrap-x)*(align==1?.5f:1):0;
        for(size_t j=line;j<out.letters.size();++j) out.letters[j].x+=offset;
        line=out.letters.size(); x=0; y+=size;
    };
    for(int code:*codes) {
        if(code=='\n') { finish(); continue; }
        const ScResource* selected=nullptr; const ScGlyph* glyph=nullptr;
        auto find=[&](const ScResource& r) {
            if(r.type!="font") return;
            if(auto found=sc_font_glyph(r,code)) { selected=&r; glyph=found; }
        };
        for(const auto& r:world->resources) if(r.name==font) find(r);
        if(!glyph) for(const auto& r:world->resources) { find(r); if(glyph) break; }
        if(!glyph&&(code<32||code>126)) { out.missing=true; code='?'; }
        float advance=glyph?static_cast<float>(glyph->advance)*size/static_cast<float>(selected->size)+1:
            static_cast<float>(default_widths[code-32])*size/10+1;
        if(wrap>0&&x>0&&x+advance>wrap) finish();
        out.letters.push_back({code,x,y,selected}); x+=advance;
    }
    finish(); out.height=y; return out;
}

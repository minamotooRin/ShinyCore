#include "../src/platform/ime_text.h"
#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"ime:%d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    ScDeviceInput input;
    const unsigned char target[]={0,1,1,0,0};
    CHECK(sc_ime_text(input,u"A\U0001f600\u4e2dB",3,target));
    CHECK(std::string(input.composition.data())=="A\xf0\x9f\x98\x80\xe4\xb8\xad" "B");
    CHECK(input.composition_edit.cursor==6&&input.composition_edit.start==2&&input.composition_edit.finish==6);
    CHECK(sc_ime_text(input,u"A\U0001f600\u4e2dB",2,target));
    CHECK(input.composition_edit.cursor==2); // Interior surrogate cursor snaps backward.
    const unsigned char split[]={0,0,1,0,0};
    CHECK(sc_ime_text(input,u"A\U0001f600\u4e2dB",99,split));
    CHECK(input.composition_edit.cursor==10&&input.composition_edit.start==2&&input.composition_edit.finish==6);
    CHECK(sc_ime_text(input,u"e\u0301",-1,{}));
    CHECK(input.composition_edit.cursor==4&&input.composition_edit.start==4&&input.composition_edit.finish==4);
    CHECK(sc_ime_text(input,{},-1,{})); CHECK(input.composition_edit.cursor==1);
    const char16_t bad[]={0xd800}; CHECK(!sc_ime_text(input,{bad,1},0,{}));
    CHECK(!input.composition[0]&&input.composition_edit.cursor==0);
    CHECK(!sc_ime_text(input,std::u16string(1500,u'\u4e2d'),0,{}));
    CHECK(!sc_ime_text(input,std::u16string(2048,u'a'),0,{}));
    CHECK(!sc_ime_text(input,std::u16string_view(u"a\0b",3),0,{}));
    CHECK(sc_ime_text(input,std::u16string(1365,u'\u4e2d'),-1,{}));
    CHECK(input.composition_edit.cursor==4096);
    const unsigned char kinds[]={0,1,2,3,4,5};
    const std::uint32_t clauses[]={0,1,2,3,4,5,6};
    CHECK(sc_ime_text(input,u"abcdef",2,kinds,clauses));
    CHECK(input.composition_segment_count==6&&!input.composition_segments_truncated);
    for(std::size_t i=0;i<6;++i) {
        const auto& segment=input.composition_segments[i];
        CHECK(segment.start==i+1&&segment.finish==i+2&&segment.kind==i);
    }
    CHECK(input.composition_edit.start==2&&input.composition_edit.finish==3);
    const unsigned char converted[]={2,2,2,2};
    const std::uint32_t split_clauses[]={0,2,4},bad_clauses[]={0,4,2};
    CHECK(sc_ime_text(input,u"abcd",-1,converted,split_clauses));
    CHECK(input.composition_segment_count==2&&input.composition_segments[0].finish==3&&input.composition_segments[1].start==3);
    CHECK(sc_ime_text(input,u"abcd",-1,converted,bad_clauses));
    CHECK(input.composition_segment_count==1&&input.composition_segments[0].finish==5);
    const std::uint32_t surrogate_clause[]={0,2,5};
    CHECK(sc_ime_text(input,u"A\U0001f600\u4e2dB",2,target,surrogate_clause));
    CHECK(input.composition_segments[1].start==2&&input.composition_segments[1].finish==6);
    std::array<unsigned char,129> many{};
    for(std::size_t i=0;i<many.size();++i) many[i]=static_cast<unsigned char>(i%2?2:0);
    CHECK(sc_ime_text(input,std::u16string(129,u'a'),-1,many));
    CHECK(input.composition_segments_truncated&&input.composition_segment_count==0);
    CHECK(std::string(input.composition.data()).size()==129&&input.composition_edit.cursor==130);
    CHECK(sc_ime_text(input,{},-1,{}));
    CHECK(!input.composition_segments_truncated&&input.composition_segment_count==0);
    std::puts("IME conversion: UTF-16 positions, clause/attribute runs, target range, malformed input and capacity passed");
}

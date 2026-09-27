#include "ime_text.h"
#include <algorithm>

bool sc_ime_text(ScDeviceInput& input,std::u16string_view text,int cursor,std::span<const unsigned char> attributes,std::span<const std::uint32_t> clauses) noexcept {
    input.composition.fill(0); input.composition_edit={};
    input.composition_segment_count=0; input.composition_segments_truncated=false;
    if(text.size()>2047) return false;
    std::array<char,4096> utf8{};
    std::array<int,2048> offsets{};
    std::size_t bytes=0;
    for(std::size_t i=0;i<text.size();) {
        const auto first=i; unsigned int code=text[i++];
        if(code>=0xd800&&code<=0xdbff) {
            if(i==text.size()||text[i]<0xdc00||text[i]>0xdfff) return false;
            code=0x10000+((code-0xd800)<<10)+(text[i++]-0xdc00);
        } else if(!code||(code>=0xdc00&&code<=0xdfff)) return false;
        const std::size_t count=code<0x80?1:code<0x800?2:code<0x10000?3:4;
        if(bytes+count>=utf8.size()) return false;
        for(auto at=first;at<i;++at) offsets[at]=static_cast<int>(bytes)+1;
        if(count==1) utf8[bytes++]=static_cast<char>(code);
        else {
            utf8[bytes++]=static_cast<char>((count==2?0xc0:count==3?0xe0:0xf0)|(code>>(6*(count-1))));
            for(std::size_t shift=count-1;shift>0;--shift)
                utf8[bytes++]=static_cast<char>(0x80|((code>>(6*(shift-1)))&0x3f));
        }
        offsets[i]=static_cast<int>(bytes)+1;
    }
    offsets[text.size()]=static_cast<int>(bytes)+1;
    auto target=[&](std::size_t i) { return i<attributes.size()&&(attributes[i]==1||attributes[i]==3); };
    std::size_t start=text.size(),finish=start;
    for(std::size_t i=0;i<text.size();++i) if(target(i)) {
        start=i; finish=i+1;
        while(finish<text.size()&&target(finish)) ++finish;
        break;
    }
    bool valid_clauses=clauses.size()>=2&&clauses.front()==0&&clauses.back()==text.size();
    for(std::size_t i=1;valid_clauses&&i<clauses.size();++i)
        valid_clauses=clauses[i]>clauses[i-1]&&clauses[i]<=text.size();
    std::size_t clause=1;
    for(std::size_t i=0;i<text.size();) {
        const auto first=i++;
        if(text[first]>=0xd800&&text[first]<=0xdbff) ++i;
        unsigned char kind=first<attributes.size()&&attributes[first]<6?attributes[first]:0;
        // An interior surrogate attribute never splits the scalar; a target takes precedence.
        if(i-first==2&&target(first+1)) kind=attributes[first+1];
        bool boundary=first==0;
        if(valid_clauses) while(clause<clauses.size()&&clauses[clause]<i) { boundary=true; ++clause; }
        auto* previous=input.composition_segment_count?&input.composition_segments[input.composition_segment_count-1]:nullptr;
        if(previous&&!boundary&&previous->kind==kind) previous->finish=static_cast<std::uint16_t>(offsets[i]);
        else {
            if(input.composition_segment_count==SC_COMPOSITION_SEGMENTS) {
                input.composition_segment_count=0; input.composition_segments_truncated=true; break;
            }
            input.composition_segments[input.composition_segment_count++]={static_cast<std::uint16_t>(offsets[first]),static_cast<std::uint16_t>(offsets[i]),kind};
        }
    }
    // Do not split a surrogate pair even if a platform supplies an interior boundary.
    if(finish<text.size()&&text[finish]>=0xdc00&&text[finish]<=0xdfff) ++finish;
    const auto caret=cursor<0?text.size():std::min(static_cast<std::size_t>(cursor),text.size());
    input.composition=utf8;
    input.composition_edit={offsets[caret],offsets[start],offsets[finish]};
    return true;
}

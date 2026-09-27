#include "panel.h"
#include "inspect.h"
#include "shiny/script.h"
#include <raylib.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <cmath>
#include <stdexcept>

namespace {
using V=ScValue;
std::string label(const V& row,const char* key) {
    const auto* value=row.get(key); if(!value) return {};
    return std::holds_alternative<std::string>(value->data)?value->text():sc_json_write(*value);
}
const V::Array& rows(const V& value) { return std::get<V::Array>(value.get("items")->data); }
constexpr const char* sections[]={"off","metrics","entities","ui","resources"};
}
ScValue ScDebugPanel::configure(const ScValue& request) {
    int section=section_; auto offset=offset_; auto tree=named_tree_;
    if(const auto* value=request.get("section")) {
        const auto name=value->text(); section=-1;
        for(int i=0;i<5;++i) if(name==sections[i]) section=i;
        if(section<0) throw std::runtime_error("panel section must be off, metrics, entities, ui or resources");
        if(section!=section_) { offset=0; tree.clear(); }
    }
    if(const auto* value=request.get("offset")) {
        const auto n=value->number(NAN);
        if(!std::isfinite(n)||n<0||n>65532||std::floor(n)!=n) throw std::runtime_error("invalid panel offset");
        offset=static_cast<std::size_t>(n);
    }
    if(const auto* value=request.get("tree")) {
        const auto* name=std::get_if<std::string>(&value->data);
        if(section!=3||!name||name->empty()||name->size()>128||name->find('\0')!=std::string::npos)
            throw std::runtime_error("panel tree requires ui section and a name of 1..128 bytes");
        tree=*name;
    }
    section_=section; offset_=offset; named_tree_=std::move(tree);
    return V{V::Object{{"section",V{std::string(sections[section_])}},
        {"offset",V{double(offset_)}},{"tree",V{named_tree_}}}};
}
void ScDebugPanel::draw(ScScript& script,float interval,bool cycle,bool previous,bool next,bool next_tree,Text draw_text) {
    if(cycle) { section_=(section_+1)%5; offset_=tree_=0; named_tree_.clear(); }
    if(!section_) return;
    constexpr size_t page_size=12;
    if(previous) offset_=offset_>=page_size?offset_-page_size:0;
    if(next) offset_=std::min(offset_+page_size,size_t(65532));
    if(next_tree) { ++tree_; offset_=0; named_tree_.clear(); }
    const int width=std::min(640,GetScreenWidth()-16),height=std::min(330,GetScreenHeight()-16);
    if(width<32||height<32) return;
    BeginScissorMode(8,8,width,height);
    DrawRectangle(8,8,width,height,Color{10,17,29,245});
    int y=18;
    auto line=[&](const std::string& text,Color color=Color{220,230,244,255}) {
        draw_text(script,text,18,y,color); y+=20;
    };
    try {
        line(std::string("Inspector / ")+sections[section_],Color{102,217,176,255});
        line("F4 section/off | F6/F7 page | F8 UI tree",Color{145,166,194,255});
        V request{V::Object{{"offset",V{double(offset_)}},{"limit",V{double(page_size)}}}};
        if(section_==1) {
            const auto metrics=sc_debug_inspect(script,"metrics",V{});
            for(const auto* key:{"entities","particles","projectiles","draw_commands","resources","lua_bytes","font_bytes","cached_glyphs"})
                line(std::string(key)+": "+label(metrics,key));
            char timing[96]; std::snprintf(timing,sizeof timing,"Display interval: %.2f ms (includes pacing)",double(interval)*1000);
            line(timing);
        } else {
            const char* section=section_==2?"entities":section_==3?"ui":"resources";
            if(section_==3) {
                if(!named_tree_.empty()) {
                    line("Tree: "+named_tree_);
                    std::get<V::Object>(request.data).emplace("tree",V{named_tree_});
                } else {
                    const auto trees=sc_debug_ui_inspect(script,V{});
                    const auto& available=rows(trees);
                    if(available.empty()) line("No registered UI trees");
                    else {
                        const auto& tree=available[tree_%available.size()];
                        const auto name=label(tree,"name");
                        line("Tree: "+name+" | focus: "+label(tree,"focus"));
                        std::get<V::Object>(request.data).emplace("tree",V{name});
                    }
                }
            }
            const auto data=sc_debug_inspect(script,section,request);
            line("Offset "+std::to_string(offset_)+" / "+label(data,"total"),Color{145,166,194,255});
            for(const auto& row:rows(data)) {
                if(section_==2) line(label(row,"id")+" "+label(row,"tag")+" ("+label(row,"x")+", "+label(row,"y")+")");
                else if(section_==4) line(label(row,"name")+" ["+label(row,"type")+"] "+label(row,"path"));
                else {
                    std::string state=label(row,"id")+" ["+label(row,"kind")+"]";
                    if(const auto* value=row.get("focused");value&&value->boolean()) state+=" focus";
                    if(const auto* value=row.get("visible");value&&!value->boolean()) state+=" hidden";
                    if(const auto* value=row.get("enabled");value&&!value->boolean()) state+=" disabled";
                    if(row.get("scroll_max")) state+=" scroll="+label(row,"scroll")+"/"+label(row,"scroll_max");
                    if(const auto* value=row.get("scroll_capture");value&&value->boolean()) state+=" dragging";
                    if(const auto* value=row.get("tooltip_visible");value&&value->boolean()) state+=" tooltip";
                    if(row.get("selected_item_id")) state+=" selected="+label(row,"selected_item_id");
                    line(state);
                }
            }
        }
    } catch(const std::exception& error) {
        // A font/cache failure in diagnostics must not fail the game or retry that font.
        DrawText(error.what(),18,y,10,Color{255,170,145,255});
    }
    EndScissorMode();
}

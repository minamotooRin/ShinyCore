#include "shiny/project.h"
#include "shiny/text.h"
#include "shiny/physics.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <stdexcept>

namespace {
const ScValue empty;
const ScValue& at(const ScValue& v,std::string_view key) { auto p=v.get(key); return p?*p:empty; }
double number(const ScValue& value,double fallback=0) {
    if(&value==&empty) return fallback;
    auto result=std::get_if<double>(&value.data);
    if(!result) throw std::runtime_error("expected number");
    return *result;
}
bool boolean(const ScValue& value,bool fallback=false) {
    if(&value==&empty) return fallback;
    auto result=std::get_if<bool>(&value.data);
    if(!result) throw std::runtime_error("expected boolean");
    return *result;
}
int integer(const ScValue& v,int lo,int hi) {
    auto p=std::get_if<double>(&v.data);
    if(!p||*p<lo||*p>hi||std::floor(*p)!=*p) throw std::runtime_error("expected bounded integer");
    return static_cast<int>(*p);
}
const ScValue::Array& array(const ScValue& v) { auto p=std::get_if<ScValue::Array>(&v.data); if(!p) throw std::runtime_error("expected array"); return *p; }
float audio_duration(const std::string& path,bool music) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    auto length=input.tellg(); if(length<12||length>128*1024*1024) throw std::runtime_error("invalid audio file size: "+path);
    std::vector<unsigned char> bytes(static_cast<size_t>(length)); input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(bytes.data()),length)) throw std::runtime_error("cannot read audio: "+path);
    auto u32=[&](size_t offset) { if(offset+4>bytes.size()) throw std::runtime_error("truncated audio header"); return static_cast<uint32_t>(bytes[offset])|(static_cast<uint32_t>(bytes[offset+1])<<8)|(static_cast<uint32_t>(bytes[offset+2])<<16)|(static_cast<uint32_t>(bytes[offset+3])<<24); };
    if(!music) {
        if(std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4)) throw std::runtime_error("sound requires WAV: "+path);
        uint32_t rate=0,data=0;
        for(size_t at=12;at+8<=bytes.size();) {
            uint32_t size=u32(at+4); if(size>bytes.size()-at-8) throw std::runtime_error("truncated WAV chunk");
            if(!std::memcmp(bytes.data()+at,"fmt ",4)) {
                if(size<16) throw std::runtime_error("invalid WAV format");
                unsigned format=bytes[at+8]|(static_cast<unsigned>(bytes[at+9])<<8);
                unsigned channels=bytes[at+10]|(static_cast<unsigned>(bytes[at+11])<<8);
                unsigned bits=bytes[at+22]|(static_cast<unsigned>(bytes[at+23])<<8);
                uint32_t samples=u32(at+12); rate=u32(at+16);
                if(channels<1||channels>2||samples<8000||samples>192000||
                    (format!=1&&format!=3)||(format==3&&bits!=32)||
                    (bits!=8&&bits!=16&&bits!=24&&bits!=32)||rate!=samples*channels*(bits/8))
                    throw std::runtime_error("WAV requires mono/stereo PCM8/16/24/32 or float32, 8..192 kHz");
            }
            if(!std::memcmp(bytes.data()+at,"data",4)) data=size;
            at+=8+static_cast<size_t>(size)+(size&1);
        }
        if(!rate||!data) throw std::runtime_error("WAV needs format and sample data");
        return static_cast<float>(data)/static_cast<float>(rate);
    }
    uint32_t rate=0; uint64_t granule=0;
    for(size_t at=0;at<bytes.size();) {
        if(at+27>bytes.size()||std::memcmp(bytes.data()+at,"OggS",4)||bytes[at+4]!=0) throw std::runtime_error("music requires Ogg Vorbis pages");
        uint64_t time=static_cast<uint64_t>(u32(at+6))|(static_cast<uint64_t>(u32(at+10))<<32);
        if(time!=UINT64_MAX) granule=std::max(granule,time);
        size_t count=bytes[at+26],size=0;
        if(at+27+count>bytes.size()) throw std::runtime_error("truncated Ogg page");
        for(size_t j=0;j<count;++j) size+=bytes[at+27+j];
        size_t start=at+27+count;
        if(start+size>bytes.size()) throw std::runtime_error("truncated Ogg payload");
        if(!rate&&size>=16&&bytes[start]==1&&!std::memcmp(bytes.data()+start+1,"vorbis",6)) rate=u32(start+12);
        at=start+size;
    }
    if(!rate||!granule) throw std::runtime_error("missing Ogg Vorbis duration");
    return static_cast<float>(static_cast<double>(granule)/rate);
}
std::string relative(const std::string& base,const std::string& input) {
    if(input.empty()||input.find('\0')!=std::string::npos||input.find_first_of("\\:")!=std::string::npos) throw std::runtime_error("invalid asset path: "+input);
    auto path=(std::filesystem::path(base).parent_path()/input).lexically_normal();
    if(path.is_absolute()||path.generic_string()==".."||path.generic_string().starts_with("../")) throw std::runtime_error("asset escapes project: "+input);
    if(path.generic_string().size()>127) throw std::runtime_error("asset path exceeds 127 bytes");
    return path.generic_string();
}
ScValue file(const std::string& root,const std::string& path) {
    auto parsed=sc_json_file(root+"/"+path,8*1024*1024);
    if(!parsed) throw std::runtime_error(path+": "+parsed.error());
    return std::move(*parsed);
}
std::string property(const ScValue& v,const std::string& name,std::string fallback={}) {
    auto properties=v.get("properties"); if(!properties) return fallback;
    for(const auto& p:array(*properties)) if(at(p,"name").text()==name) {
        const auto* value=std::get_if<std::string>(&at(p,"value").data);
        if(!value) throw std::runtime_error("property "+name+" must be a string");
        return *value;
    }
    return fallback;
}
void tileset(ScWorld* w,const ScValue& record,const std::string& root,const std::string& map_path) {
    int first=integer(at(record,"firstgid"),1,0x0fffffff);
    std::string path=map_path; ScValue set=record;
    if(auto source=record.get("source")) { path=relative(map_path,source->text()); set=file(root,path); }
    auto image=relative(path,at(set,"image").text());
    if(!std::filesystem::is_regular_file(std::filesystem::path(root)/image)) throw std::runtime_error("tileset image missing: "+image);
    int width=integer(at(set,"tilewidth"),1,256),height=integer(at(set,"tileheight"),1,256);
    int columns=integer(at(set,"columns"),1,8192),count=integer(at(set,"tilecount"),1,65536);
    if(width!=w->map.tile_size||height!=w->map.tile_size) throw std::runtime_error("tileset tiles must match square map cells");
    if(set.get("tileoffset")) {
        const auto& offset=at(set,"tileoffset");
        if(number(at(offset,"x"))!=0||number(at(offset,"y"))!=0) throw std::runtime_error("tileset offsets unsupported");
    }
    if(at(set,"objectalignment").text("unspecified")!="unspecified") throw std::runtime_error("tileset object alignment unsupported");
    int margin=set.get("margin")?integer(at(set,"margin"),0,8192):0,spacing=set.get("spacing")?integer(at(set,"spacing"),0,256):0;
    if(margin<0||spacing<0||margin>8192||spacing>256||w->tile_graphics.size()+static_cast<size_t>(count)>65536) throw std::runtime_error("invalid tileset spacing or capacity");
    for(int i=0;i<count;++i) w->tile_graphics.push_back({static_cast<uint32_t>(first+i),image,margin+(i%columns)*(width+spacing),margin+(i/columns)*(height+spacing),width,height});
    if(auto tiles=set.get("tiles")) for(const auto& tile:array(*tiles)) {
        int id=integer(at(tile,"id"),0,count-1); auto& g=w->tile_graphics[w->tile_graphics.size()-static_cast<size_t>(count)+static_cast<size_t>(id)];
        auto collision=property(tile,"collision","empty");
        if(collision=="solid") g.collision='#'; else if(collision=="one_way") g.collision='=';
        else if(collision!="empty") throw std::runtime_error("unknown collision property: "+collision);
        if(auto group=tile.get("objectgroup")) {
            const auto& shapes=array(at(*group,"objects"));
            if(shapes.size()!=1) throw std::runtime_error("tile collision requires one convex polygon");
            const auto& shape=shapes[0]; const auto& points=array(at(shape,"polygon"));
            if(number(at(shape,"rotation"))!=0) throw std::runtime_error("rotated tile collision unsupported");
            if(points.size()<3||points.size()>8) throw std::runtime_error("tile polygon requires 3..8 vertices");
            g.vertex_count=static_cast<int>(points.size()); g.collision='#';
            for(size_t j=0;j<points.size();++j) {
                g.vertices[2*j]=static_cast<float>(number(at(shape,"x"))+number(at(points[j],"x")));
                g.vertices[2*j+1]=static_cast<float>(number(at(shape,"y"))+number(at(points[j],"y")));
            }
            if(!sc_physics_polygon_valid(g.vertices.data(),g.vertex_count)) throw std::runtime_error("tile collision must be a distinct convex polygon");
        }
        if(tile.get("animation")) throw std::runtime_error("Tiled tile animations unsupported; use Lua animation clips");
    }
}
}
const ScTileGraphic* sc_tile_graphic(const ScWorld* w,std::uint32_t gid) {
    gid&=0x0fffffffu;
    auto it=std::lower_bound(w->tile_graphics.begin(),w->tile_graphics.end(),gid,[](const ScTileGraphic& a,uint32_t b){ return a.gid<b; });
    return it!=w->tile_graphics.end()&&it->gid==gid?&*it:nullptr;
}
ScResult<void> sc_project_tiles(ScWorld* w) {
    try {
        std::vector<ScTerrainShape> shapes;
        for(const auto& layer:w->layers) for(size_t i=0;i<layer.cells.size();++i) {
            uint32_t gid=layer.cells[i]; if(!gid) continue;
            if(gid&0x10000000u) throw std::runtime_error("hexagonal GID rotation unsupported");
            const auto* g=sc_tile_graphic(w,gid); if(!g) throw std::runtime_error("unknown tile GID in layer "+layer.name);
            if(g->collision=='.') continue;
            if(shapes.size()>=SC_MAX_TILES) throw std::runtime_error("terrain collider capacity exceeded");
            ScTerrainShape s;
            s.x=static_cast<float>(i%static_cast<size_t>(w->map.width)*static_cast<size_t>(w->map.tile_size))+layer.x;
            s.y=static_cast<float>(i/static_cast<size_t>(w->map.width)*static_cast<size_t>(w->map.tile_size))+layer.y;
            s.w=static_cast<float>(g->w); s.h=static_cast<float>(g->h); s.one_way=g->collision=='=';
            s.vertices=g->vertices; s.vertex_count=g->vertex_count;
            for(int j=0;j<s.vertex_count;++j) {
                float& x=s.vertices[2*j]; float& y=s.vertices[2*j+1];
                if(gid&0x20000000u) std::swap(x,y);
                if(gid&0x80000000u) x=s.w-x;
                if(gid&0x40000000u) y=s.h-y;
            }
            if(!shapes.empty()&&!s.vertex_count) {
                auto& previous=shapes.back();
                if(!previous.vertex_count&&previous.one_way==s.one_way&&previous.y==s.y&&previous.h==s.h&&previous.x+previous.w==s.x) { previous.w+=s.w; continue; }
            }
            shapes.push_back(s);
        }
        w->terrain_shapes=std::move(shapes); ++w->terrain_revision; return {};
    } catch(const std::exception& e) { return std::unexpected(e.what()); }
}
ScResult<ScValue> sc_project_map(ScWorld* w,const std::string& root,const std::string& path) {
    try {
        auto map=file(root,path);
        if(at(map,"orientation").text()!="orthogonal"||boolean(at(map,"infinite"))) throw std::runtime_error("only finite orthogonal maps are supported");
        if(at(map,"renderorder").text("right-down")!="right-down") throw std::runtime_error("only right-down render order is supported");
        int width=integer(at(map,"width"),1,SC_MAX_TILES),height=integer(at(map,"height"),1,SC_MAX_TILES/width);
        int tile=integer(at(map,"tilewidth"),1,256);
        if(integer(at(map,"tileheight"),1,256)!=tile) throw std::runtime_error("map cells must be square");
        w->map.width=width; w->map.height=height; w->map.tile_size=tile; w->map.tiles.fill('.');
        w->layers.clear(); w->tile_graphics.clear();
        for(const auto& set:array(at(map,"tilesets"))) tileset(w,set,root,path);
        std::sort(w->tile_graphics.begin(),w->tile_graphics.end(),[](const auto& a,const auto& b){ return a.gid<b.gid; });
        for(size_t i=1;i<w->tile_graphics.size();++i) if(w->tile_graphics[i-1].gid==w->tile_graphics[i].gid) throw std::runtime_error("overlapping tileset GID ranges");
        ScValue::Array objects; int order=0;
        for(const auto& layer:array(at(map,"layers"))) {
            std::string name=at(layer,"name").text(),type=at(layer,"type").text();
            if(name.empty()||name.size()>128) throw std::runtime_error("layer requires a name of 1..128 bytes");
            if(number(at(layer,"x"))!=0||number(at(layer,"y"))!=0||number(at(layer,"parallaxx"), 1)!=1||number(at(layer,"parallaxy"), 1)!=1||layer.get("tintcolor"))
                throw std::runtime_error("layer tile offsets, parallax and tint unsupported: "+name);
            if(type=="tilelayer") {
                if(w->layers.size()>=16||layer.get("encoding")||layer.get("compression")) throw std::runtime_error("unsupported layer encoding or layer capacity");
                ScLayer output; output.name=name; output.order=order++;
                for(const auto& existing:w->layers) if(existing.name==name) throw std::runtime_error("duplicate tile layer name: "+name);
                if((layer.get("width")&&integer(at(layer,"width"),1,SC_MAX_TILES)!=width)||(layer.get("height")&&integer(at(layer,"height"),1,SC_MAX_TILES)!=height)) throw std::runtime_error("layer dimensions differ from map");
                output.visible=boolean(at(layer,"visible"), true); output.opacity=static_cast<float>(number(at(layer,"opacity"), 1));
                output.x=static_cast<float>(number(at(layer,"offsetx"))); output.y=static_cast<float>(number(at(layer,"offsety")));
                if(output.opacity<0||output.opacity>1||std::fabs(output.x)>1000000||std::fabs(output.y)>1000000) throw std::runtime_error("invalid layer opacity or offset");
                const auto& cells=array(at(layer,"data"));
                if(cells.size()!=static_cast<size_t>(width*height)) throw std::runtime_error("layer size mismatch: "+name);
                for(const auto& cell:cells) {
                    double gid=cell.number(-1); if(gid<0||gid>UINT32_MAX||std::floor(gid)!=gid) throw std::runtime_error("invalid GID in "+name);
                    output.cells.push_back(static_cast<uint32_t>(gid));
                }
                w->layers.push_back(std::move(output));
            } else if(type=="objectgroup") {
                if(number(at(layer,"offsetx"))!=0||number(at(layer,"offsety"))!=0) throw std::runtime_error("object layer offsets unsupported");
                for(const auto& object:array(at(layer,"objects"))) {
                    if(object.get("template")) throw std::runtime_error("Tiled object templates unsupported");
                    if(objects.size()>=SC_MAX_TILES) throw std::runtime_error("map object capacity exceeded");
                    objects.push_back(object);
                }
            } else throw std::runtime_error("unsupported layer type: "+type);
        }
        auto rebuilt=sc_project_tiles(w); if(!rebuilt) throw std::runtime_error(rebuilt.error());
        return ScValue(std::move(objects));
    } catch(const std::exception& e) { return std::unexpected(path+": "+e.what()); }
}
ScResult<void> sc_project_resources(ScWorld* w,const ScValue& project,const std::string& root) {
    try {
        w->resources.clear(); auto resources=project.get("resources"); if(!resources) return {};
        const auto* object=std::get_if<ScValue::Object>(&resources->data); if(!object) throw std::runtime_error("resources must be an object");
        if(object->size()>128) throw std::runtime_error("resource capacity exceeded");
        for(const auto& [name,v]:*object) {
            if(name.empty()||name.size()>127) throw std::runtime_error("resource name requires 1..127 bytes");
            const auto* fields=std::get_if<ScValue::Object>(&v.data);
            if(!fields) throw std::runtime_error("resource declaration must be an object");
            for(const auto& [key,value]:*fields) {
                (void)value;
                if(key!="type"&&key!="path"&&key!="characters"&&key!="size") throw std::runtime_error("unknown resource field: "+key);
            }
            ScResource r; r.name=name; r.type=at(v,"type").text(); r.path=relative("project.lua",at(v,"path").text());
            if(r.type!="image"&&r.type!="sound"&&r.type!="music"&&r.type!="font") throw std::runtime_error("unknown resource type: "+r.type);
            if(!std::filesystem::is_regular_file(std::filesystem::path(root)/r.path)) throw std::runtime_error("resource missing: "+r.path);
            r.characters=at(v,"characters").text(); r.size=v.get("size")?integer(at(v,"size"),1,128):16;
            if(v.get("characters")&&!std::holds_alternative<std::string>(at(v,"characters").data)) throw std::runtime_error("font characters must be UTF-8 text");
            if(r.type!="font"&&(v.get("characters")||v.get("size"))) throw std::runtime_error("characters and size require a font resource");
            if(r.type=="font") { auto loaded=sc_font_load(r,root); if(!loaded) throw std::runtime_error(loaded.error()); }
            if(r.type=="sound"||r.type=="music") r.duration=audio_duration(root+"/"+r.path,r.type=="music");
            w->resources.push_back(std::move(r));
        }
        return {};
    } catch(const std::exception& e) { return std::unexpected(e.what()); }
}

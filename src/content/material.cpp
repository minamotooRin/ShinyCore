#include "shiny/material.h"
#include "shiny/core.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
const ScValue::Object& object(const ScValue& v) {
    const auto* o=std::get_if<ScValue::Object>(&v.data);
    if(!o) throw std::runtime_error("expected a plain object");
    return *o;
}
const ScResource& resource(const ScWorld& world,const ScValue& value,const char* type) {
    const auto* name=std::get_if<std::string>(&value.data);
    if(name) for(const auto& r:world.resources) if(r.name==*name&&r.type==type) return r;
    throw std::runtime_error(std::string("expected declared ")+type+" resource");
}
float scalar(const ScValue& value) {
    const auto* n=std::get_if<double>(&value.data);
    if(!n||!std::isfinite(*n)||std::abs(*n)>1e6) throw std::runtime_error("uniform number must be finite within +/-1000000");
    return static_cast<float>(*n);
}
void uniform_value(const ScWorld& world,ScUniform& uniform,const ScValue& value) {
    if(uniform.type==ScUniformType::texture) {
        const auto& r=resource(world,value,"image");
        std::snprintf(uniform.texture,sizeof uniform.texture,"%s",r.path.c_str());
    } else if(uniform.type==ScUniformType::boolean) {
        const auto* v=std::get_if<bool>(&value.data); if(!v) throw std::runtime_error("uniform requires boolean"); uniform.integer=*v?1:0;
    } else if(uniform.type==ScUniformType::integer) {
        const auto* n=std::get_if<double>(&value.data);
        if(!n||!std::isfinite(*n)||*n<-2147483648.||*n>2147483647.||std::floor(*n)!=*n) throw std::runtime_error("uniform requires int32");
        uniform.integer=static_cast<int>(*n);
    } else if(uniform.type==ScUniformType::scalar) uniform.values[0]=scalar(value);
    else {
        const auto* values=std::get_if<ScValue::Array>(&value.data);
        const auto count=static_cast<size_t>(uniform.type)+1;
        if(!values||values->size()!=count) throw std::runtime_error("uniform vector length does not match type");
        for(size_t i=0;i<count;++i) uniform.values[i]=scalar((*values)[i]);
    }
}
bool identifier(std::string_view name) {
    if(name.empty()||name.size()>63||name.starts_with("gl_")) return false;
    for(size_t i=0;i<name.size();++i) {
        const char c=name[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(i&&c>='0'&&c<='9'))) return false;
    }
    return name!="texture0"&&name!="mvp"&&name!="colDiffuse"&&name!="sc_scene"&&name!="sc_resolution";
}
}
std::string sc_shader_source(const std::string& root,const std::string& relative) {
    std::ifstream file(std::filesystem::path(root)/relative,std::ios::binary|std::ios::ate);
    const auto size=file.tellg();
    if(size<=0||size>65536) throw std::runtime_error(relative+": shader source requires 1..65536 bytes");
    std::string source(static_cast<size_t>(size),'\0'); file.seekg(0);
    if(!file.read(source.data(),size)||source.find('\0')!=source.npos) throw std::runtime_error(relative+": cannot read shader text without NUL");
    return source;
}
ScResult<ScMaterialId> ScMaterials::create(const ScWorld& world,const std::string& root,const ScValue& spec) {
    try {
        const auto& fields=object(spec);
        for(const auto& [name,v]:fields) if(name!="shader"&&name!="uniforms"&&name!="postprocess") throw std::runtime_error("unknown material field: "+name);
        const auto* shader=spec.get("shader"); if(!shader) throw std::runtime_error("material.shader is required");
        size_t slot=0; while(slot<slots_.size()&&(slots_[slot].id||!generations_[slot])) ++slot;
        if(slot==slots_.size()) throw std::runtime_error("material capacity exhausted (64)");
        ScMaterial material; material.shader=resource(world,*shader,"shader").path;
        if(const auto* post_flag=spec.get("postprocess")) {
            const auto* flag=std::get_if<bool>(&post_flag->data);
            if(!flag) throw std::runtime_error("material.postprocess requires boolean");
            material.postprocess=*flag;
        }
        material.source=sc_shader_source(root,material.shader);
        if(const auto* uniforms=spec.get("uniforms")) {
            const auto& values=object(*uniforms);
            if(values.size()>32) throw std::runtime_error("uniform capacity exhausted (32)");
            size_t textures=0;
            for(const auto& [name,description]:values) {
                if(!identifier(name)) throw std::runtime_error("invalid or reserved uniform name: "+name);
                for(const auto& [key,v]:object(description)) if(key!="type"&&key!="value") throw std::runtime_error("unknown uniform field: "+name+"."+key);
                const auto* type=description.get("type"); const auto* value=description.get("value");
                if(!type||!value) throw std::runtime_error("uniform requires type/value: "+name);
                auto& uniform=material.uniforms[material.uniform_count++]; std::snprintf(uniform.name,sizeof uniform.name,"%s",name.c_str());
                const auto label=type->text();
                if(label=="float") uniform.type=ScUniformType::scalar;
                else if(label=="vec2") uniform.type=ScUniformType::vec2;
                else if(label=="vec3") uniform.type=ScUniformType::vec3;
                else if(label=="vec4") uniform.type=ScUniformType::vec4;
                else if(label=="int") uniform.type=ScUniformType::integer;
                else if(label=="bool") uniform.type=ScUniformType::boolean;
                else if(label=="texture") {
                    uniform.type=ScUniformType::texture;
                    if(++textures>(material.postprocess?3u:4u)) throw std::runtime_error("material texture capacity exhausted (image: 4, postprocess: 3)");
                }
                else throw std::runtime_error("unsupported uniform type: "+name);
                uniform_value(world,uniform,*value);
            }
        }
        material.id=(ScMaterialId{epoch_}<<32)|(ScMaterialId{generations_[slot]}<<16)|slot;
        slots_[slot]=std::move(material); return slots_[slot].id;
    } catch(const std::exception& error) { return std::unexpected(std::string("material.create: ")+error.what()); }
}
ScMaterial* ScMaterials::find(ScMaterialId id) noexcept {
    const auto slot=id&65535;
    return id&&slot<slots_.size()&&slots_[slot].id==id?&slots_[slot]:nullptr;
}
ScResult<void> ScMaterials::set(const ScWorld& world,ScMaterialId id,const ScValue& patch) {
    try {
        auto* material=find(id); if(!material) throw std::runtime_error("stale material handle");
        auto changed=material->uniforms;
        for(const auto& [name,value]:object(patch)) {
            auto it=std::find_if(changed.begin(),changed.begin()+static_cast<ptrdiff_t>(material->uniform_count),[&](const auto& u){return name==u.name;});
            if(it==changed.begin()+static_cast<ptrdiff_t>(material->uniform_count)) throw std::runtime_error("unknown uniform: "+name);
            uniform_value(world,*it,value);
        }
        material->uniforms=changed; return {};
    } catch(const std::exception& error) { return std::unexpected(std::string("material.set: ")+error.what()); }
}
ScResult<void> ScMaterials::reload(ScMaterialId id,const std::string& root) {
    try {
        auto* material=find(id); if(!material) throw std::runtime_error("stale material handle");
        if(material->revision==SC_ID_MAX) throw std::runtime_error("material revision exhausted");
        auto source=sc_shader_source(root,material->shader);
        material->source=std::move(source); ++material->revision; material->error.clear(); return {};
    } catch(const std::exception& error) { return std::unexpected(std::string("material.reload: ")+error.what()); }
}
bool ScMaterials::destroy(const ScWorld& world,ScMaterialId id) noexcept {
    auto* material=find(id); if(!material) return false;
    if(std::find(post.passes.begin(),post.passes.begin()+static_cast<ptrdiff_t>(post.count),id)!=post.passes.begin()+static_cast<ptrdiff_t>(post.count)) return false;
    for(std::size_t i=0;i<image_count_;++i) if(images_[i].material==id) return false;
    for(const auto& e:world.entities) if(e.alive) {
        const auto slot=sc_entity_slot(e.id);
        if(slot<entities_.size()&&entities_[slot].entity==e.id&&entities_[slot].material==id) return false;
    }
    *material={};
    auto& generation=generations_[id&65535];
    generation=generation==UINT16_MAX?0:static_cast<std::uint16_t>(generation+1);
    return true;
}
ScResult<void> ScMaterials::bind_entity(const ScWorld& world,std::uint64_t entity,std::optional<ScMaterialId> id) {
    const auto slot=sc_entity_slot(entity);
    if(!entity||slot>=world.entities.size()||!world.entities[slot].alive||world.entities[slot].id!=entity)
        return std::unexpected("material binding requires a live entity handle");
    if(slot>=entities_.size()) return std::unexpected("material entity binding capacity exceeded");
    if(id&&*id) {
        const auto* material=find(*id);
        if(!material||material->postprocess) return std::unexpected("entity binding requires a live surface material");
    }
    entities_[slot]=id?EntityBinding{entity,*id}:EntityBinding{}; return {};
}
ScResult<void> ScMaterials::bind_image(const ScWorld& world,std::string_view name,ScMaterialId id) {
    const ScResource* image=nullptr;
    for(const auto& resource:world.resources) if(resource.name==name&&resource.type=="image") { image=&resource; break; }
    if(!image) return std::unexpected("material binding requires a declared image resource");
    if(id) {
        const auto* material=find(id);
        if(!material||material->postprocess) return std::unexpected("image binding requires a live surface material");
    }
    std::size_t at=0; while(at<image_count_&&images_[at].path!=image->path) ++at;
    if(!id) {
        if(at<image_count_) { images_[at]=std::move(images_[--image_count_]); images_[image_count_]={}; }
        return {};
    }
    if(at==images_.size()) return std::unexpected("image material binding capacity exceeded (64)");
    ImageBinding binding{image->path,id}; // Allocate before publishing a new path or replacement.
    images_[at]=std::move(binding); if(at==image_count_) ++image_count_; return {};
}
ScMaterialId ScMaterials::image_material(std::string_view path) const noexcept {
    for(std::size_t i=0;i<image_count_;++i) if(images_[i].path==path) return images_[i].material;
    return 0;
}
ScMaterialId ScMaterials::entity_material(const ScEntity& entity) const noexcept {
    const auto slot=sc_entity_slot(entity.id);
    if(slot<entities_.size()&&entities_[slot].entity==entity.id) return entities_[slot].material;
    return image_material(entity.sprite);
}
std::size_t ScMaterials::entity_bindings(const ScWorld& world) const noexcept {
    std::size_t count=0;
    for(const auto& e:world.entities) if(e.alive) {
        const auto slot=sc_entity_slot(e.id);
        if(slot<entities_.size()&&entities_[slot].entity==e.id) ++count;
    }
    return count;
}
ScResult<void> ScMaterials::configure_post(const ScWorld& world,const ScValue& value,std::uint64_t budget_bytes) {
    try {
        if(budget_bytes>1024u*1024u*1024u) throw std::runtime_error("postprocess budget must be 0..1073741824 bytes");
        const auto* array=std::get_if<ScValue::Array>(&value.data);
        const auto* empty=std::get_if<ScValue::Object>(&value.data);
        if(!array&&(!empty||!empty->empty())) throw std::runtime_error("postprocess requires a dense pass array");
        const size_t count=array?array->size():0;
        if(count>4) throw std::runtime_error("postprocess capacity exhausted (4 passes)");
        std::array<ScMaterialId,4> passes{};
        for(size_t i=0;i<count;++i) {
            const auto* id=std::get_if<double>(&(*array)[i].data);
            if(!id||!std::isfinite(*id)||*id<1||*id>double(SC_ID_MAX)||std::floor(*id)!=*id) throw std::runtime_error("invalid postprocess material handle");
            passes[i]=static_cast<ScMaterialId>(*id);
            const auto* material=find(passes[i]);
            if(!material||!material->postprocess) throw std::runtime_error("postprocess requires live postprocess=true materials");
        }
        if(count&&(world.view_width<1||world.view_height<1||world.view_width>8192||world.view_height>8192)) throw std::runtime_error("postprocess dimensions must be 1..8192");
        const auto bytes=count?std::uint64_t(world.view_width)*std::uint64_t(world.view_height)*4*std::min(count,size_t(2)):0;
        if(bytes>budget_bytes) throw std::runtime_error("postprocess target budget exceeded (required="+std::to_string(bytes)+", budget="+std::to_string(budget_bytes)+")");
        if(post.revision==SC_ID_MAX) throw std::runtime_error("postprocess revision exhausted");
        post.passes=passes; post.count=count; post.budget_bytes=budget_bytes; post.required_bytes=bytes;
        ++post.revision; post.presented=false; post.error.clear(); return {};
    } catch(const std::exception& error) { return std::unexpected(std::string("material.postprocess: ")+error.what()); }
}
size_t ScMaterials::size() const noexcept { return static_cast<size_t>(std::count_if(slots_.begin(),slots_.end(),[](const auto& m){return m.id!=0;})); }

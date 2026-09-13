#include "shiny/state.h"
#include <yyjson.h>
#include <cmath>
#include <cstdlib>
#include "shiny/path.h"
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

const ScValue* ScValue::get(std::string_view key) const {
    auto object = std::get_if<Object>(&data);
    if (!object) return nullptr;
    auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}
std::string ScValue::text(std::string fallback) const { auto p=std::get_if<std::string>(&data); return p?*p:fallback; }
double ScValue::number(double fallback) const { auto p=std::get_if<double>(&data); return p?*p:fallback; }
bool ScValue::boolean(bool fallback) const { auto p=std::get_if<bool>(&data); return p?*p:fallback; }
namespace {
void state_shape(const ScValue& value) {
    if(std::holds_alternative<std::monostate>(value.data)) throw std::runtime_error("state cannot contain JSON null");
    if(auto object=std::get_if<ScValue::Object>(&value.data)) for(const auto& [key,item]:*object) {
        if(key.empty()||key.size()>128||key.find('\0')!=std::string::npos) throw std::runtime_error("invalid state key");
        state_shape(item);
    }
    if(auto array=std::get_if<ScValue::Array>(&value.data)) for(const auto& item:*array) state_shape(item);
}
ScValue decode(yyjson_val* value, int depth,int max_depth) {
    if (depth>max_depth) throw std::runtime_error("data exceeds nesting depth "+std::to_string(max_depth));
    if (yyjson_is_null(value)) return {};
    if (yyjson_is_bool(value)) return ScValue(yyjson_get_bool(value));
    if (yyjson_is_num(value)) {
        double n=yyjson_get_num(value);
        if (!std::isfinite(n)) throw std::runtime_error("non-finite number");
        return ScValue(n);
    }
    if (yyjson_is_str(value)) return ScValue(std::string(yyjson_get_str(value),yyjson_get_len(value)));
    if (yyjson_is_arr(value)) {
        ScValue::Array array; size_t i,max; yyjson_val* item;
        yyjson_arr_foreach(value,i,max,item) array.push_back(decode(item,depth+1,max_depth));
        return ScValue(std::move(array));
    }
    ScValue::Object object; size_t i,max; yyjson_val *key,*item;
    yyjson_obj_foreach(value,i,max,key,item) {
        std::string name(yyjson_get_str(key),yyjson_get_len(key));
        if (!object.emplace(name,decode(item,depth+1,max_depth)).second) throw std::runtime_error("duplicate JSON key: "+name);
    }
    return ScValue(std::move(object));
}
yyjson_mut_val* encode(yyjson_mut_doc* doc,const ScValue& value) {
    if (auto value1=std::get_if<bool>(&value.data)) return yyjson_mut_bool(doc,*value1);
    if (auto value2=std::get_if<double>(&value.data)) return yyjson_mut_real(doc,*value2);
    if (auto value3=std::get_if<std::string>(&value.data)) return yyjson_mut_strncpy(doc,value3->data(),value3->size());
    if (auto value4=std::get_if<ScValue::Array>(&value.data)) {
        auto array=yyjson_mut_arr(doc);
        for (const auto& item:*value4) if (!yyjson_mut_arr_append(array,encode(doc,item))) throw std::bad_alloc();
        return array;
    }
    if (auto value5=std::get_if<ScValue::Object>(&value.data)) {
        auto object=yyjson_mut_obj(doc);
        for (const auto& [key,item]:*value5)
            if (!yyjson_mut_obj_add(object,yyjson_mut_strncpy(doc,key.data(),key.size()),encode(doc,item))) throw std::bad_alloc();
        return object;
    }
    return yyjson_mut_null(doc);
}
}
ScResult<ScValue> sc_json_read(std::string_view json,std::size_t limit,int max_depth) {
    if (json.size()>limit) return std::unexpected("JSON exceeds byte limit");
    yyjson_read_err error{};
    std::unique_ptr<yyjson_doc,decltype(&yyjson_doc_free)> doc(
        yyjson_read_opts(const_cast<char*>(json.data()),json.size(),0,nullptr,&error),yyjson_doc_free);
    if (!doc) return std::unexpected("JSON byte "+std::to_string(error.pos)+": "+(error.msg?error.msg:"parse failed"));
    try { return decode(yyjson_doc_get_root(doc.get()),0,max_depth); }
    catch(const std::exception& e) { return std::unexpected(e.what()); }
}
ScResult<ScValue> sc_state_validate(const ScValue& value) {
    try {
        auto validated=sc_json_read(sc_json_write(value));
        if(!validated) return validated;
        if(!std::holds_alternative<ScValue::Object>(validated->data)) return std::unexpected("state must be an object");
        state_shape(*validated);
        return validated;
    } catch(const std::exception& e) { return std::unexpected(e.what()); }
}
ScResult<ScValue> sc_json_file(const std::string& path,std::size_t limit,int max_depth) {
    std::ifstream input(sc_path(path),std::ios::binary|std::ios::ate);
    if (!input) return std::unexpected("cannot open: "+path);
    auto length=input.tellg();
    if (length<0 || static_cast<std::uint64_t>(length)>limit) return std::unexpected("file exceeds byte limit: "+path);
    std::string bytes(static_cast<std::size_t>(length),'\0'); input.seekg(0);
    if (!input.read(bytes.data(),length)) return std::unexpected("cannot read: "+path);
    return sc_json_read(bytes,limit,max_depth);
}
std::string sc_json_write(const ScValue& value) {
    std::unique_ptr<yyjson_mut_doc,decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),yyjson_mut_doc_free);
    if (!doc) throw std::bad_alloc();
    yyjson_mut_doc_set_root(doc.get(),encode(doc.get(),value));
    size_t length=0;
    std::unique_ptr<char,decltype(&std::free)> bytes(yyjson_mut_write(doc.get(),0,&length),std::free);
    if (!bytes) throw std::bad_alloc();
    return std::string(bytes.get(),length);
}
ScResult<void> sc_atomic_write(const std::string& path,std::string_view bytes) {
    auto target=sc_path(path), temporary=target; temporary+=".tmp";
    std::error_code ec; std::filesystem::create_directories(target.parent_path(),ec);
    if (ec) return std::unexpected("cannot create save directory: "+ec.message());
    if(std::filesystem::is_directory(temporary,ec)) return std::unexpected("save temporary path is a directory");
    {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output.write(bytes.data(),static_cast<std::streamsize>(bytes.size())); output.flush(); output.close();
        if (!output) { std::filesystem::remove(temporary,ec); return std::unexpected("cannot write save file"); }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary,ec); return std::unexpected("cannot replace save file");
    }
#else
    std::filesystem::rename(temporary,target,ec);
    if (ec) { auto message=ec.message(); std::filesystem::remove(temporary,ec); return std::unexpected(message); }
#endif
    return {};
}
std::string sc_user_data_directory() {
#ifdef _WIN32
    const char* base=std::getenv("LOCALAPPDATA");
    return std::string(base?base:".")+"/ShinyCore";
#elif defined(__APPLE__)
    const char* base=std::getenv("HOME");
    return std::string(base?base:".")+"/Library/Application Support/ShinyCore";
#else
    if (const char* base=std::getenv("XDG_DATA_HOME")) return std::string(base)+"/shinycore";
    const char* base=std::getenv("HOME"); return std::string(base?base:".")+"/.local/share/shinycore";
#endif
}
std::uint64_t sc_data_hash(const ScValue& value) {
    std::uint64_t hash=14695981039346656037ull;
    for (unsigned char byte:sc_json_write(value)) { hash^=byte; hash*=1099511628211ull; }
    return hash;
}

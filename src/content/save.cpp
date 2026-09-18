#include "shiny/save.h"
#include "shiny/path.h"
#include "shiny/core.h"
#include <cmath>
#include <filesystem>

ScResult<void> sc_save_validate(const ScValue& record,std::string_view project,double data_version) {
    auto format=record.get("format"),owner=record.get("project"),version=record.get("data_version");
    auto scene=record.get("scene"),state=record.get("state");
    if(!format||format->number(-1)!=SC_SAVE_FORMAT) return std::unexpected("unsupported save format version");
    if(!owner||owner->text()!=project) return std::unexpected("save belongs to another project");
    if(!version||version->number(-1)!=data_version) return std::unexpected("unsupported save data version");
    if(!scene) return std::unexpected("save scene is missing");
    auto entry=scene->text();
    if(entry.size()>=SC_PATH_MAX||!sc_relative_path(entry)||!entry.ends_with(".lua"))
        return std::unexpected("invalid saved scene");
    if(!state) return std::unexpected("save state is missing");
    auto valid=sc_state_validate(*state);
    if(!valid) return std::unexpected(valid.error());
    if(sc_json_write(record).size()>SC_SAVE_BYTES) return std::unexpected("save record exceeds capacity");
    return {};
}
ScResult<ScValue> sc_save_read(const std::string& path,std::string_view project,double version) {
    auto primary=sc_json_file(path,SC_SAVE_BYTES,17);
    if(primary) {
        auto valid=sc_save_validate(*primary,project,version);
        if(valid) return primary;
        primary=std::unexpected(valid.error());
    }
    auto backup=sc_json_file(path+".bak",SC_SAVE_BYTES,17);
    if(backup&&sc_save_validate(*backup,project,version)) return backup;
    return std::unexpected(primary.error());
}
ScResult<void> sc_save_write(const std::string& path,const ScValue& record,std::string_view project,double version) {
    auto valid=sc_save_validate(record,project,version);
    if(!valid) return valid;
    // Never replace a known-good backup with damaged or incompatible bytes.
    if(std::filesystem::exists(sc_path(path))) {
        auto previous=sc_json_file(path,SC_SAVE_BYTES,17);
        if(previous&&sc_save_validate(*previous,project,version)) {
            auto copied=sc_atomic_write(path+".bak",sc_json_write(*previous));
            if(!copied) return copied;
        }
    }
    return sc_atomic_write(path,sc_json_write(record));
}

#pragma once
#include "shiny/state.h"
#include <array>

struct ScSettings {
    int width{1152},height{648};
    bool borderless{},smooth{},vsync{true};
    std::array<float,4> volume{1,1,1,1};
    ScValue bindings{ScValue::Object{}};
};
ScValue sc_settings_value(const ScSettings&);
ScResult<ScSettings> sc_settings_patch(const ScSettings&,const ScValue&);

// Application owner. The one optional callback is the actual window adapter;
// headless hosts validate and persist the same data without device side effects.
class ScSettingsService final {
public:
    ScSettings current;
    std::string last_error;
    ScResult<void> (*apply_native)(const ScSettings&){};
    ScResult<void> initialize(const ScValue* display,std::string_view root,std::string_view project);
    ScResult<void> apply(const ScValue& patch,bool persist=true);
    bool initialized() const noexcept { return initialized_; }
private:
    std::string path_;
    bool initialized_{};
};

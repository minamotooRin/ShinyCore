#include "entity_contract.h"
#include <cstdio>

ScValue sc_entity_contract() {
    ScValue::Array fields;
    auto field=[&](const char* name,const char* type,ScValue initial,const char* description) -> ScValue::Object& {
        fields.emplace_back(ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{false}},{"default",std::move(initial)},{"description",ScValue{std::string(description)}}});
        return std::get<ScValue::Object>(fields.back().data);
    };
    auto numbers=[&](const auto& list,const char* type) {
        for(const auto& value:list) {
            auto& out=field(value.name,type,ScValue{static_cast<double>(value.initial)},value.description);
            out.emplace("minimum",ScValue{value.minimum}); out.emplace("maximum",ScValue{value.maximum});
            out.emplace("finite",ScValue{true});
        }
    };
    numbers(SC_ENTITY_NUMBERS,"number"); numbers(SC_ENTITY_INTEGERS,"integer");
    for(const auto& value:SC_ENTITY_BOOLEANS) field(value.name,"boolean",ScValue{value.initial},value.description);
    for(const auto& value:{SC_ENTITY_TAG,SC_ENTITY_SPRITE,SC_ENTITY_IDENTITY}) {
        auto& out=field(value.name,"string",ScValue{std::string{}},value.description);
        out.emplace("maximum_bytes",ScValue{static_cast<double>(value.maximum_bytes)});
        if(value.name==std::string_view("persistent_id")) out.emplace("immutable_after_spawn",ScValue{true});
    }
    char color[10]{}; std::snprintf(color,sizeof color,"#%08X",SC_ENTITY_COLOR);
    field("color","ScColor",ScValue{std::string(color)},"Hexadecimal #RRGGBB or #RRGGBBAA; reads return #RRGGBBAA.");
    field("body","ScBody|false",ScValue{false},"Explicit body configuration; false removes the body and clears dynamic. Omitted patches retain the previous body.");
    ScValue::Array constraints;
    for(const char* text:{"Plain table with string keys; unknown fields and metatables are rejected.",
        "frame_w and frame_h must both be zero or both positive.",
        "A nonempty sprite must resolve to a project-relative path without dot segments.",
        "Body polygons require 3..8 distinct convex vertices; at most four compound shapes.",
        "Patches keep unspecified fields. Defaults apply only when spawning.",
        "Attached children cannot change world x/y/angle or acquire a body/velocity; use sc.presentation.attach to change their local pose.",
        "Explicit body configuration overrides dynamic; sprite resource names resolve to paths."}) constraints.emplace_back(std::string(text));
    return ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},{"constraints",ScValue{std::move(constraints)}},
        {"default_scope",ScValue{std::string("spawn")}}, {"unknown_fields",ScValue{std::string("reject")}},
        {"capacity",ScValue{std::string("project.limits.entities")}}}};
}

ScValue sc_entity_read_contract() {
    auto patch=sc_entity_contract();
    auto fields=std::get<ScValue::Array>(patch.get("fields")->data);
    for(auto& value:fields) {
        auto& field=std::get<ScValue::Object>(value.data);
        field["required"]=ScValue{true}; field.erase("default");
    }
    auto readonly=[&](const char* name,const char* type,const char* description) {
        fields.emplace_back(ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{true}},{"readonly",ScValue{true}},{"description",ScValue{std::string(description)}}});
    };
    readonly("id","ScEntityId","Generation-checked runtime handle; cannot be persisted or patched.");
    readonly("grounded","boolean","Ground contact from the preceding completed physics step.");
    readonly("support","ScEntityId","Supporting entity; zero for map or none.");
    readonly("normal_x","number","Ground normal X from the preceding completed physics step.");
    readonly("normal_y","number","Ground normal Y; positive Y points down.");
    return ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},{"constraints",ScValue{ScValue::Array{
        ScValue{std::string("An independent snapshot; editing it never updates the entity.")},
        ScValue{std::string("Body fields describe current solver configuration. Defaults and dependent-field overrides apply at spawn/patch time.")}}}}}};
}

ScValue sc_entity_edit_contract() {
    ScValue::Array fields;
    for(const auto& [name,type]:{std::pair{"id","ScEntityId"},std::pair{"patch","ScEntityPatch"}})
        fields.emplace_back(ScValue::Object{{"name",ScValue{std::string(name)}},{"type",ScValue{std::string(type)}},
            {"required",ScValue{true}},{"description",ScValue{std::string(name==std::string_view("id")?
                "Live runtime handle to patch.":"Fields to replace; unspecified fields remain unchanged.")}}});
    return ScValue{ScValue::Object{{"fields",ScValue{std::move(fields)}},{"constraints",ScValue{ScValue::Array{
        ScValue{std::string("Plain table containing exactly id and patch; unknown fields are rejected.")},
        ScValue{std::string("Duplicate entity IDs in a batch are rejected; the entire batch is validated before commit.")}}}}}};
}

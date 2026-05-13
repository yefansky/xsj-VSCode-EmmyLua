#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace emmy {

using json = nlohmann::json;

enum class Severity { None = 0, Warning = 1, Error = 2 };

inline Severity severityFromString(const std::string& s) {
    if (s == "Warning") return Severity::Warning;
    if (s == "Error") return Severity::Error;
    return Severity::None;
}

// All emmylua.* settings from VSCode extension
struct Config {
    std::vector<std::string> source_roots;
    bool completion_case_sensitive = false;
    bool code_lens = false;
    std::string constructor_names = "new;get";
    std::string require_like_functions = "require;Include";
    bool support_module_pattern = true;  // Lua 5.1 module() pattern

    struct TypeCheck {
        bool any_can_assign_to_any_define = true;
        bool define_any_can_be_assigned_by_any = true;
        bool define_type_can_receive_nil = false;
    } typecheck;

    struct Inspections {
        Severity undeclared_variable = Severity::None;
        Severity field_validation = Severity::None;
        Severity parameter_validation = Severity::None;
        Severity assign_validation = Severity::None;
        bool deprecated = false;
    } inspections;

    struct Hint {
        bool param_hint = true;
        bool local_hint = false;
        bool vararg_hint = true;
        bool override_hint = false;
    } hint;

    // Update from VSCode configuration JSON
    void update(const json& settings) {
        json emmylua;
        if (settings.contains("emmylua") && settings["emmylua"].is_object()) {
            emmylua = settings["emmylua"];
        }

        auto getSetting = [&](const std::string& flatKey, const std::string& nestedKey) -> const json* {
            if (settings.contains(flatKey) && !settings[flatKey].is_null()) return &settings[flatKey];
            if (emmylua.contains(nestedKey) && !emmylua[nestedKey].is_null()) return &emmylua[nestedKey];
            return nullptr;
        };

        if (auto v = getSetting("emmylua.source.roots", "source.roots")) {
            if (v->is_array()) source_roots = v->get<std::vector<std::string>>();
        }
        if (auto v = getSetting("emmylua.completion.caseSensitive", "completion.caseSensitive")) {
            if (v->is_boolean()) completion_case_sensitive = v->get<bool>();
        }
        if (auto v = getSetting("emmylua.codeLens", "codeLens")) {
            if (v->is_boolean()) code_lens = v->get<bool>();
        }
        if (auto v = getSetting("emmylua.constructorNames", "constructorNames")) {
            if (v->is_string()) constructor_names = v->get<std::string>();
        }
        if (auto v = getSetting("emmylua.requireLikeFunctions", "requireLikeFunctions")) {
            if (v->is_string()) require_like_functions = v->get<std::string>();
        }
        if (auto v = getSetting("emmylua.supportModulePattern", "supportModulePattern")) {
            if (v->is_boolean()) support_module_pattern = v->get<bool>();
        }

        // Typecheck
        if (settings.contains("emmylua.typecheck.anyTypeCanAssignToAnyDefineType") && settings["emmylua.typecheck.anyTypeCanAssignToAnyDefineType"].is_boolean()) {
            typecheck.any_can_assign_to_any_define = settings["emmylua.typecheck.anyTypeCanAssignToAnyDefineType"].get<bool>();
        }
        if (settings.contains("emmylua.typecheck.defineAnyTypeCanBeAssignedByAnyVariable") && settings["emmylua.typecheck.defineAnyTypeCanBeAssignedByAnyVariable"].is_boolean()) {
            typecheck.define_any_can_be_assigned_by_any = settings["emmylua.typecheck.defineAnyTypeCanBeAssignedByAnyVariable"].get<bool>();
        }
        if (settings.contains("emmylua.typecheck.defineTypeCanReceiveNilType") && settings["emmylua.typecheck.defineTypeCanReceiveNilType"].is_boolean()) {
            typecheck.define_type_can_receive_nil = settings["emmylua.typecheck.defineTypeCanReceiveNilType"].get<bool>();
        }

        // Inspections
        if (settings.contains("emmylua.inspections.undeclaredVariable") && settings["emmylua.inspections.undeclaredVariable"].is_string()) {
            inspections.undeclared_variable = severityFromString(settings["emmylua.inspections.undeclaredVariable"].get<std::string>());
        }
        if (settings.contains("emmylua.inspections.fieldValidation") && settings["emmylua.inspections.fieldValidation"].is_string()) {
            inspections.field_validation = severityFromString(settings["emmylua.inspections.fieldValidation"].get<std::string>());
        }
        if (settings.contains("emmylua.inspections.parameterValidation") && settings["emmylua.inspections.parameterValidation"].is_string()) {
            inspections.parameter_validation = severityFromString(settings["emmylua.inspections.parameterValidation"].get<std::string>());
        }
        if (settings.contains("emmylua.inspections.assignValidation") && settings["emmylua.inspections.assignValidation"].is_string()) {
            inspections.assign_validation = severityFromString(settings["emmylua.inspections.assignValidation"].get<std::string>());
        }
        if (settings.contains("emmylua.inspections.deprecated") && settings["emmylua.inspections.deprecated"].is_boolean()) {
            inspections.deprecated = settings["emmylua.inspections.deprecated"].get<bool>();
        }

        // Hints
        if (settings.contains("emmylua.hint.paramHint") && settings["emmylua.hint.paramHint"].is_boolean()) {
            hint.param_hint = settings["emmylua.hint.paramHint"].get<bool>();
        }
        if (settings.contains("emmylua.hint.localHint") && settings["emmylua.hint.localHint"].is_boolean()) {
            hint.local_hint = settings["emmylua.hint.localHint"].get<bool>();
        }
        if (settings.contains("emmylua.hint.varargHint") && settings["emmylua.hint.varargHint"].is_boolean()) {
            hint.vararg_hint = settings["emmylua.hint.varargHint"].get<bool>();
        }
        if (settings.contains("emmylua.hint.overrideHint") && settings["emmylua.hint.overrideHint"].is_boolean()) {
            hint.override_hint = settings["emmylua.hint.overrideHint"].get<bool>();
        }
    }
};

// emmy.config.json file format
struct EmmyConfig {
    std::string lua_version;  // "lua5.1", "lua5.2", "lua5.3"

    struct SourceRoot {
        std::string dir;
        std::vector<std::string> exclude;
    };
    std::vector<SourceRoot> source;

    struct Editor {
        bool completion_case_sensitive = false;
    } editor;

    static EmmyConfig parse(const json& j) {
        EmmyConfig cfg;
        if (j.contains("lua.version")) {
            cfg.lua_version = j["lua.version"].get<std::string>();
        }
        if (j.contains("source")) {
            for (auto& s : j["source"]) {
                SourceRoot root;
                root.dir = s.value("dir", "");
                if (s.contains("exclude")) {
                    root.exclude = s["exclude"].get<std::vector<std::string>>();
                }
                cfg.source.push_back(root);
            }
        }
        if (j.contains("editor")) {
            auto ed = j["editor"];
            cfg.editor.completion_case_sensitive = ed.value("completionCaseSensitive", false);
        }
        return cfg;
    }
};

}  // namespace emmy

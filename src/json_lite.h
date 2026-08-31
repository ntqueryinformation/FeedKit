// FeedKit - json_lite.h
// Minimal recursive-descent JSON parser, just enough for the GitHub API and
// dlss_manifest.json. No external dependencies.
#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>

namespace fk {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::wstring str;
    std::vector<Json> items;
    std::vector<std::pair<std::wstring, Json>> members;

    bool is(Type t) const { return type == t; }
    bool is_null() const { return type == Type::Null; }

    // Object access: returns nullptr when key absent or not an object.
    const Json* find(const std::wstring& key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : members)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::wstring get_str(const std::wstring& key, const std::wstring& def = {}) const {
        const Json* v = find(key);
        return v && v->type == Type::String ? v->str : def;
    }

    static bool parse(const std::string& utf8, Json& out);
};

} // namespace fk

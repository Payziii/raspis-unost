#pragma once

#include <map>
#include <string>
#include <vector>

namespace timetable {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::map<std::string, JsonValue> object_value;

    static JsonValue MakeNull();
    static JsonValue MakeBool(bool value);
    static JsonValue MakeNumber(double value);
    static JsonValue MakeString(const std::string& value);
    static JsonValue MakeArray();
    static JsonValue MakeObject();

    bool IsNull() const;
    bool IsBool() const;
    bool IsNumber() const;
    bool IsString() const;
    bool IsArray() const;
    bool IsObject() const;

    bool Has(const std::string& key) const;
    const JsonValue& At(const std::string& key) const;
    JsonValue& At(const std::string& key);
};

struct JsonParseResult {
    bool ok = false;
    JsonValue value;
    std::string error;
};

JsonParseResult ParseJson(const std::string& text);
std::string ToJson(const JsonValue& value, int indent = 0);
std::string JsonEscape(const std::string& text);

int JsonInt(const JsonValue& object, const std::string& key, int fallback);
bool JsonBool(const JsonValue& object, const std::string& key, bool fallback);
std::string JsonString(const JsonValue& object, const std::string& key, const std::string& fallback);

}  // namespace timetable

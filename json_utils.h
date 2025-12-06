#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>

// Minimal JSON parser for Bazel aquery output
namespace json_utils {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

struct JsonValue;

using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue {
    JsonType type = JsonType::Null;
    bool bool_val = false;
    double number_val = 0.0;
    std::string string_val;
    std::shared_ptr<JsonArray> array_val;
    std::shared_ptr<JsonObject> object_val;

    JsonValue() = default;
    JsonValue(bool b) : type(JsonType::Boolean), bool_val(b) {}
    JsonValue(double n) : type(JsonType::Number), number_val(n) {}
    JsonValue(std::string s) : type(JsonType::String), string_val(std::move(s)) {}
    JsonValue(const char* s) : type(JsonType::String), string_val(s) {}
    JsonValue(JsonArray a) : type(JsonType::Array), array_val(std::make_shared<JsonArray>(std::move(a))) {}
    JsonValue(JsonObject o) : type(JsonType::Object), object_val(std::make_shared<JsonObject>(std::move(o))) {}

    static JsonValue Array() { return JsonValue(JsonArray{}); }
    static JsonValue Object() { return JsonValue(JsonObject{}); }

    const JsonArray& as_array() const { return *array_val; }
    const JsonObject& as_object() const { return *object_val; }
    
    bool is_null() const { return type == JsonType::Null; }
};

JsonValue Parse(const std::string& json);
std::string Dump(const JsonValue& val, int indent = 0);

} // namespace json_utils

#endif // JSON_UTILS_H

#include "json_utils.h"
#include <sstream>
#include <iostream>
#include <cctype>
#include <stdexcept>
#include <cmath>

namespace json_utils {

namespace {

struct Parser {
    const std::string& str;
    size_t pos = 0;

    char peek() {
        while (pos < str.size() && std::isspace(str[pos])) pos++;
        if (pos == str.size()) return 0;
        return str[pos];
    }

    char advance() {
        char c = peek();
        if (c) pos++;
        return c;
    }

    JsonValue parse_value() {
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || std::isdigit(c)) return parse_number();
        throw std::runtime_error("Invalid JSON value");
    }

    JsonValue parse_object() {
        advance(); // skip {
        JsonObject obj;
        while (true) {
            char c = peek();
            if (c == '}') {
                advance();
                break;
            }
            if (!obj.empty()) {
                if (c != ',') throw std::runtime_error("Expected , in object");
                advance();
            }
            JsonValue key = parse_string();
            if (peek() != ':') throw std::runtime_error("Expected : in object");
            advance();
            obj[key.string_val] = parse_value();
        }
        return JsonValue(obj);
    }

    JsonValue parse_array() {
        advance(); // skip [
        JsonArray arr;
        while (true) {
            char c = peek();
            if (c == ']') {
                advance();
                break;
            }
            if (!arr.empty()) {
                if (c != ',') throw std::runtime_error("Expected , in array");
                advance();
            }
            arr.push_back(parse_value());
        }
        return JsonValue(arr);
    }

    JsonValue parse_string() {
        advance(); // skip "
        std::string s;
        while (true) {
            if (pos >= str.size()) throw std::runtime_error("Unterminated string");
            char c = str[pos++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos >= str.size()) throw std::runtime_error("Unterminated string");
                char escape = str[pos++];
                if (escape == '"') s += '"';
                else if (escape == '\\') s += '\\';
                else if (escape == '/') s += '/';
                else if (escape == 'b') s += '\b';
                else if (escape == 'f') s += '\f';
                else if (escape == 'n') s += '\n';
                else if (escape == 'r') s += '\r';
                else if (escape == 't') s += '\t';
                else if (escape == 'u') {
                    if (pos + 4 > str.size()) throw std::runtime_error("Unterminated unicode escape");
                    std::string hex = str.substr(pos, 4);
                    pos += 4;
                    try {
                        int c_val = std::stoi(hex, nullptr, 16);
                        s += static_cast<char>(c_val);
                    } catch (const std::invalid_argument& e) {
                        throw std::runtime_error("Invalid unicode escape: " + hex);
                    }
                }
            } else {
                s += c;
            }
        }
        return JsonValue(s);
    }

    JsonValue parse_bool() {
        if (str.substr(pos, 4) == "true") {
            pos += 4;
            return JsonValue(true);
        }
        if (str.substr(pos, 5) == "false") {
            pos += 5;
            return JsonValue(false);
        }
        throw std::runtime_error("Invalid boolean");
    }

    JsonValue parse_null() {
        if (str.substr(pos, 4) == "null") {
            pos += 4;
            return JsonValue();
        }
        throw std::runtime_error("Invalid null");
    }

    JsonValue parse_number() {
        size_t start = pos;
        if (str[pos] == '-') pos++;
        while (pos < str.size() && std::isdigit(str[pos])) pos++;
        if (pos < str.size() && str[pos] == '.') {
            pos++;
            while (pos < str.size() && std::isdigit(str[pos])) pos++;
        }
        if (pos < str.size() && (str[pos] == 'e' || str[pos] == 'E')) {
            pos++;
            if (pos < str.size() && (str[pos] == '+' || str[pos] == '-')) pos++;
            while (pos < str.size() && std::isdigit(str[pos])) pos++;
        }
        return JsonValue(std::stod(str.substr(start, pos - start)));
    }
};

void dump_string(const std::string& s, std::stringstream& ss) {
    ss << '"';
    for (char c : s) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else if (c < 0x20) {
            // hex print?
            ss << c; 
        } else {
            ss << c;
        }
    }
    ss << '"';
}

void dump_value(const JsonValue& val, std::stringstream& ss, int indent, int level) {
    switch (val.type) {
        case JsonType::Null: ss << "null"; break;
        case JsonType::Boolean: ss << (val.bool_val ? "true" : "false"); break;
        case JsonType::Number: ss << val.number_val; break; // simplistic, might lose precision
        case JsonType::String: dump_string(val.string_val, ss); break;
        case JsonType::Array: {
            ss << "[\n";
            const auto& arr = *val.array_val;
            for (size_t i = 0; i < arr.size(); ++i) {
                for (int j = 0; j < (level + 1) * indent; ++j) ss << ' ';
                dump_value(arr[i], ss, indent, level + 1);
                if (i < arr.size() - 1) ss << ",";
                ss << "\n";
            }
            for (int j = 0; j < level * indent; ++j) ss << ' ';
            ss << "]";
            break;
        }
        case JsonType::Object: {
            ss << "{\n";
            const auto& obj = *val.object_val;
            size_t i = 0;
            for (const auto& kv : obj) {
                for (int j = 0; j < (level + 1) * indent; ++j) ss << ' ';
                dump_string(kv.first, ss);
                ss << ": ";
                dump_value(kv.second, ss, indent, level + 1);
                if (i < obj.size() - 1) ss << ",";
                ss << "\n";
                i++;
            }
            for (int j = 0; j < level * indent; ++j) ss << ' ';
            ss << "}";
            break;
        }
    }
}

} // namespace

JsonValue Parse(const std::string& json) {
    Parser p{json};
    return p.parse_value();
}

std::string Dump(const JsonValue& val, int indent) {
    std::stringstream ss;
    dump_value(val, ss, indent, 0);
    return ss.str();
}

} // namespace json_utils
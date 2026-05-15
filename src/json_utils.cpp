#include "json_utils.h"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace timetable {

JsonValue JsonValue::MakeNull() { return JsonValue(); }
JsonValue JsonValue::MakeBool(bool value) { JsonValue v; v.type = Type::Bool; v.bool_value = value; return v; }
JsonValue JsonValue::MakeNumber(double value) { JsonValue v; v.type = Type::Number; v.number_value = value; return v; }
JsonValue JsonValue::MakeString(const std::string& value) { JsonValue v; v.type = Type::String; v.string_value = value; return v; }
JsonValue JsonValue::MakeArray() { JsonValue v; v.type = Type::Array; return v; }
JsonValue JsonValue::MakeObject() { JsonValue v; v.type = Type::Object; return v; }

bool JsonValue::IsNull() const { return type == Type::Null; }
bool JsonValue::IsBool() const { return type == Type::Bool; }
bool JsonValue::IsNumber() const { return type == Type::Number; }
bool JsonValue::IsString() const { return type == Type::String; }
bool JsonValue::IsArray() const { return type == Type::Array; }
bool JsonValue::IsObject() const { return type == Type::Object; }

bool JsonValue::Has(const std::string& key) const {
    if (!IsObject()) return false;
    return object_value.find(key) != object_value.end();
}

const JsonValue& JsonValue::At(const std::string& key) const {
    static const JsonValue null_value;
    if (!IsObject()) return null_value;
    auto it = object_value.find(key);
    if (it == object_value.end()) return null_value;
    return it->second;
}

JsonValue& JsonValue::At(const std::string& key) {
    if (!IsObject()) {
        type = Type::Object;
        object_value.clear();
    }
    return object_value[key];
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& input) : input_(input) {}

    JsonParseResult Parse() {
        JsonParseResult result;
        SkipSpaces();
        if (!ParseValue(result.value)) {
            result.ok = false;
            result.error = error_.empty() ? "Некорректный JSON" : error_;
            return result;
        }
        SkipSpaces();
        if (pos_ != input_.size()) {
            result.ok = false;
            result.error = "Лишние символы после JSON";
            return result;
        }
        result.ok = true;
        return result;
    }

private:
    bool ParseValue(JsonValue& out) {
        SkipSpaces();
        if (pos_ >= input_.size()) return Fail("Неожиданный конец JSON");
        char c = input_[pos_];
        if (c == 'n') return ParseLiteral("null", JsonValue::MakeNull(), out);
        if (c == 't') return ParseLiteral("true", JsonValue::MakeBool(true), out);
        if (c == 'f') return ParseLiteral("false", JsonValue::MakeBool(false), out);
        if (c == '"') return ParseString(out);
        if (c == '[') return ParseArray(out);
        if (c == '{') return ParseObject(out);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return ParseNumber(out);
        return Fail("Ожидалось значение JSON");
    }

    bool ParseLiteral(const char* literal, const JsonValue& value, JsonValue& out) {
        std::string lit(literal);
        if (input_.compare(pos_, lit.size(), lit) != 0) {
            return Fail("Некорректный литерал JSON");
        }
        pos_ += lit.size();
        out = value;
        return true;
    }

    bool ParseString(JsonValue& out) {
        std::string s;
        if (input_[pos_] != '"') return Fail("Ожидалась строка");
        pos_++;
        while (pos_ < input_.size()) {
            unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') {
                out = JsonValue::MakeString(s);
                return true;
            }
            if (c == '\\') {
                if (pos_ >= input_.size()) return Fail("Обрыв escape-последовательности");
                char e = input_[pos_++];
                switch (e) {
                case '"': s.push_back('"'); break;
                case '\\': s.push_back('\\'); break;
                case '/': s.push_back('/'); break;
                case 'b': s.push_back('\b'); break;
                case 'f': s.push_back('\f'); break;
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                case 'u':
                    // Для русских строк от клиента в UTF-8 это не нужно.
                    // Unicode escape оставляем как вопросительный знак, чтобы парсер не падал.
                    if (pos_ + 4 > input_.size()) return Fail("Некорректный unicode escape");
                    pos_ += 4;
                    s.push_back('?');
                    break;
                default:
                    return Fail("Некорректный escape в строке");
                }
            } else {
                s.push_back(static_cast<char>(c));
            }
        }
        return Fail("Строка JSON не закрыта");
    }

    bool ParseNumber(JsonValue& out) {
        size_t start = pos_;
        if (input_[pos_] == '-') pos_++;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) pos_++;
        if (pos_ < input_.size() && input_[pos_] == '.') {
            pos_++;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) pos_++;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            pos_++;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) pos_++;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) pos_++;
        }
        try {
            out = JsonValue::MakeNumber(std::stod(input_.substr(start, pos_ - start)));
            return true;
        } catch (...) {
            return Fail("Некорректное число JSON");
        }
    }

    bool ParseArray(JsonValue& out) {
        pos_++;
        out = JsonValue::MakeArray();
        SkipSpaces();
        if (pos_ < input_.size() && input_[pos_] == ']') { pos_++; return true; }
        while (true) {
            JsonValue item;
            if (!ParseValue(item)) return false;
            out.array_value.push_back(item);
            SkipSpaces();
            if (pos_ >= input_.size()) return Fail("Массив JSON не закрыт");
            if (input_[pos_] == ']') { pos_++; return true; }
            if (input_[pos_] != ',') return Fail("Ожидалась запятая в массиве");
            pos_++;
        }
    }

    bool ParseObject(JsonValue& out) {
        pos_++;
        out = JsonValue::MakeObject();
        SkipSpaces();
        if (pos_ < input_.size() && input_[pos_] == '}') { pos_++; return true; }
        while (true) {
            SkipSpaces();
            JsonValue key;
            if (!ParseString(key)) return false;
            SkipSpaces();
            if (pos_ >= input_.size() || input_[pos_] != ':') return Fail("Ожидалось двоеточие в объекте");
            pos_++;
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.object_value[key.string_value] = value;
            SkipSpaces();
            if (pos_ >= input_.size()) return Fail("Объект JSON не закрыт");
            if (input_[pos_] == '}') { pos_++; return true; }
            if (input_[pos_] != ',') return Fail("Ожидалась запятая в объекте");
            pos_++;
        }
    }

    void SkipSpaces() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) pos_++;
    }

    bool Fail(const std::string& message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    const std::string& input_;
    size_t pos_ = 0;
    std::string error_;
};

void WriteJson(const JsonValue& value, std::ostringstream& out, int indent, int level) {
    switch (value.type) {
    case JsonValue::Type::Null:
        out << "null";
        break;
    case JsonValue::Type::Bool:
        out << (value.bool_value ? "true" : "false");
        break;
    case JsonValue::Type::Number: {
        double rounded = std::round(value.number_value);
        if (std::fabs(value.number_value - rounded) < 0.0000001) {
            out << static_cast<long long>(rounded);
        } else {
            out << std::setprecision(15) << value.number_value;
        }
        break;
    }
    case JsonValue::Type::String:
        out << '"' << JsonEscape(value.string_value) << '"';
        break;
    case JsonValue::Type::Array: {
        out << '[';
        for (size_t i = 0; i < value.array_value.size(); i++) {
            if (i > 0) out << ',';
            if (indent > 0) out << '\n' << std::string((level + 1) * indent, ' ');
            WriteJson(value.array_value[i], out, indent, level + 1);
        }
        if (indent > 0 && !value.array_value.empty()) out << '\n' << std::string(level * indent, ' ');
        out << ']';
        break;
    }
    case JsonValue::Type::Object: {
        out << '{';
        size_t i = 0;
        for (const auto& kv : value.object_value) {
            if (i++ > 0) out << ',';
            if (indent > 0) out << '\n' << std::string((level + 1) * indent, ' ');
            out << '"' << JsonEscape(kv.first) << '"' << ':';
            if (indent > 0) out << ' ';
            WriteJson(kv.second, out, indent, level + 1);
        }
        if (indent > 0 && !value.object_value.empty()) out << '\n' << std::string(level * indent, ' ');
        out << '}';
        break;
    }
    }
}

}  // namespace

JsonParseResult ParseJson(const std::string& text) {
    Parser parser(text);
    return parser.Parse();
}

std::string JsonEscape(const std::string& text) {
    std::ostringstream out;
    for (unsigned char ch : text) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) out << "?";
            else out << static_cast<char>(ch);
        }
    }
    return out.str();
}

std::string ToJson(const JsonValue& value, int indent) {
    std::ostringstream out;
    WriteJson(value, out, indent, 0);
    return out.str();
}

int JsonInt(const JsonValue& object, const std::string& key, int fallback) {
    const JsonValue& value = object.At(key);
    if (!value.IsNumber()) return fallback;
    return static_cast<int>(std::llround(value.number_value));
}

bool JsonBool(const JsonValue& object, const std::string& key, bool fallback) {
    const JsonValue& value = object.At(key);
    return value.IsBool() ? value.bool_value : fallback;
}

std::string JsonString(const JsonValue& object, const std::string& key, const std::string& fallback) {
    const JsonValue& value = object.At(key);
    return value.IsString() ? value.string_value : fallback;
}

}  // namespace timetable

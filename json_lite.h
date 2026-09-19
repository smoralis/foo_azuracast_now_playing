#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cctype>


namespace azuracast {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() : m_type(Type::Null) {}

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }

    const Json& operator[](const std::string& key) const {
        static const Json s_null;
        if (m_type != Type::Object) return s_null;
        auto it = m_object.find(key);
        if (it == m_object.end()) return s_null;
        return it->second;
    }

    const Json& operator[](size_t idx) const {
        static const Json s_null;
        if (m_type != Type::Array || idx >= m_array.size()) return s_null;
        return m_array[idx];
    }

    size_t size() const {
        if (m_type == Type::Array) return m_array.size();
        if (m_type == Type::Object) return m_object.size();
        return 0;
    }

    std::string asString(const std::string& def = "") const {
        return m_type == Type::String ? m_string : def;
    }

    double asDouble(double def = 0.0) const {
        return m_type == Type::Number ? m_number : def;
    }

    int asInt(int def = 0) const {
        return m_type == Type::Number ? (int)m_number : def;
    }

    bool asBool(bool def = false) const {
        return m_type == Type::Bool ? m_bool : def;
    }

    static Json parse(const std::string& text) {
        Parser p(text);
        return p.parseValue();
    }

private:
    Type m_type;
    std::string m_string;
    double m_number = 0.0;
    bool m_bool = false;
    std::vector<Json> m_array;
    std::map<std::string, Json> m_object;

    class Parser {
    public:
        explicit Parser(const std::string& s) : m_s(s), m_pos(0) {}

        Json parseValue() {
            skipWs();
            if (m_pos >= m_s.size()) throw std::runtime_error("unexpected end of JSON");
            char c = m_s[m_pos];
            if (c == '{') return parseObject();
            if (c == '[') return parseArray();
            if (c == '"') return parseString();
            if (c == 't' || c == 'f') return parseBool();
            if (c == 'n') { m_pos += 4; return Json(); }
            return parseNumber();
        }

    private:
        const std::string& m_s;
        size_t m_pos;

        void skipWs() {
            while (m_pos < m_s.size() && std::isspace((unsigned char)m_s[m_pos])) m_pos++;
        }

        Json parseObject() {
            Json j; j.m_type = Type::Object;
            m_pos++;
            skipWs();
            if (m_pos < m_s.size() && m_s[m_pos] == '}') { m_pos++; return j; }
            while (true) {
                skipWs();
                Json key = parseString();
                skipWs();
                if (m_pos >= m_s.size() || m_s[m_pos] != ':') throw std::runtime_error("expected ':'");
                m_pos++;
                Json val = parseValue();
                j.m_object[key.asString()] = val;
                skipWs();
                if (m_pos < m_s.size() && m_s[m_pos] == ',') { m_pos++; continue; }
                if (m_pos < m_s.size() && m_s[m_pos] == '}') { m_pos++; break; }
                throw std::runtime_error("expected ',' or '}'");
            }
            return j;
        }

        Json parseArray() {
            Json j; j.m_type = Type::Array;
            m_pos++; 
            skipWs();
            if (m_pos < m_s.size() && m_s[m_pos] == ']') { m_pos++; return j; }
            while (true) {
                Json val = parseValue();
                j.m_array.push_back(val);
                skipWs();
                if (m_pos < m_s.size() && m_s[m_pos] == ',') { m_pos++; continue; }
                if (m_pos < m_s.size() && m_s[m_pos] == ']') { m_pos++; break; }
                throw std::runtime_error("expected ',' or ']'");
            }
            return j;
        }

        Json parseString() {
            Json j; j.m_type = Type::String;
            if (m_pos >= m_s.size() || m_s[m_pos] != '"') throw std::runtime_error("expected string");
            m_pos++;
            std::string out;
            while (m_pos < m_s.size() && m_s[m_pos] != '"') {
                char c = m_s[m_pos];
                if (c == '\\' && m_pos + 1 < m_s.size()) {
                    m_pos++;
                    char esc = m_s[m_pos];
                    switch (esc) {
                        case 'n': out += '\n'; break;
                        case 't': out += '\t'; break;
                        case 'r': out += '\r'; break;
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'u': {
                            if (m_pos + 4 < m_s.size()) {
                                std::string hex = m_s.substr(m_pos + 1, 4);
                                unsigned int cp = (unsigned int)std::stoul(hex, nullptr, 16);
                                m_pos += 4;
                                if (cp < 0x80) out += (char)cp;
                                else if (cp < 0x800) {
                                    out += (char)(0xC0 | (cp >> 6));
                                    out += (char)(0x80 | (cp & 0x3F));
                                } else {
                                    out += (char)(0xE0 | (cp >> 12));
                                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                                    out += (char)(0x80 | (cp & 0x3F));
                                }
                            }
                            break;
                        }
                        default: out += esc; break;
                    }
                } else {
                    out += c;
                }
                m_pos++;
            }
            m_pos++;
            j.m_string = out;
            return j;
        }

        Json parseBool() {
            Json j; j.m_type = Type::Bool;
            if (m_s.compare(m_pos, 4, "true") == 0) { j.m_bool = true; m_pos += 4; }
            else { j.m_bool = false; m_pos += 5; } 
            return j;
        }

        Json parseNumber() {
            Json j; j.m_type = Type::Number;
            size_t start = m_pos;
            while (m_pos < m_s.size() &&
                   (std::isdigit((unsigned char)m_s[m_pos]) || m_s[m_pos] == '-' ||
                    m_s[m_pos] == '+' || m_s[m_pos] == '.' || m_s[m_pos] == 'e' || m_s[m_pos] == 'E')) {
                m_pos++;
            }
            j.m_number = std::stod(m_s.substr(start, m_pos - start));
            return j;
        }
    };
};

}

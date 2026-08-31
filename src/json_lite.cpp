// FeedKit - json_lite.cpp
#include "json_lite.h"
#include "util.h"

namespace fk {

namespace {

struct Parser {
    const std::wstring& s;
    size_t pos = 0;
    bool ok = true;

    explicit Parser(const std::wstring& src) : s(src) {}

    void skip_ws() {
        while (pos < s.size() && (s[pos] == L' ' || s[pos] == L'\t' || s[pos] == L'\r' || s[pos] == L'\n'))
            pos++;
    }

    wchar_t peek() {
        skip_ws();
        return pos < s.size() ? s[pos] : L'\0';
    }

    bool eat(wchar_t c) {
        if (peek() == c) { pos++; return true; }
        return false;
    }

    void fail() { ok = false; }

    bool parse_value(Json& out, int depth) {
        if (depth > 64 || !ok) { fail(); return false; }
        wchar_t c = peek();
        switch (c) {
        case L'{': return parse_object(out, depth);
        case L'[': return parse_array(out, depth);
        case L'"': out.type = Json::Type::String; out.str = parse_string(); return ok;
        case L't': case L'f':
            out.type = Json::Type::Bool;
            if (s.compare(pos, 4, L"true") == 0) { out.boolean = true; pos += 4; return true; }
            if (s.compare(pos, 5, L"false") == 0) { out.boolean = false; pos += 5; return true; }
            fail(); return false;
        case L'n':
            if (s.compare(pos, 4, L"null") == 0) { out.type = Json::Type::Null; pos += 4; return true; }
            fail(); return false;
        default:
            if (c == L'-' || iswdigit(c)) return parse_number(out);
            fail(); return false;
        }
    }

    bool parse_number(Json& out) {
        skip_ws();
        size_t start = pos;
        if (pos < s.size() && s[pos] == L'-') pos++;
        while (pos < s.size() && (iswdigit(s[pos]) || s[pos] == L'.' || s[pos] == L'e' || s[pos] == L'E' ||
                                  s[pos] == L'+' || s[pos] == L'-'))
            pos++;
        if (pos == start) { fail(); return false; }
        out.type = Json::Type::Number;
        out.number = wcstod(s.substr(start, pos - start).c_str(), nullptr);
        return true;
    }

    std::wstring parse_string() {
        std::wstring out;
        if (!eat(L'"')) { fail(); return out; }
        while (pos < s.size()) {
            wchar_t c = s[pos++];
            if (c == L'"') return out;
            if (c == L'\\') {
                if (pos >= s.size()) break;
                wchar_t e = s[pos++];
                switch (e) {
                case L'"': out += L'"'; break;
                case L'\\': out += L'\\'; break;
                case L'/': out += L'/'; break;
                case L'b': out += L'\b'; break;
                case L'f': out += L'\f'; break;
                case L'n': out += L'\n'; break;
                case L'r': out += L'\r'; break;
                case L't': out += L'\t'; break;
                case L'u': {
                    if (pos + 4 > s.size()) { fail(); return out; }
                    unsigned cp = (unsigned)wcstoul(s.substr(pos, 4).c_str(), nullptr, 16);
                    pos += 4;
                    // Surrogate pair
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= s.size() && s[pos] == L'\\' && s[pos + 1] == L'u') {
                        unsigned lo = (unsigned)wcstoul(s.substr(pos + 2, 4).c_str(), nullptr, 16);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            pos += 6;
                        }
                    }
                    if (cp >= 0x10000) {
                        cp -= 0x10000;
                        out += (wchar_t)(0xD800 + (cp >> 10));
                        out += (wchar_t)(0xDC00 + (cp & 0x3FF));
                    } else {
                        out += (wchar_t)cp;
                    }
                    break;
                }
                default: fail(); return out;
                }
            } else {
                out += c;
            }
        }
        fail();
        return out;
    }

    bool parse_array(Json& out, int depth) {
        out.type = Json::Type::Array;
        pos++; // [
        if (eat(L']')) return true;
        while (ok) {
            Json v;
            if (!parse_value(v, depth + 1)) break;
            out.items.push_back(std::move(v));
            if (eat(L',')) continue;
            if (eat(L']')) return true;
            fail();
        }
        return false;
    }

    bool parse_object(Json& out, int depth) {
        out.type = Json::Type::Object;
        pos++; // {
        if (eat(L'}')) return true;
        while (ok) {
            std::wstring key = parse_string();
            if (!ok) break;
            if (!eat(L':')) { fail(); break; }
            Json v;
            if (!parse_value(v, depth + 1)) break;
            out.members.emplace_back(std::move(key), std::move(v));
            if (eat(L',')) continue;
            if (eat(L'}')) return true;
            fail();
        }
        return false;
    }
};

} // namespace

bool Json::parse(const std::string& utf8, Json& out) {
    std::wstring src = to_wide(utf8);
    Parser p(src);
    out = Json{};
    if (!p.parse_value(out, 0)) return false;
    p.skip_ws();
    return p.pos >= src.size();
}

} // namespace fk

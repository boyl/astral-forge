#include "json_min.h"

#include <stdlib.h>
#include <ctype.h>

namespace aamod {
namespace json {

static void skip_ws(const std::string& s, size_t& i)
{
    while (i < s.size() && isspace((unsigned char)s[i])) ++i;
}

static std::string read_string(const std::string& s, size_t& i)
{
    std::string out;
    ++i; // opening quote
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"')
            break;
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i >= s.size())
            break;
        char e = s[i++];
        switch (e) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'u': {
            // keep it simple: copy the raw escape through
            out += "\\u";
            for (int k = 0; k < 4 && i < s.size(); ++k)
                out.push_back(s[i++]);
            break;
        }
        default: out.push_back(e); break;
        }
    }
    return out;
}

static void skip_value(const std::string& s, size_t& i)
{
    skip_ws(s, i);
    if (i >= s.size())
        return;
    if (s[i] == '{' || s[i] == '[') {
        char open = s[i], close = (open == '{') ? '}' : ']';
        int depth = 0;
        bool in_str = false;
        while (i < s.size()) {
            char c = s[i++];
            if (in_str) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') { in_str = true; continue; }
            if (c == open) ++depth;
            else if (c == close) {
                if (--depth == 0)
                    break;
            }
        }
        return;
    }
    if (s[i] == '"') {
        read_string(s, i);
        return;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
           !isspace((unsigned char)s[i]))
        ++i;
}

bool parse_flat_object(const std::string& text, std::vector<Field>& out)
{
    out.clear();
    size_t i = 0;
    skip_ws(text, i);
    if (i >= text.size() || text[i] != '{')
        return false;
    ++i;

    for (;;) {
        skip_ws(text, i);
        if (i >= text.size())
            return false;
        if (text[i] == '}') {
            ++i;
            break;
        }
        if (text[i] == ',') {
            ++i;
            continue;
        }
        if (text[i] != '"')
            return false;

        Field f;
        f.is_string = false;
        f.is_true = false;
        f.is_false = false;
        f.is_number = false;
        f.key = read_string(text, i);

        skip_ws(text, i);
        if (i >= text.size() || text[i] != ':')
            return false;
        ++i;
        skip_ws(text, i);
        if (i >= text.size())
            return false;

        if (text[i] == '"') {
            f.is_string = true;
            f.value = read_string(text, i);
        }
        else if (text[i] == '{' || text[i] == '[') {
            size_t start = i;
            skip_value(text, i);
            f.value = text.substr(start, i - start);
        }
        else {
            size_t start = i;
            while (i < text.size() && text[i] != ',' && text[i] != '}' &&
                   !isspace((unsigned char)text[i]))
                ++i;
            f.value = text.substr(start, i - start);
            if (f.value == "true") f.is_true = true;
            else if (f.value == "false") f.is_false = true;
            else if (!f.value.empty() &&
                     (isdigit((unsigned char)f.value[0]) || f.value[0] == '-' ||
                      f.value[0] == '+'))
                f.is_number = true;
        }
        out.push_back(f);
    }
    return true;
}

const Field* find(const std::vector<Field>& fields, const std::string& key)
{
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i].key == key)
            return &fields[i];
    return NULL;
}

std::string get_string(const std::vector<Field>& fields, const std::string& key,
                       const std::string& def)
{
    const Field* f = find(fields, key);
    return (f && f->is_string) ? f->value : def;
}

bool get_bool(const std::vector<Field>& fields, const std::string& key, bool def)
{
    const Field* f = find(fields, key);
    if (!f)
        return def;
    if (f->is_true) return true;
    if (f->is_false) return false;
    if (f->is_number) return f->value != "0";
    if (f->is_string) return f->value == "true" || f->value == "1" || f->value == "yes";
    return def;
}

long long get_int(const std::vector<Field>& fields, const std::string& key, long long def)
{
    const Field* f = find(fields, key);
    if (!f)
        return def;
    const std::string& v = f->value;
    if (v.empty())
        return def;
    char* end = NULL;
    long long r = _strtoi64(v.c_str(), &end, 0);
    if (end == v.c_str())
        return def;
    return r;
}

} // namespace json
} // namespace aamod

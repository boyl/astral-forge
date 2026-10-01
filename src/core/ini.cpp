#include "ini.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

namespace aamod {

static std::string lower(const std::string& s)
{
    std::string r = s;
    for (size_t i = 0; i < r.size(); ++i)
        r[i] = (char)tolower((unsigned char)r[i]);
    return r;
}

static std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

void Ini::clear() { m_map.clear(); }

bool Ini::load_file(const wchar_t* path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return false;

    char buf[32768];
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;

    std::string section;
    std::string line;
    for (DWORD i = 0; i <= got; ++i) {
        char c = (i == got) ? '\n' : buf[i];
        if (c != '\n' && c != '\r') {
            line.push_back(c);
            continue;
        }
        std::string t = trim(line);
        line.clear();
        if (t.empty() || t[0] == ';' || t[0] == '#')
            continue;
        if (t[0] == '[') {
            size_t e = t.find(']');
            section = lower(trim(t.substr(1, e == std::string::npos ? e : e - 1)));
            if (!section.empty()) section += '.';
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = lower(trim(t.substr(0, eq)));
        std::string v = trim(t.substr(eq + 1));
        if (k.empty())
            continue;
        // strip inline comment
        size_t cm = v.find_first_of(";#");
        if (cm != std::string::npos) {
            std::string before = trim(v.substr(0, cm));
            if (!before.empty()) v = before;
        }
        m_map[section + k] = v;
    }
    return true;
}

bool Ini::has(const std::string& key) const
{
    return m_map.find(lower(key)) != m_map.end();
}

std::string Ini::get_str(const std::string& key, const std::string& def) const
{
    std::map<std::string, std::string>::const_iterator it = m_map.find(lower(key));
    return it == m_map.end() ? def : it->second;
}

long long Ini::get_int(const std::string& key, long long def) const
{
    std::string v = get_str(key);
    if (v.empty())
        return def;
    char* end = NULL;
    long long r = _strtoi64(v.c_str(), &end, 0);
    if (end == v.c_str())
        return def;
    return r;
}

bool Ini::get_bool(const std::string& key, bool def) const
{
    std::string v = lower(get_str(key));
    if (v.empty())
        return def;
    if (v == "1" || v == "true" || v == "yes" || v == "on")
        return true;
    if (v == "0" || v == "false" || v == "no" || v == "off")
        return false;
    return def;
}

} // namespace aamod

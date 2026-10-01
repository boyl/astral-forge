#ifndef AAMOD_INI_H
#define AAMOD_INI_H

#include <string>
#include <map>

namespace aamod {

// Minimal, dependency-free INI reader: [section] + key = value,
// ';' and '#' comments, whitespace trimmed, keys lower-cased.
class Ini {
public:
    bool load_file(const wchar_t* path);
    void clear();

    bool  has(const std::string& key) const;
    std::string get_str(const std::string& key, const std::string& def = std::string()) const;
    long long   get_int(const std::string& key, long long def = 0) const;
    bool        get_bool(const std::string& key, bool def = false) const;

    const std::map<std::string, std::string>& entries() const { return m_map; }

private:
    std::map<std::string, std::string> m_map;
};

} // namespace aamod

#endif // AAMOD_INI_H

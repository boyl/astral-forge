#ifndef AAMOD_JSON_MIN_H
#define AAMOD_JSON_MIN_H

#include <string>
#include <vector>

namespace aamod {
namespace json {

// A deliberately tiny, dependency-free JSON reader for flat objects.
// It only needs to read mod manifests (mod.json), which are flat:
//   { "id": "hello", "name": "Hello", "version": "1.0",
//     "entry": "hello.dll", "enabled": true, "priority": 10 }
// Nested objects/arrays are skipped, escapes are decoded for \n \t \" \\ \/.
// This is NOT a general JSON parser and does not validate input.

struct Field {
    std::string key;
    std::string value;   // raw text for objects/arrays, unquoted text for scalars
    bool        is_string;
    bool        is_true;
    bool        is_false;
    bool        is_number;
};

bool parse_flat_object(const std::string& text, std::vector<Field>& out);

// Convenience accessors over a parsed flat object.
const Field* find(const std::vector<Field>& fields, const std::string& key);
std::string  get_string(const std::vector<Field>& fields, const std::string& key,
                        const std::string& def = std::string());
bool         get_bool(const std::vector<Field>& fields, const std::string& key, bool def = false);
long long    get_int(const std::vector<Field>& fields, const std::string& key, long long def = 0);

} // namespace json
} // namespace aamod

#endif // AAMOD_JSON_MIN_H

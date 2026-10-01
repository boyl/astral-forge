#ifndef AAMOD_LOG_H
#define AAMOD_LOG_H

#include <stdarg.h>
#include "aamod/aamod.h"

namespace aamod {

// Initialises the log. `dir` is created recursively if needed.
// Logging is a no-op until this succeeds (so it is safe from DllMain).
bool log_open(const wchar_t* dir);
void log_close();
bool log_is_open();

// Path of the active log file (empty if not open). UTF-8.
const char* log_path();

void log_write(int level, const char* fmt, ...);
void log_writev(int level, const char* fmt, va_list args);

} // namespace aamod

#define AAMOD_LOG(level, ...) ::aamod::log_write(level, __VA_ARGS__)
#define AAMOD_TRACE(...) ::aamod::log_write(AAMOD_LOG_TRACE, __VA_ARGS__)
#define AAMOD_DEBUG(...) ::aamod::log_write(AAMOD_LOG_DEBUG, __VA_ARGS__)
#define AAMOD_INFO(...)  ::aamod::log_write(AAMOD_LOG_INFO,  __VA_ARGS__)
#define AAMOD_WARN(...)  ::aamod::log_write(AAMOD_LOG_WARN,  __VA_ARGS__)
#define AAMOD_ERROR(...) ::aamod::log_write(AAMOD_LOG_ERROR, __VA_ARGS__)

#endif // AAMOD_LOG_H

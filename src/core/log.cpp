#include "log.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

namespace aamod {

static HANDLE      g_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_lock;
static bool        g_lock_ready = false;
static char        g_path_utf8[MAX_PATH * 3] = { 0 };

static const char* level_name(int level)
{
    switch (level) {
    case AAMOD_LOG_TRACE: return "TRACE";
    case AAMOD_LOG_DEBUG: return "DEBUG";
    case AAMOD_LOG_INFO:  return "INFO ";
    case AAMOD_LOG_WARN:  return "WARN ";
    default:              return "ERROR";
    }
}

static bool make_dir_recursive(const wchar_t* path)
{
    if (CreateDirectoryW(path, NULL))
        return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return true;
    // create parent then retry
    wchar_t parent[MAX_PATH * 2];
    wcsncpy(parent, path, MAX_PATH * 2 - 1);
    parent[MAX_PATH * 2 - 1] = 0;
    wchar_t* slash = wcsrchr(parent, L'\\');
    if (!slash || slash == parent)
        return false;
    *slash = 0;
    if (!make_dir_recursive(parent))
        return false;
    return CreateDirectoryW(path, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool log_open(const wchar_t* dir)
{
    if (g_file != INVALID_HANDLE_VALUE)
        return true;
    if (!make_dir_recursive(dir))
        return false;

    wchar_t path[MAX_PATH * 2];
    _snwprintf_s(path, _TRUNCATE, L"%s\\aamod.log", dir);

    g_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_file == INVALID_HANDLE_VALUE)
        return false;

    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }
    WideCharToMultiByte(CP_UTF8, 0, path, -1, g_path_utf8, sizeof(g_path_utf8), NULL, NULL);
    return true;
}

void log_close()
{
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

bool log_is_open() { return g_file != INVALID_HANDLE_VALUE; }

const char* log_path() { return g_path_utf8; }

void log_writev(int level, const char* fmt, va_list args)
{
    if (g_file == INVALID_HANDLE_VALUE)
        return;

    char body[2048];
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[2304];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                        "[%02d:%02d:%02d.%03d][%s][tid %lu] %s\r\n",
                        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                        level_name(level), (unsigned long)GetCurrentThreadId(), body);
    if (n <= 0)
        return;

    if (g_lock_ready)
        EnterCriticalSection(&g_lock);
    DWORD written = 0;
    WriteFile(g_file, line, (DWORD)strlen(line), &written, NULL);
    if (g_lock_ready)
        LeaveCriticalSection(&g_lock);
}

void log_write(int level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_writev(level, fmt, args);
    va_end(args);
}

} // namespace aamod

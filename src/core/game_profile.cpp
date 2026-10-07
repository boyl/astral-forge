#include "game_profile.h"
#include "log.h"
#include <bcrypt.h>
#include <cstring>
#include <vector>

namespace aamod {
namespace {
AAModGameInfo identity = {};
const char* known_hash = "ca376d2b741f65c75c416317517d24c316dd8a32a4eae6559030797b3b9aa88b";
}

bool sha256_file(const wchar_t* path, char output[65], uint64_t* file_size)
{
    output[0] = 0;
    *file_size = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    LARGE_INTEGER length;
    bool ok = GetFileSizeEx(file, &length) && length.QuadPart >= 0;
    if (ok) ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::vector<unsigned char> buffer(1024 * 1024);
    uint64_t total = 0;
    while (ok) {
        DWORD bytes = 0;
        if (!ReadFile(file, buffer.data(), (DWORD)buffer.size(), &bytes, nullptr)) { ok = false; break; }
        if (!bytes) break;
        total += bytes;
        ok = BCryptHashData(hash, buffer.data(), bytes, 0) >= 0;
    }
    unsigned char digest[32];
    if (ok) ok = total == (uint64_t)length.QuadPart && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) return false;
    const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(digest); ++i) {
        output[i * 2] = hex[digest[i] >> 4];
        output[i * 2 + 1] = hex[digest[i] & 15];
    }
    output[64] = 0;
    *file_size = total;
    return true;
}

void game_profile_apply(AAModGameInfo& info)
{
    info.steam_app_id = 0;
    memset(info.profile_id, 0, sizeof(info.profile_id));
    memset(info.game_version, 0, sizeof(info.game_version));
    if (!info.sha256[0]) { info.identity_status = AAMOD_GAME_IDENTITY_UNAVAILABLE; return; }
    info.identity_status = AAMOD_GAME_UNKNOWN;
    if (info.file_size != 172132352 || strcmp(info.sha256, known_hash) != 0) return;
    info.identity_status = AAMOD_GAME_IDENTITY_MATCH;
    info.steam_app_id = 1280930;
    strcpy_s(info.profile_id, "astral-ascent-steam-win64-2.6.4-ca376d2b");
    strcpy_s(info.game_version, "2.6.4");
}

void game_profile_initialize()
{
    identity = {};
    identity.size = sizeof(identity);
    identity.version = AAMOD_GAME_INFO_VERSION;
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (n && n < sizeof(path) / sizeof(path[0]))
        sha256_file(path, identity.sha256, &identity.file_size);
    // The Windows loader has already validated this PE; no external raw pointer.
    const auto* base = (const unsigned char*)GetModuleHandleW(nullptr);
    const auto* dos = (const IMAGE_DOS_HEADER*)base;
    const auto* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    identity.pe_timestamp = nt->FileHeader.TimeDateStamp;
    identity.image_size = nt->OptionalHeader.SizeOfImage;
    game_profile_apply(identity);
    AAMOD_INFO("game identity: status=%u profile=%s version=%s sha256=%s file_size=%llu",
               identity.identity_status, identity.profile_id, identity.game_version, identity.sha256,
               (unsigned long long)identity.file_size);
}

uint32_t game_profile_query(AAModGameInfo* output, uint32_t output_size)
{
    if (!output || output_size < sizeof(AAModGameInfo)) return AAMOD_ERR_ARGUMENT;
    memcpy(output, &identity, sizeof(identity));
    return 0;
}
}

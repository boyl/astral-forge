#include "../src/core/game_profile.h"
#include <cstdio>
#include <cstring>
#define CHECK(e) do { if (!(e)) { printf("FAIL line %d: %s\n", __LINE__, #e); return 1; } } while (0)
int main()
{
    wchar_t temp[MAX_PATH], path[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, temp)); CHECK(GetTempFileNameW(temp, L"aap", 0, path));
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(f != INVALID_HANDLE_VALUE); CloseHandle(f);
    char digest[65]; uint64_t bytes = 123;
    CHECK(aamod::sha256_file(path, digest, &bytes));
    CHECK(bytes == 0 && !strcmp(digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(f != INVALID_HANDLE_VALUE); DWORD written;
    CHECK(WriteFile(f, "abc", 3, &written, nullptr)); CloseHandle(f);
    CHECK(aamod::sha256_file(path, digest, &bytes));
    CHECK(bytes == 3 && !strcmp(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(DeleteFileW(path));
    CHECK(!aamod::sha256_file(path, digest, &bytes) && digest[0] == 0 && bytes == 0);
    AAModGameInfo info = {};
    aamod::game_profile_apply(info); CHECK(info.identity_status == AAMOD_GAME_IDENTITY_UNAVAILABLE);
    strcpy_s(info.sha256, "ca376d2b741f65c75c416317517d24c316dd8a32a4eae6559030797b3b9aa88b");
    info.file_size = 172132352; aamod::game_profile_apply(info);
    CHECK(info.identity_status == AAMOD_GAME_IDENTITY_MATCH && info.steam_app_id == 1280930);
    info.sha256[63] = 'c'; aamod::game_profile_apply(info);
    CHECK(info.identity_status == AAMOD_GAME_UNKNOWN && !info.profile_id[0] && info.steam_app_id == 0);
    info.sha256[63] = 'b'; info.file_size--; aamod::game_profile_apply(info);
    CHECK(info.identity_status == AAMOD_GAME_UNKNOWN);
    aamod::game_profile_initialize();
    CHECK(aamod::game_profile_query(nullptr, sizeof(info)) == AAMOD_ERR_ARGUMENT);
    memset(&info, 0xaa, sizeof(info));
    CHECK(aamod::game_profile_query(&info, sizeof(info) - 1) == AAMOD_ERR_ARGUMENT);
    for (size_t i=0;i<sizeof(info);++i) CHECK(((unsigned char*)&info)[i] == 0xaa);
    struct { AAModGameInfo info; unsigned char canary[16]; } large;
    memset(&large, 0xaa, sizeof(large));
    CHECK(aamod::game_profile_query(&large.info, sizeof(large)) == 0);
    CHECK(large.info.size == sizeof(info) && large.info.version == 1);
    CHECK(large.info.identity_status == AAMOD_GAME_UNKNOWN && strlen(large.info.sha256) == 64);
    for (unsigned char b : large.canary) CHECK(b == 0xaa);
    puts("PASS: SHA256 vectors, exact profile match, unknown/unavailable hosts, query buffer ownership");
    return 0;
}

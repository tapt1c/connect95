#include "settings.h"
#include <shlobj.h>
#include <stdio.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "Settings check failed at line %d: %s\n", __LINE__, #expression); return 1; \
} } while (0)

/* Run only in a fresh Windows user profile / isolated WINEPREFIX. */
int main(int argc, char **argv)
{
    WCHAR directory[MAX_PATH], path[MAX_PATH], value[80];
    const WCHAR name[] = L"\x041f\x041a \x0414\x0430\x043d\x0438\x044d\x043b\x044f";
    HANDLE file;
    BYTE bom[2];
    DWORD count;
    (void)argv;
    CHECK(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, directory) == S_OK);
    CHECK(lstrlenW(directory) < MAX_PATH - 40);
    lstrcatW(directory, L"\\ConnectXP");
    lstrcpyW(path, directory);
    lstrcatW(path, L"\\settings.ini");
    if (argc == 1) {
        if (GetFileAttributesW(directory) != INVALID_FILE_ATTRIBUTES) {
            fprintf(stderr, "Use a clean test profile: ConnectXP data already exists.\n");
            return 77;
        }
        CHECK(settings_get(L"AutoConnect", TRUE));
        CHECK(!settings_get(L"StartInTray", FALSE));
        /* A file blocking the profile directory must produce a save error. */
        file = CreateFileW(directory, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(file != INVALID_HANDLE_VALUE);
        CloseHandle(file);
        CHECK(!settings_set(L"AutoConnect", FALSE));
        CHECK(DeleteFileW(directory));
        CHECK(settings_set(L"AutoConnect", FALSE));
        CHECK(settings_set(L"StartInTray", TRUE));
        CHECK(settings_set(L"Language", TRUE));
        CHECK(settings_set_string(L"DeviceName", name));
        CHECK(settings_set_string(L"InvalidBoolean", L"2"));
        CHECK(settings_get(L"InvalidBoolean", TRUE));
        CHECK(!settings_get(L"InvalidBoolean", FALSE));
        CHECK(!settings_get_string(L"DeviceName", value, 4));
        file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(file != INVALID_HANDLE_VALUE);
        CHECK(ReadFile(file, bom, sizeof(bom), &count, NULL));
        CloseHandle(file);
        CHECK(count == 2 && bom[0] == 0xff && bom[1] == 0xfe);
        puts("Settings written as UTF-16 in local AppData. Run again with --verify.");
    } else {
        CHECK(!settings_get(L"AutoConnect", TRUE));
        CHECK(settings_get(L"StartInTray", FALSE));
        CHECK(settings_get(L"Language", FALSE));
        CHECK(settings_get_string(L"DeviceName", value, ARRAYSIZE(value)));
        CHECK(lstrcmpW(value, name) == 0);
        CHECK(DeleteFileW(path));
        CHECK(RemoveDirectoryW(directory));
        puts("Settings survived process restart, including Cyrillic; test profile cleaned.");
    }
    return 0;
}

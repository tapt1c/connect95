#include "battery_icon.h"
#include "battery_level.h"
#include "../resources/resource.h"
#include <commctrl.h>

static HIMAGELIST images;

static HIMAGELIST load_strip(HINSTANCE module, int resource)
{
    HBITMAP bitmap = LoadImageW(module, MAKEINTRESOURCEW(resource), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    BITMAP details;
    HIMAGELIST list = NULL;
    if (!bitmap) return NULL;
    /* Other Windows versions may reuse resource IDs for different artwork. */
    if (GetObjectW(bitmap, sizeof(details), &details) == sizeof(details) &&
        details.bmWidth == 161 && details.bmHeight == 16) {
        list = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 10, 0);
        if (list && (ImageList_AddMasked(list, bitmap, RGB(255, 0, 255)) < 0 || ImageList_GetImageCount(list) != 10)) {
            ImageList_Destroy(list); list = NULL;
        }
    }
    DeleteObject(bitmap);
    return list;
}

HICON battery_icon_load(int charge, BOOL charging)
{
    if (!images) {
        WCHAR path[MAX_PATH];
        UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
        if (length && length + 14 < ARRAYSIZE(path)) {
            HMODULE module;
            lstrcatW(path, L"\\batmeter.dll");
            module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
            if (module) {
                images = load_strip(module, 301);
                FreeLibrary(module);
            }
        }
        if (!images) images = load_strip(GetModuleHandleW(NULL), IDB_BATTERY_STRIP);
    }
    return images ? ImageList_GetIcon(images, battery_image_index(charge, charging), ILD_NORMAL) : NULL;
}

void battery_icon_free(void)
{
    if (images) ImageList_Destroy(images);
    images = NULL;
}

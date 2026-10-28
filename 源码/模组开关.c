#include "模组开关.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static unsigned features = PAL3_MOD_ALL;

static int exists(const char *directory, const char *name)
{
    char path[MAX_PATH];
    DWORD attrs;
    if (snprintf(path, sizeof(path), "%smods\\pal3\\%s", directory, name) >= sizeof(path)) return 0;
    attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void PAL3Mod_LoadDirectory(const char *directory)
{
    static const char *files[] = {"controller.enabled", "camera.enabled", "cheats.enabled"};
    static const unsigned flags[] = {PAL3_MOD_CONTROLLER, PAL3_MOD_CAMERA, PAL3_MOD_CHEATS};
    unsigned i;
    /* 安装包通过独立开关启用功能；开发者直接使用 DLL 时默认启用三项功能。 */
    features = PAL3_MOD_ALL;
    if (!exists(directory, "modular.enabled")) return;
    features = 0;
    for (i = 0; i < 3; ++i)
        if (exists(directory, files[i])) features |= flags[i];
}

void PAL3Mod_Initialize(void)
{
    char directory[MAX_PATH], *end;
    DWORD count = GetModuleFileNameA(NULL, directory, sizeof(directory));
    if (!count || count >= sizeof(directory)) { features = 0; return; }
    end = strrchr(directory, '\\');
    if (!end) { features = 0; return; }
    end[1] = 0;
    PAL3Mod_LoadDirectory(directory);
}

int PAL3Mod_Enabled(unsigned feature) { return (features & feature) != 0; }
unsigned PAL3Mod_Flags(void) { return features; }

const wchar_t *PAL3Mod_SystemHint(void)
{
    if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER))
        return PAL3Mod_Enabled(PAL3_MOD_CHEATS) ?
            L"X 切换存读　Y 金手指　右摇杆滚动" : L"X 切换存读　右摇杆滚动";
    return PAL3Mod_Enabled(PAL3_MOD_CHEATS) ? L"Y 金手指" : L"";
}

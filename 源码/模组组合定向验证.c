#define PAL3_GAMEPAD_TEST
#include "手柄插件.c"

static char root[MAX_PATH];

static int flag_file(const char *name, int enabled)
{
    char path[260];
    HANDLE file;
    snprintf(path, sizeof(path), "%smods\\pal3\\%s", root, name);
    if (!enabled) { DeleteFileA(path); return 1; }
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(file);
    return 1;
}

int main(void)
{
    unsigned i;
    char parent[MAX_PATH];
    DWORD count = GetTempPathA(sizeof(parent), parent);
    if (!count || count >= sizeof(parent) || strlen(parent) > sizeof(root) - 60) return 1;
    snprintf(root, sizeof(root), "%spal3-mod-flags-test\\", parent);
    CreateDirectoryA(root, NULL);
    snprintf(parent, sizeof(parent), "%smods", root); CreateDirectoryA(parent, NULL);
    snprintf(parent, sizeof(parent), "%smods\\pal3", root); CreateDirectoryA(parent, NULL);
    if (!flag_file("modular.enabled", 1)) return 1;
    experience_rates[0] = 7;
    experience_rates[1] = 9;
    for (i = 0; i < 8; ++i) {
        unsigned expected = (i & 3) | ((i & 4) ? PAL3_MOD_CHEATS : 0);
        if (!flag_file("controller.enabled", i & 1) || !flag_file("camera.enabled", i & 2) ||
            !flag_file("cheats.enabled", i & 4)) return 1;
        PAL3Mod_LoadDirectory(root);
        if (PAL3Mod_Flags() != expected) return 2;
        if (!!wcsstr(PAL3Mod_SystemHint(), L"金手指") != !!(i & 4)) return 3;
        if (!(i & 4) && wcschr(PAL3Mod_SystemHint(), L'Y')) return 4;
        if (PAL3Gamepad_GetExperienceRate(0) != ((i & 4) ? 7 : 1) ||
            PAL3Gamepad_GetExperienceRate(1) != ((i & 4) ? 9 : 1)) return 5;
    }
    flag_file("controller.enabled", 0);
    flag_file("camera.enabled", 0);
    flag_file("cheats.enabled", 0);
    flag_file("modular.enabled", 0);
    PAL3Mod_LoadDirectory(root);
    if (PAL3Mod_Flags() != PAL3_MOD_ALL) return 6;
    puts("八种功能组合通过：未启用金手指无提示且倍率为一，旧整合包行为保持");
    return 0;
}

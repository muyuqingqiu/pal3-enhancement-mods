#ifndef PAL3_MOD_FEATURES_H
#define PAL3_MOD_FEATURES_H
#include <wchar.h>

#define PAL3_MOD_CONTROLLER 1u
#define PAL3_MOD_CAMERA     2u
#define PAL3_MOD_CHEATS     8u
#define PAL3_MOD_ALL        11u

void PAL3Mod_Initialize(void);
void PAL3Mod_LoadDirectory(const char *directory);
int PAL3Mod_Enabled(unsigned feature);
unsigned PAL3Mod_Flags(void);
const wchar_t *PAL3Mod_SystemHint(void);

#endif

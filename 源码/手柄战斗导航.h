#ifndef PAL3_COMBAT_NAV_H
#define PAL3_COMBAT_NAV_H

#include <stdint.h>
#include "手柄菜单导航.h"

struct CCBUI;

/* 仅在已核实的原版目标处理入口安装手柄选择，原版命令校验继续有效。 */
void PAL3CombatNav_Initialize(void);
/* 返回掩码表示本模块已接管的方向、确认或返回输入。 */
uint32_t PAL3CombatNav_Tick(const struct PAL3MenuNavInput *input);

/* 输入位沿用菜单导航：Switch 实体 A 为 XInput B，实体 B 为 XInput A。 */
/* combat_active 由调用方提供，供定向验证避开游戏进程全局地址。 */
uint32_t PAL3CombatNav_TickForUi(struct CCBUI *ui, int combat_active,
    const struct PAL3MenuNavInput *input);
void PAL3CombatNav_Reset(void);

#endif

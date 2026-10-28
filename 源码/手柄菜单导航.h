#ifndef PAL3_MENU_NAV_H
#define PAL3_MENU_NAV_H

#include <stdint.h>

/* held/pressed 是 XInput 按钮位；Switch 实体 A/B 分别映射到 XInput B/A。 */
struct PAL3MenuNavInput {
    uint16_t held;
    uint16_t pressed;
    int16_t stick_x;
    int16_t stick_y;
    int16_t right_stick_y;
    /* L 持有期间的鼠标模式，与是否实际组合使用鼠标分开传入。 */
    uint8_t mouse_mode;
    uint8_t mouse_combo_used;
    /* 中立输入也要区分手柄已就绪和失焦、断连。 */
    uint8_t controller_active;
    uint32_t now_ms;
};

enum PAL3MenuNavAction {
    PAL3_MENU_NAV_UP       = 1u << 0,
    PAL3_MENU_NAV_DOWN     = 1u << 1,
    PAL3_MENU_NAV_LEFT     = 1u << 2,
    PAL3_MENU_NAV_RIGHT    = 1u << 3,
    PAL3_MENU_NAV_CONFIRM  = 1u << 4,
    PAL3_MENU_NAV_CANCEL   = 1u << 5,
    PAL3_MENU_NAV_CATEGORY = 1u << 6,
    PAL3_MENU_NAV_MODAL    = 1u << 7,
    PAL3_MENU_NAV_REPEAT   = 1u << 8
};

enum PAL3MenuNavDialogChoice {
    PAL3_MENU_NAV_DIALOG_NONE = 0,
    PAL3_MENU_NAV_DIALOG_YES = 1,
    PAL3_MENU_NAV_DIALOG_NO = 2
};

enum PAL3MenuNavDialogType {
    PAL3_MENU_NAV_DIALOG_TYPE_YES = 0,
    PAL3_MENU_NAV_DIALOG_TYPE_NO = 1,
    PAL3_MENU_NAV_DIALOG_TYPE_YESNO = 2,
    PAL3_MENU_NAV_DIALOG_TYPE_MSGBOX = 3
};

struct PAL3MenuNavRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

/* 返回方向上最近的矩形；没有目标时返回 from。 */
uint32_t PAL3MenuNav_SelectNeighbor(const struct PAL3MenuNavRect *rects,
    uint32_t count, uint32_t from, int32_t dx, int32_t dy);
/* 分类按钮按横向顺序选择；锁定分类会跳过，越界或无目标时返回 count。 */
uint32_t PAL3MenuNav_NextCategory(const uint8_t *enabled, uint32_t count,
    uint32_t current, int32_t direction);
/* 将原版分类控件 ID 映射到 StateUI 的实际页面编号；无效 ID 返回 8。 */
uint32_t PAL3MenuNav_CategoryPageIndex(uint32_t category_id);
/* 按实际显示页号定位当前分类下标；无匹配时返回 -1。 */
int32_t PAL3MenuNav_FindCategoryForPage(const uint32_t *page_indexes,
    uint32_t count, uint32_t current_page);
/* 状态页非零页面按 B 返回页 0；页 0 才关闭状态页。 */
int PAL3MenuNav_ShouldReturnToStateRoot(uint32_t page_index);
/* 按原版弹窗类型和按钮可见状态选择 Yes/No；physical_a 表示实体 A。 */
enum PAL3MenuNavDialogChoice PAL3MenuNav_SelectDialogChoice(
    uint32_t dialog_type, int physical_a, int yes_available, int no_available);
uint32_t PAL3MenuNav_Tick(const struct PAL3MenuNavInput *input);
void PAL3MenuNav_Initialize(void);
/* 单线程只读采样入口；不推进输入，只记录菜单树变化。 */
void PAL3MenuNav_DiagnosticObserve(void);
void PAL3MenuNav_Render(void);
struct UIWnd;
/* 勾选控件由原版 Render 调用，保留 PAL3patch 的当前缩放。 */
void PAL3MenuNav_RenderChoice(struct UIWnd *window);
void PAL3MenuNav_Reset(void);
int PAL3Gamepad_GetExperienceRate(int skill);
void PAL3Gamepad_SetExperienceRate(int skill, int rate);
/* 自制页面拦截鼠标点击，不能同时点到下面的原版存档槽。 */
int PAL3MenuNav_MouseMessage(unsigned message, long position);
int PAL3MenuNav_ToggleCheat(void);
uint32_t PAL3MenuNav_CheatTick(const struct PAL3MenuNavInput *input);
/* 只判断 L 单按释放是否可切分类；控件识别和原版点击由 Tick 执行。 */
int PAL3MenuNav_ShouldSwitchCategoryOnLRelease(int was_held, int is_held,
    int used_for_mouse, uint8_t directions_while_held);
int PAL3MenuNav_ShouldNavigate(int left_shoulder_held, int mouse_mode);

#endif

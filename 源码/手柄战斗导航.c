#define PLUGIN_NAME "PAL3Gamepad"
#define USE_PAL3_DEFINITIONS
#include "PAL3patch.h"
#include "手柄战斗导航.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define PAL3_COMBAT_NAV_BUTTON_VTABLE_RVA 0x0016B018u
#define PAL3_COMBAT_NAV_BASE_BUTTON_VTABLE_RVA 0x0016AC70u
#define PAL3_COMBAT_NAV_BUTTON_MESSAGE 0x3E9u
/* 通过 OnMessage 实现地址识别原版 CCBControlWindow 父窗体。 */
#define PAL3_COMBAT_NAV_CONTROL_WINDOW_ONMESSAGE_RVA 0x001195E0u
#define PAL3_COMBAT_NAV_MIN_BUTTON_ID 0x103u
#define PAL3_COMBAT_NAV_MAX_BUTTON_ID 0x10Cu
#define PAL3_COMBAT_NAV_MAX_BUTTONS 32
/* 普通战斗根窗体现有 272 个直属控件。 */
#define PAL3_COMBAT_NAV_MAX_CHILDREN 512
#define PAL3_COMBAT_NAV_MAX_DEPTH 10
#define PAL3_COMBAT_NAV_DEADZONE 7849
#define PAL3_COMBAT_NAV_INITIAL_REPEAT_MS 360u
#define PAL3_COMBAT_NAV_REPEAT_MS 140u

#define XINPUT_DPAD_UP        0x0001u
#define XINPUT_DPAD_DOWN      0x0002u
#define XINPUT_DPAD_LEFT      0x0004u
#define XINPUT_DPAD_RIGHT     0x0008u
#define XINPUT_BACK           0x0020u
#define XINPUT_LEFT_SHOULDER  0x0100u
#define XINPUT_RIGHT_SHOULDER 0x0200u
#define XINPUT_A              0x1000u
#define XINPUT_B              0x2000u

struct combat_button {
    struct UIButton *button;
    struct PAL3MenuNavRect rect;
};

static struct CCBUI *s_ui;
static struct UIButton *s_selected;
static uint32_t s_last_command_id;
static uint8_t s_previous_directions;
static uint32_t s_repeat_at[4];
static uint8_t s_mouse_mode;
static struct PAL3MenuNavInput s_target_input;
static uint32_t s_input_tick;
static uint32_t s_target_tick;
static void *s_target_step;
static void *s_target_role;
static int s_target_index = -1;
static int s_target_engaged;
static int s_hover_override;
static struct UIWnd *s_list_window;
static uint8_t s_list_left_held;
static uint8_t s_list_left_used;
static struct UIListBox *s_list;
static int s_list_row = -1;
static uintptr_t s_list_update;
static struct CCBLineupWindow *s_lineup;
static int s_lineup_actor = -1;
static int s_lineup_slot = -1;
static int s_lineup_origin = -1;
static int s_lineup_held;
static int s_lineup_dropping;
static int s_lineup_engaged;
static uint8_t direction_state(const struct PAL3MenuNavInput *input);
static int direction_event(uint8_t held, uint8_t bit, uint8_t index, uint32_t now);
static int is_ai_choice(struct UIWnd *window);

#define PAL3_COMBAT_TARGET_HANDLER_RVA 0x000FD3D0u
#define PAL3_COMBAT_TARGET_REGISTER_RVA 0x000DEFF5u
#define PAL3_COMBAT_TARGET_HOVER_RVA 0x0010A4B0u

#ifndef PAL3_COMBAT_NAV_TEST
static MAKE_THISCALL(int, selected_list_update, struct UIWnd *window,
    float delta_time, int have_input)
{
    struct UIListBox *list = (struct UIListBox *)window;
    if (PAL3_s_gamestate == GAME_COMBAT && list == s_list &&
        !s_mouse_mode && s_list_row >= 0) {
        /* 手柄已调用原版列表按键处理，禁止旧鼠标位置每帧改写选择和物品说明。 */
        list->m_onsel = s_list_row - list->m_firstshow;
        have_input = 0;
    }
    return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(s_list_update,
        int, struct UIWnd *, float, int), window, delta_time, have_input);
}

static MAKE_THISCALL(void, selected_button_render, struct UIWnd *window)
{
    struct UIButton *button = (struct UIButton *)window;
    enum UIButton_UIBUTTON_STATE previous = button->m_state;
    /* 在原版绘制流程内高亮，沿用当前缩放，避免另画一个错位图标。 */
    if (button == s_selected && !s_mouse_mode &&
        PAL3_s_gamestate == GAME_COMBAT && PAL3_m_pCBSystem &&
        PAL3_m_pCBSystem->m_pUI == s_ui &&
        !(s_target_step && s_target_tick == s_input_tick))
        button->m_state = UIBUTTON_MOUSEON;
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) + 0x0003AD40u,
        void, struct UIWnd *), window);
    button->m_state = previous;
}

static MAKE_THISCALL(void, selected_choice_render, struct UIWnd *window)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x00040F30u,
        void, struct UIWnd *), window);
    PAL3MenuNav_RenderChoice(window);
    if (s_selected && window == &s_selected->baseclass && !s_mouse_mode &&
        PAL3_s_gamestate == GAME_COMBAT && PAL3_m_pCBSystem &&
        PAL3_m_pCBSystem->m_pUI == s_ui && is_ai_choice(window)) {
        const RECT *r = &window->m_rect;
        const uint32_t color = 0xFFFFFF00u;
        void (__cdecl *draw)(int, int, int, int, const uint32_t *) =
            (void (__cdecl *)(int, int, int, int, const uint32_t *))(base + 0x00140070u);
        /* 在原控件绘制期间画焦点框，沿用 PAL3patch 当前缩放，不改变原有勾选状态。 */
        draw(r->left, r->top, r->right, r->top + 2, &color);
        draw(r->left, r->bottom - 2, r->right, r->bottom, &color);
        draw(r->left, r->top, r->left + 2, r->bottom, &color);
        draw(r->right - 2, r->top, r->right, r->bottom, &color);
    }
}

static int lineup_override(void)
{
    return s_lineup && s_lineup_engaged && PAL3_s_gamestate == GAME_COMBAT &&
        PAL3_m_pCBSystem && PAL3_m_pCBSystem->m_pUI &&
        PAL3_m_pCBSystem->m_pUI->m_pLineupWindow == s_lineup &&
        s_lineup->baseclass.baseclass.m_bvisible;
}

static MAKE_THISCALL(short, lineup_mouse_held, void *input, unsigned key)
{
    if (lineup_override()) return (short)s_lineup_held;
    return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) + 0x000022C0u,
        short, void *, unsigned), input, key);
}

static MAKE_THISCALL(void, lineup_mouse_position, void *input, POINT *point)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (lineup_override() && s_lineup_actor >= 0 && s_lineup_actor < 5) {
        const RECT *r = &s_lineup->m_pFace[s_lineup_actor]->baseclass.m_rect;
        if (s_lineup_held && s_lineup_slot >= 1 && s_lineup_slot <= 6) {
            const int *left = (const int *)(base + 0x0017024Cu);
            const int *top = (const int *)(base + 0x00170268u);
            point->x = left[s_lineup_slot] + 9 + 20;
            point->y = top[s_lineup_slot] + 232 + 19;
        } else {
            point->x = (r->left + r->right) / 2;
            point->y = (r->top + r->bottom) / 2;
        }
        return;
    }
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x000022E0u,
        void, void *, POINT *), input, point);
}

static MAKE_THISCALL(int, lineup_pick_face, struct CCBLineupWindow *window, POINT point)
{
    if (lineup_override() && window == s_lineup) return s_lineup_actor;
    return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) + 0x0011B9A0u,
        int, struct CCBLineupWindow *, POINT), window, point);
}

static MAKE_THISCALL(int, lineup_drop_slot, struct CCBLineupWindow *window, int actor)
{
    if (lineup_override() && window == s_lineup) return s_lineup_slot;
    return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) + 0x0011BAB0u,
        int, struct CCBLineupWindow *, int), window, actor);
}

static MAKE_THISCALL(void, lineup_face_render, struct UIWnd *window)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x00042890u,
        void, struct UIWnd *), window);
    if (lineup_override() && s_lineup_actor >= 0 && s_lineup_actor < 5 &&
        window == &s_lineup->m_pFace[s_lineup_actor]->baseclass && window->m_bvisible) {
        const RECT *r = &window->m_rect;
        const uint32_t color = 0xFFFFFF00u;
        void (__cdecl *draw)(int, int, int, int, const uint32_t *) =
            (void (__cdecl *)(int, int, int, int, const uint32_t *))(base + 0x00140070u);
        draw(r->left, r->top, r->right, r->top + 2, &color);
        draw(r->left, r->bottom - 2, r->right, r->bottom, &color);
        draw(r->left, r->top, r->left + 2, r->bottom, &color);
        draw(r->right - 2, r->top, r->right, r->bottom, &color);
    }
}

static MAKE_THISCALL(char, target_hover, void *role)
{
    if (s_hover_override) return role == s_target_role;
    return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) + PAL3_COMBAT_TARGET_HOVER_RVA,
        char, void *), role);
}

static uint32_t read_control_u32(const void *control, uint32_t offset)
{
    uint32_t value;
    memcpy(&value, (const uint8_t *)control + offset, sizeof(value));
    return value;
}

static MAKE_THISCALL(int, target_handler, void *control, uint32_t argument)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    struct CCBSystem *system = PAL3_m_pCBSystem;
    const struct tagCmdData *command;
    struct GRP_KEYREG previous_left, previous_right;
    void *roles[11];
    uint8_t valid[11] = {0};
    uint32_t task, step, flags;
    uint32_t row;
    uint8_t directions;
    uint8_t *step_data;
    int count = 0, i, first = -1, direction = 0, fresh, entered;
    int result;

    /* 目标回调属于 CCBStage；CCBControl 只管理命令菜单，不保存这份目标任务。 */
    if (!system || (void *)system->m_pStage != control ||
        PAL3_s_gamestate != GAME_COMBAT || system->m_eState != CBCS_Running)
        goto original;
    task = read_control_u32(control, 0xA0);
    step = read_control_u32(control, 0xA4);
    /* 原版任务数组共 256 行，每行有 32 个 0x50 字节的处理步骤。 */
    if (task >= 256 || step >= 32) goto original;
    row = task * 0xA48u;
    step_data = (uint8_t *)control + row + 0xB8u + step * 0x50u;
    command = (const struct tagCmdData *)((const uint8_t *)control + row + 0xAC0u);
    memcpy(&flags, step_data + 0x2C, sizeof(flags));
    fresh = s_target_tick != s_input_tick;
    entered = s_target_step != step_data || s_target_tick + 1u != s_input_tick;
    if (!fresh) entered = 0;
    if (entered) {
        s_target_step = step_data;
        s_target_index = -1;
        s_target_engaged = 0;
        s_previous_directions = 0;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
    }
    s_target_tick = s_input_tick;
    if (!s_target_input.controller_active || s_target_input.mouse_mode ||
        (s_target_input.held & XINPUT_LEFT_SHOULDER)) {
        s_target_engaged = 0;
        s_previous_directions = 0;
        goto original;
    }
    directions = direction_state(&s_target_input);
    if (directions || s_target_input.pressed) s_target_engaged = 1;
    if (!s_target_engaged) goto original;

    memcpy(roles, (const uint8_t *)control + 0x18, sizeof(roles));
    for (i = 0; i < 11; ++i) {
        int ally;
        if (!roles[i]) continue;
        if (flags & 1u) {
            if (!THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
                    base + 0x000EECB0u, char, struct CCBRoleState *,
                    const struct tagCmdData *, int), system->m_pRoleState, command, i))
                continue;
        } else {
            /* 原版己方分支先检查角色在场，不套用敌方命令筛选；复活等限制仍由原版处理。 */
            const uint8_t *state = (const uint8_t *)system->m_pRoleState;
            if (!state[0x241u + i * 0x37Cu]) continue;
        }
        ally = THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
            base + 0x000ED040u, char, struct CCBRoleState *, int),
            system->m_pRoleState, i);
        /* 与原版目标阶段的敌我分支保持一致，死亡和技能限制由原版筛选。 */
        if (((flags & 1u) != 0) == (ally != 0)) continue;
        valid[i] = 1;
        if (first < 0) first = i;
        ++count;
    }
    if (s_target_index < 0 || s_target_index >= 11 || !valid[s_target_index]) {
        int current = (int)read_control_u32((void *)base, 0x013AAD3Cu);
        /* 接管时沿用画面当前目标，避免第一次确认或切换悄悄改成队首。 */
        s_target_index = current >= 0 && current < 11 && valid[current] ? current : first;
    }
    if (fresh) {
        if ((directions & PAL3_MENU_NAV_UP) &&
            direction_event(directions, PAL3_MENU_NAV_UP, 0, s_target_input.now_ms))
            direction = -1;
        else if ((directions & PAL3_MENU_NAV_DOWN) &&
            direction_event(directions, PAL3_MENU_NAV_DOWN, 1, s_target_input.now_ms))
            direction = 1;
        else if ((directions & PAL3_MENU_NAV_LEFT) &&
            direction_event(directions, PAL3_MENU_NAV_LEFT, 2, s_target_input.now_ms))
            direction = -1;
        else if ((directions & PAL3_MENU_NAV_RIGHT) &&
            direction_event(directions, PAL3_MENU_NAV_RIGHT, 3, s_target_input.now_ms))
            direction = 1;
        s_previous_directions = directions;
    }
    if (direction && count) {
        for (i = 1; i <= 11; ++i) {
            int next = (s_target_index + direction * i + 11) % 11;
            if (valid[next]) { s_target_index = next; break; }
        }
    }
    s_target_role = s_target_index >= 0 ? roles[s_target_index] : NULL;
    previous_left = g_input.KeyInfo[0];
    previous_right = g_input.KeyInfo[1];
    /* 只把按下沿交给原版目标处理，不直接写结果命令或跳过确认校验。 */
    if (fresh && !entered && (s_target_input.pressed & XINPUT_B) && count) {
        g_input.KeyInfo[0].Data = 1;
        g_input.KeyInfo[0].PressTime = 1;
    }
    if (fresh && !entered && (s_target_input.pressed & XINPUT_A)) {
        g_input.KeyInfo[1].Data = 1;
        g_input.KeyInfo[1].PressTime = 1;
    }
    s_hover_override = 1;
    result = THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        base + PAL3_COMBAT_TARGET_HANDLER_RVA, int, void *, uint32_t),
        control, argument);
    s_hover_override = 0;
    g_input.KeyInfo[0] = previous_left;
    g_input.KeyInfo[1] = previous_right;
    return result;
original:
    return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        base + PAL3_COMBAT_TARGET_HANDLER_RVA, int, void *, uint32_t),
        control, argument);
}

void PAL3CombatNav_Initialize(void)
{
    static const uint32_t hover_calls[] = {0x000FD4FCu, 0x000FD5D9u};
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    unsigned i;
    if (read_control_u32((void *)base, PAL3_COMBAT_TARGET_REGISTER_RVA) !=
        base + PAL3_COMBAT_TARGET_HANDLER_RVA) {
        plog("[PAL3CombatNav] target handler registration differs; left unchanged");
        return;
    }
    for (i = 0; i < 2; ++i) {
        const uint8_t *call = (const uint8_t *)(base + hover_calls[i]);
        int32_t relative;
        memcpy(&relative, call + 1, sizeof(relative));
        if (*call != 0xE8 || (uintptr_t)(call + 5) + relative !=
                base + PAL3_COMBAT_TARGET_HOVER_RVA) {
            plog("[PAL3CombatNav] target hover call differs; left unchanged");
            return;
        }
    }
    for (i = 0; i < 2; ++i)
        make_call((unsigned)(base + hover_calls[i]), target_hover);
    make_pointer((unsigned)(base + PAL3_COMBAT_TARGET_REGISTER_RVA), target_handler);
    s_list_update = read_control_u32((void *)base, 0x0016AD48u);
    make_pointer((unsigned)(base + 0x0016AD48u), selected_list_update);
    if (read_control_u32((void *)base, PAL3_COMBAT_NAV_BUTTON_VTABLE_RVA + 4) ==
            base + 0x0003AD40u &&
        read_control_u32((void *)base, PAL3_COMBAT_NAV_BASE_BUTTON_VTABLE_RVA + 4) ==
            base + 0x0003AD40u) {
        make_pointer((unsigned)(base + PAL3_COMBAT_NAV_BUTTON_VTABLE_RVA + 4),
            selected_button_render);
        make_pointer((unsigned)(base + PAL3_COMBAT_NAV_BASE_BUTTON_VTABLE_RVA + 4),
            selected_button_render);
    }
    if (read_control_u32((void *)base, 0x0016AD8Cu) == base + 0x00040F30u)
        make_pointer((unsigned)(base + 0x0016AD8Cu), selected_choice_render);
    /* 只替换阵列拖动路径的采样，角色状态、换位校验与命令仍交给原版。 */
    make_call((unsigned)(base + 0x000DE392u), lineup_mouse_position);
    make_call((unsigned)(base + 0x000DE3A1u), lineup_mouse_held);
    make_call((unsigned)(base + 0x000DE3D0u), lineup_pick_face);
    {
        static const uint32_t calls[] = {0xDE4F8u, 0xDE515u, 0xDE53Au, 0xDE570u};
        for (i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i)
            make_call((unsigned)(base + calls[i]), lineup_drop_slot);
    }
    if (read_control_u32((void *)base, 0x0016ADD4u) == base + 0x00042890u)
        make_pointer((unsigned)(base + 0x0016ADD4u), lineup_face_render);
    plog("[PAL3CombatNav] original target filtering and selection connected");
}
#endif

#ifndef PAL3_COMBAT_NAV_DIAGNOSTIC
#define PAL3_COMBAT_NAV_DIAGNOSTIC 0
#endif

#if PAL3_COMBAT_NAV_DIAGNOSTIC && defined(_WIN32)
#define PAL3_COMBAT_NAV_DIAG_SLOTS 16

struct combat_nav_diag_slot {
    struct CCBUI *ui;
    struct UIWnd *window;
    const char *name;
    struct UIWndVtbl *original;
    struct UIWndVtbl copy;
    int in_message;
    int used;
};

static struct combat_nav_diag_slot s_diag_slots[PAL3_COMBAT_NAV_DIAG_SLOTS];
static struct CCBSystem *s_diag_snapshot_system;
static struct CCBUI *s_diag_snapshot_ui;
static struct tagCmdData s_diag_snapshot_commands[11];
static int s_diag_snapshot_valid;
static int s_diag_ambiguous_reuse_logged;
static int s_diag_slots_exhausted_logged;

static void diag_reset_snapshot(void)
{
    s_diag_snapshot_system = NULL;
    s_diag_snapshot_ui = NULL;
    s_diag_snapshot_valid = 0;
}

static struct combat_nav_diag_slot *diag_find_slot(struct UIWndVtbl *vtable)
{
    int i;
    for (i = 0; i < PAL3_COMBAT_NAV_DIAG_SLOTS; ++i)
        if (s_diag_slots[i].used && &s_diag_slots[i].copy == vtable)
            return &s_diag_slots[i];
    return NULL;
}

static int diag_snapshot_current(struct CCBUI *ui,
    struct tagCmdData commands[11])
{
    struct CCBSystem *system = PAL3_m_pCBSystem;
    if (!system || PAL3_s_gamestate != GAME_COMBAT ||
        system->m_eState != CBCS_Running || system->m_pUI != ui)
        return 0;
    {
        int i;
        for (i = 0; i < 11; ++i)
            commands[i] = system->m_Player[i].LastRoleCmd;
    }
    return 1;
}

static void diag_log_command(const char *phase, unsigned message,
    const char *window_name, unsigned wparam, unsigned lparam, int player,
    const struct tagCmdData *command)
{
    char source_roles[12];
    char destination_roles[12];
    int i;

    for (i = 0; i < 11; ++i) {
        source_roles[i] = command->bSrcRole[i] ? '1' : '0';
        destination_roles[i] = command->bDestRole[i] ? '1' : '0';
    }
    source_roles[11] = '\0';
    destination_roles[11] = '\0';
    plog("[PAL3CombatNav] %s window=%s msg=%03X wparam=%u lparam=%u player=%d valid=%d kind=%u cmd=%lu src=%d dst=%d multi=%d/%d srcRoles=%s dstRoles=%s",
        phase, window_name, message, wparam, lparam, player, command->bValid, command->dwCmdKind,
        command->dwCmdID, command->nSrcRole, command->nDestRole,
        command->bMultiSrcRole, command->bMultiDestRole,
        source_roles, destination_roles);
}

static void diag_log_tick_command(unsigned long tick, struct CCBUI *ui,
    int player, const struct tagCmdData *command)
{
    char source_roles[12];
    char destination_roles[12];
    int i;

    for (i = 0; i < 11; ++i) {
        source_roles[i] = command->bSrcRole[i] ? '1' : '0';
        destination_roles[i] = command->bDestRole[i] ? '1' : '0';
    }
    source_roles[11] = '\0';
    destination_roles[11] = '\0';
    plog("[PAL3CombatNav] tick changed tick=%lu ui=%p player=%d valid=%d kind=%u cmd=%lu src=%d dst=%d multi=%d/%d srcRoles=%s dstRoles=%s",
        tick, (void *)ui, player, command->bValid, command->dwCmdKind,
        command->dwCmdID, command->nSrcRole, command->nDestRole,
        command->bMultiSrcRole, command->bMultiDestRole,
        source_roles, destination_roles);
}

static int diag_same_command(const struct tagCmdData *a,
    const struct tagCmdData *b)
{
    return a->dwCmdKind == b->dwCmdKind && a->dwCmdID == b->dwCmdID &&
        a->nSrcRole == b->nSrcRole && a->nDestRole == b->nDestRole &&
        a->bMultiSrcRole == b->bMultiSrcRole &&
        a->bMultiDestRole == b->bMultiDestRole && a->bValid == b->bValid &&
        memcmp(a->bSrcRole, b->bSrcRole, sizeof(a->bSrcRole)) == 0 &&
        memcmp(a->bDestRole, b->bDestRole, sizeof(a->bDestRole)) == 0;
}

static MAKE_THISCALL(int, diag_on_message, struct UIWnd *this,
    unsigned message, unsigned wparam, unsigned lparam)
{
    struct combat_nav_diag_slot *slot = diag_find_slot(this->vfptr);
    struct tagCmdData before[11];
    struct tagCmdData after[11];
    int have_before;
    int result;
    int i;

    if (!slot || !slot->original || !slot->original->OnMessage) return 0;
    if (slot->window != this) {
        return THISCALL_WRAPPER(slot->original->OnMessage, this,
            message, wparam, lparam);
    }
    if (slot->in_message) {
        return THISCALL_WRAPPER(slot->original->OnMessage, this,
            message, wparam, lparam);
    }
    slot->in_message = 1;
    /* 鼠标悬停消息每帧都会到达，只记录实际操作，避免淹没命令变化。 */
    have_before = message != 0x3EAu && diag_snapshot_current(slot->ui, before);
    if (have_before) {
        plog("[PAL3CombatNav] before window=%s msg=%03X wparam=%u lparam=%u ui=%p window_ptr=%p",
            slot->name, message, wparam, lparam, (void *)slot->ui, (void *)this);
        for (i = 0; i < 11; ++i)
            if (before[i].bValid)
                diag_log_command("before", message, slot->name,
                    wparam, lparam, i, &before[i]);
    }

    result = THISCALL_WRAPPER(slot->original->OnMessage, this,
        message, wparam, lparam);

    /* 地址和状态仍匹配不代表回调前后是同一代 system，日志只作事件前后线索。 */
    if (have_before && diag_snapshot_current(slot->ui, after)) {
        for (i = 0; i < 11; ++i)
            if (!diag_same_command(&before[i], &after[i]))
                diag_log_command("after changed", message, slot->name,
                    wparam, lparam, i, &after[i]);
    }
    slot->in_message = 0;
    return result;
}

static void diag_observe_current_tick(struct CCBSystem *system,
    struct CCBUI *ui)
{
    struct tagCmdData current[11];
    unsigned long tick;
    int i;

    if (!system || !ui || system != PAL3_m_pCBSystem ||
        PAL3_s_gamestate != GAME_COMBAT || system->m_eState != CBCS_Running ||
        system->m_pUI != ui) {
        diag_reset_snapshot();
        return;
    }

    for (i = 0; i < 11; ++i)
        current[i] = system->m_Player[i].LastRoleCmd;

    if (!s_diag_snapshot_valid || s_diag_snapshot_system != system ||
        s_diag_snapshot_ui != ui) {
        s_diag_snapshot_system = system;
        s_diag_snapshot_ui = ui;
        memcpy(s_diag_snapshot_commands, current, sizeof(current));
        s_diag_snapshot_valid = 1;
        return;
    }

    tick = (unsigned long)GetTickCount();
    for (i = 0; i < 11; ++i)
        if (!diag_same_command(&s_diag_snapshot_commands[i], &current[i]))
            diag_log_tick_command(tick, ui, i, &current[i]);
    /* 时间戳只用于对照日志，不表示命令变化由某条消息触发。 */
    memcpy(s_diag_snapshot_commands, current, sizeof(current));
}

static void diag_arm_window(struct CCBUI *ui, struct UIWnd *window,
    const char *name)
{
    struct combat_nav_diag_slot *slot;
    struct UIWnd *root = ui ? &ui->baseclass.baseclass : NULL;
    struct UIWnd *ancestor;
    uint32_t depth;
    int i;

    if (!ui || !window || !window->vfptr || !window->vfptr->OnMessage) return;
    /* 只挂当前战斗树里实际可见、有效的窗体，不追未知子对象。 */
    ancestor = window;
    for (depth = 0; ancestor && depth <= PAL3_COMBAT_NAV_MAX_DEPTH; ++depth) {
        if (!ancestor->m_bcreateok || !ancestor->m_bvisible || !ancestor->m_benable)
            return;
        if (ancestor == root) break;
        ancestor = ancestor->m_pfather;
    }
    if (ancestor != root) return;

    slot = diag_find_slot(window->vfptr);
    if (slot) {
        if ((slot->ui != ui || slot->window != window) &&
            !s_diag_ambiguous_reuse_logged) {
            plog("[PAL3CombatNav] ambiguous reused window address; diagnostic skipped name=%s", name);
            s_diag_ambiguous_reuse_logged = 1;
        }
        return;
    }
    for (i = 0; i < PAL3_COMBAT_NAV_DIAG_SLOTS; ++i) {
        if (!s_diag_slots[i].used) {
            slot = &s_diag_slots[i];
            slot->used = 1;
            slot->ui = ui;
            slot->window = window;
            slot->name = name;
            slot->in_message = 0;
            slot->original = window->vfptr;
            slot->copy = *window->vfptr;
            slot->copy.OnMessage = diag_on_message;
            window->vfptr = &slot->copy;
            plog("[PAL3CombatNav] OnMessage observer installed name=%s ui=%p window=%p",
                name, (void *)ui, (void *)window);
            return;
        }
    }
    if (!s_diag_slots_exhausted_logged) {
        plog("[PAL3CombatNav] observer slots exhausted; no new window hooked name=%s", name);
        s_diag_slots_exhausted_logged = 1;
    }
}

static void diag_arm_current_ui(struct CCBUI *ui)
{
    struct CCBSystem *system = PAL3_m_pCBSystem;
    struct UIWnd *windows[8];
    static const char *names[] = {
        "root", "main", "item", "magic", "skill", "ai", "lineup", "protect",
    };
    unsigned i;

    if (!ui || !system || PAL3_s_gamestate != GAME_COMBAT ||
        system->m_eState != CBCS_Running || system->m_pUI != ui)
        return;
    windows[0] = (struct UIWnd *)&ui->baseclass.baseclass;
    windows[1] = (struct UIWnd *)ui->m_pMain;
    windows[2] = (struct UIWnd *)ui->m_pItemWindow;
    windows[3] = (struct UIWnd *)ui->m_pMagicWindow;
    windows[4] = (struct UIWnd *)ui->m_pSkillWindow;
    windows[5] = (struct UIWnd *)ui->m_pAIWindow;
    windows[6] = (struct UIWnd *)ui->m_pLineupWindow;
    windows[7] = (struct UIWnd *)ui->m_pProtectWindow;
    /* 根窗体已有通用界面诊断，再替换函数表会让其原版 Render 无法执行。 */
    for (i = 1; i < sizeof(windows) / sizeof(windows[0]); ++i)
        diag_arm_window(ui, windows[i], names[i]);
}
#endif

static int time_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static int is_plain_button(struct UIWnd *window)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    /* 原版战斗主命令使用 UIButton 基类，与另一种按钮共用 Render。 */
    return window && (window->vfptr == (struct UIWndVtbl *)
        (base + PAL3_COMBAT_NAV_BUTTON_VTABLE_RVA) ||
        window->vfptr == (struct UIWndVtbl *)
        (base + PAL3_COMBAT_NAV_BASE_BUTTON_VTABLE_RVA));
}

static int is_ai_choice(struct UIWnd *window)
{
    return s_ui && window && window->m_pfather == (struct UIWnd *)s_ui->m_pAIWindow &&
        window->m_id >= 0xD6u && window->m_id <= 0xDBu &&
        window->vfptr == (struct UIWndVtbl *)
            ((uintptr_t)GetModuleHandleA(NULL) + 0x0016AD88u);
}

static int is_control_window(struct UIWnd *window)
{
    uintptr_t on_message;

    if (!window || !window->vfptr || !window->vfptr->OnMessage) return 0;
    on_message = (uintptr_t)window->vfptr->OnMessage;
#if PAL3_COMBAT_NAV_DIAGNOSTIC && defined(_WIN32)
    {
        struct combat_nav_diag_slot *slot = diag_find_slot(window->vfptr);
        if (slot && slot->window == window && slot->original &&
            slot->original->OnMessage)
            on_message = (uintptr_t)slot->original->OnMessage;
    }
#endif
    return on_message == (uintptr_t)GetModuleHandleA(NULL) +
        PAL3_COMBAT_NAV_CONTROL_WINDOW_ONMESSAGE_RVA;
}

static void collect_buttons(struct UIWnd *window, int ancestors_available,
    struct combat_button *buttons, uint32_t *count, uint32_t depth)
{
    struct PtrArray *children;
    int available;
    int i;

    if (!window || depth > PAL3_COMBAT_NAV_MAX_DEPTH ||
        *count >= PAL3_COMBAT_NAV_MAX_BUTTONS) return;
    available = ancestors_available && window->m_bcreateok &&
        window->m_bvisible && window->m_benable;
    if (!available) return;

    if (window != (struct UIWnd *)&s_ui->baseclass &&
        (is_plain_button(window) || is_ai_choice(window))) {
        struct UIButton *button = (struct UIButton *)window;
        /* 只有确认父窗体是原版主命令窗体时才套用它的按钮编号范围。 */
        if (window->m_pfather &&
            (!is_control_window(window->m_pfather) ||
                (window->m_id >= PAL3_COMBAT_NAV_MIN_BUTTON_ID &&
                 window->m_id <= PAL3_COMBAT_NAV_MAX_BUTTON_ID)) &&
            window->m_rect.right > window->m_rect.left &&
            window->m_rect.bottom > window->m_rect.top) {
            struct combat_button *candidate = &buttons[*count];
            candidate->button = button;
            candidate->rect.left = window->m_rect.left;
            candidate->rect.top = window->m_rect.top;
            candidate->rect.right = window->m_rect.right;
            candidate->rect.bottom = window->m_rect.bottom;
            ++*count;
        }
        return;
    }

    children = &window->m_childs;
    if (!children->m_pData || children->m_nSize <= 0 ||
        children->m_nSize > PAL3_COMBAT_NAV_MAX_CHILDREN) return;
    for (i = 0; i < children->m_nSize && *count < PAL3_COMBAT_NAV_MAX_BUTTONS; ++i) {
        struct UIWnd *child = (struct UIWnd *)children->m_pData[i];
        if (child && child->m_pfather == window)
            collect_buttons(child, available, buttons, count, depth + 1);
    }
}

static uint32_t collect_active_buttons(struct CCBUI *ui,
    struct combat_button *buttons)
{
    uint32_t count = 0;
    struct UIWnd *root;

    if (!ui) return 0;
    root = &ui->baseclass.baseclass;
    if (!root->m_bcreateok || !root->m_bvisible || !root->m_benable) return 0;
    s_ui = ui;
    collect_buttons(root, 1, buttons, &count, 0);
    return count;
}

static int find_button(const struct combat_button *buttons, uint32_t count,
    struct UIButton *button)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
        if (buttons[i].button == button) return (int)i;
    return -1;
}

static uint32_t initial_button(const struct combat_button *buttons, uint32_t count)
{
    uint32_t best = 0;
    uint32_t i;

    for (i = 0; i < count; ++i)
        if (buttons[i].button->baseclass.m_bfocus) return i;
    for (i = 1; i < count; ++i) {
        int center_y = (buttons[i].rect.top + buttons[i].rect.bottom) / 2;
        int best_y = (buttons[best].rect.top + buttons[best].rect.bottom) / 2;
        int center_x = (buttons[i].rect.left + buttons[i].rect.right) / 2;
        int best_x = (buttons[best].rect.left + buttons[best].rect.right) / 2;
        if (center_y < best_y || (center_y == best_y && center_x < best_x))
            best = i;
    }
    return best;
}

static uint8_t direction_state(const struct PAL3MenuNavInput *input)
{
    uint8_t dirs = 0;
    int up = (input->held & XINPUT_DPAD_UP) || input->stick_y > PAL3_COMBAT_NAV_DEADZONE;
    int down = (input->held & XINPUT_DPAD_DOWN) || input->stick_y < -PAL3_COMBAT_NAV_DEADZONE;
    int left = (input->held & XINPUT_DPAD_LEFT) || input->stick_x < -PAL3_COMBAT_NAV_DEADZONE;
    int right = (input->held & XINPUT_DPAD_RIGHT) || input->stick_x > PAL3_COMBAT_NAV_DEADZONE;

    if (up && down) up = down = 0;
    if (left && right) left = right = 0;
    if ((up || down) && (left || right)) {
        if (input->held & (XINPUT_DPAD_UP | XINPUT_DPAD_DOWN | XINPUT_DPAD_LEFT | XINPUT_DPAD_RIGHT)) {
            if (input->held & (XINPUT_DPAD_UP | XINPUT_DPAD_DOWN)) left = right = 0;
            else up = down = 0;
        } else if (abs((int)input->stick_x) > abs((int)input->stick_y)) {
            up = down = 0;
        } else {
            left = right = 0;
        }
    }
    if (up) dirs |= PAL3_MENU_NAV_UP;
    if (down) dirs |= PAL3_MENU_NAV_DOWN;
    if (left) dirs |= PAL3_MENU_NAV_LEFT;
    if (right) dirs |= PAL3_MENU_NAV_RIGHT;
    return dirs;
}

static int direction_event(uint8_t held, uint8_t bit, uint8_t index, uint32_t now)
{
    if (!(held & bit)) return 0;
    if (!(s_previous_directions & bit)) {
        s_repeat_at[index] = now + PAL3_COMBAT_NAV_INITIAL_REPEAT_MS;
        return 1;
    }
    if (time_reached(now, s_repeat_at[index])) {
        s_repeat_at[index] = now + PAL3_COMBAT_NAV_REPEAT_MS;
        return 1;
    }
    return 0;
}

static uint32_t select_combat_neighbor(const struct PAL3MenuNavRect *rects,
    uint32_t count, uint32_t from, int32_t dx, int32_t dy)
{
    uint32_t best = from, i;
    int64_t best_score = INT64_MAX;
    int64_t cx = ((int64_t)rects[from].left + rects[from].right) / 2;
    int64_t cy = ((int64_t)rects[from].top + rects[from].bottom) / 2;

    for (i = 0; i < count; ++i) {
        int64_t x, y, primary, secondary, score;
        if (i == from) continue;
        x = ((int64_t)rects[i].left + rects[i].right) / 2 - cx;
        y = ((int64_t)rects[i].top + rects[i].bottom) / 2 - cy;
        primary = dx ? x * dx : y * dy;
        if (primary <= 0) continue;
        secondary = dx ? y : x;
        /* 圆盘图标并非整齐网格，优先沿按键方向，避免从上方跳到左侧。 */
        score = primary * primary + 4 * secondary * secondary;
        if (score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

static int move_selection(int32_t dx, int32_t dy,
    const struct combat_button *buttons, uint32_t count)
{
    struct PAL3MenuNavRect rects[PAL3_COMBAT_NAV_MAX_BUTTONS];
    int current = find_button(buttons, count, s_selected);
    uint32_t next;
    uint32_t i;

    if (!count) return 0;
    if (current < 0) current = (int)initial_button(buttons, count);
    for (i = 0; i < count; ++i) rects[i] = buttons[i].rect;
    next = select_combat_neighbor(rects, count, (uint32_t)current, dx, dy);
    if (next == (uint32_t)current) return 0;
    s_selected = buttons[next].button;
    return 1;
}

static int has_neighbor(int32_t dx, int32_t dy,
    const struct combat_button *buttons, uint32_t count)
{
    struct PAL3MenuNavRect rects[PAL3_COMBAT_NAV_MAX_BUTTONS];
    int current = find_button(buttons, count, s_selected);
    uint32_t i;

    if (!count) return 0;
    if (current < 0) current = (int)initial_button(buttons, count);
    for (i = 0; i < count; ++i) rects[i] = buttons[i].rect;
    return select_combat_neighbor(rects, count, (uint32_t)current, dx, dy) !=
        (uint32_t)current;
}

static int activate_selected(struct CCBUI *ui,
    const struct combat_button *buttons, uint32_t count)
{
    int selected = find_button(buttons, count, s_selected);
    struct UIWnd *button_window;
    struct UIWnd *parent;

    if (!ui || !count) return 0;
    if (selected < 0) selected = (int)initial_button(buttons, count);
    s_selected = buttons[selected].button;
    button_window = &s_selected->baseclass;
    parent = button_window->m_pfather;
    if (!button_window->m_bcreateok || !button_window->m_bvisible ||
        !button_window->m_benable || !parent || !parent->vfptr ||
        !parent->vfptr->OnMessage) return 0;

    if (is_ai_choice(button_window)) {
        uint8_t *state = (uint8_t *)button_window;
        int checked = !state[0x44];
        /* 与原版勾选控件 Update 相同：先切换显示，再携带新值交给原版 AI 处理。 */
        state[0x44] = (uint8_t)checked;
        memcpy(state + 0x50, &checked, sizeof(checked));
        THISCALL_WRAPPER(parent->vfptr->OnMessage, parent,
            PAL3_COMBAT_NAV_BUTTON_MESSAGE, button_window->m_id, checked);
        return 1;
    }
    /* 按钮更新的原生路径把确认消息发给它自己的父窗体。 */
    THISCALL_WRAPPER(parent->vfptr->OnMessage, parent,
        PAL3_COMBAT_NAV_BUTTON_MESSAGE, s_selected->baseclass.m_id, 0);
    return 1;
}

uint32_t PAL3CombatNav_TickForUi(struct CCBUI *ui, int combat_active,
    const struct PAL3MenuNavInput *input)
{
    struct combat_button buttons[PAL3_COMBAT_NAV_MAX_BUTTONS];
    uint32_t count;
    uint32_t consumed = 0;
    uint8_t directions;
    int initialized = 0;
    int current;

    if (!input) return 0;
    if (!combat_active || !ui || ui != s_ui) {
        s_ui = combat_active ? ui : NULL;
        s_selected = NULL;
        s_previous_directions = 0;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
        if (!combat_active || !ui) return 0;
    }
    count = collect_active_buttons(ui, buttons);
    if (!count) {
        s_selected = NULL;
        s_previous_directions = 0;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
        return 0;
    }
    current = find_button(buttons, count, s_selected);
    if (current < 0) {
        s_selected = buttons[initial_button(buttons, count)].button;
        /* 圆盘隐藏或切到列表后，按按钮 ID 恢复上次位置，不保存失效控件指针。 */
        for (uint32_t i = 0; i < count; ++i)
            if (buttons[i].button->baseclass.m_id == s_last_command_id &&
                is_control_window(buttons[i].button->baseclass.m_pfather)) {
                s_selected = buttons[i].button;
                break;
            }
        current = find_button(buttons, count, s_selected);
        initialized = 1;
    }

    if (input->mouse_mode || (input->held & XINPUT_LEFT_SHOULDER)) {
        consumed = direction_state(input);
        if (input->pressed & XINPUT_B) consumed |= PAL3_MENU_NAV_CONFIRM;
        s_previous_directions = 0;
        s_mouse_mode = 1;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
        return consumed;
    }
    s_mouse_mode = 0;
    directions = direction_state(input);
    if (directions & PAL3_MENU_NAV_UP) {
        if (has_neighbor(0, -1, buttons, count)) {
            consumed |= PAL3_MENU_NAV_UP;
            if (direction_event(directions, PAL3_MENU_NAV_UP, 0, input->now_ms))
                move_selection(0, -1, buttons, count);
        }
    }
    if (directions & PAL3_MENU_NAV_DOWN) {
        if (has_neighbor(0, 1, buttons, count)) {
            consumed |= PAL3_MENU_NAV_DOWN;
            if (direction_event(directions, PAL3_MENU_NAV_DOWN, 1, input->now_ms))
                move_selection(0, 1, buttons, count);
        }
    }
    if (directions & PAL3_MENU_NAV_LEFT) {
        if (has_neighbor(-1, 0, buttons, count)) {
            consumed |= PAL3_MENU_NAV_LEFT;
            if (direction_event(directions, PAL3_MENU_NAV_LEFT, 2, input->now_ms))
                move_selection(-1, 0, buttons, count);
        }
    }
    if (directions & PAL3_MENU_NAV_RIGHT) {
        if (has_neighbor(1, 0, buttons, count)) {
            consumed |= PAL3_MENU_NAV_RIGHT;
            if (direction_event(directions, PAL3_MENU_NAV_RIGHT, 3, input->now_ms))
                move_selection(1, 0, buttons, count);
        }
    }
    s_previous_directions = directions;

    if (s_selected && is_control_window(s_selected->baseclass.m_pfather)) {
        s_last_command_id = s_selected->baseclass.m_id;
        if (input->pressed & XINPUT_BACK)
            return consumed | PAL3_MENU_NAV_REPEAT;
    }

    /* Switch 实体 A 只有在已有焦点后才确认，避免首次出现时误点。 */
    if ((input->pressed & XINPUT_B) && !initialized &&
        activate_selected(ui, buttons, count))
        consumed |= PAL3_MENU_NAV_CONFIRM;
    else if ((input->pressed & XINPUT_B) && initialized)
        consumed |= PAL3_MENU_NAV_CONFIRM;
    return consumed;
}

#ifndef PAL3_COMBAT_NAV_TEST
static struct UIListBox *active_combat_list(struct CCBUI *ui,
    struct UIWnd **owner)
{
    struct UIWnd *windows[] = {(struct UIWnd *)ui->m_pItemWindow,
        (struct UIWnd *)ui->m_pMagicWindow, (struct UIWnd *)ui->m_pSkillWindow,
        (struct UIWnd *)ui->m_pAIWindow};
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    unsigned i;
    for (i = 0; i < sizeof(windows) / sizeof(windows[0]); ++i) {
        struct UIWnd *window = windows[i];
        int j;
        if (!window || window->m_pfather != &ui->baseclass.baseclass ||
            !window->m_bvisible || !window->m_benable ||
            !window->m_childs.m_pData || window->m_childs.m_nSize <= 0 ||
            window->m_childs.m_nSize > PAL3_COMBAT_NAV_MAX_CHILDREN) continue;
        for (j = 0; j < window->m_childs.m_nSize; ++j) {
            struct UIWnd *child = window->m_childs.m_pData[j];
            if (child && child->m_pfather == window && child->m_bvisible &&
                child->m_benable && child->vfptr == (struct UIWndVtbl *)
                    (base + 0x0016AD40u)) {
                *owner = window;
                return (struct UIListBox *)child;
            }
        }
    }
    return NULL;
}

static uint32_t handle_lineup(struct CCBLineupWindow *window,
    const struct PAL3MenuNavInput *input)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    struct PAL3MenuNavRect rects[6];
    int ids[6], count = 0, current = -1, i, dx = 0, dy = 0;
    uint8_t directions = direction_state(input);
    int native_actor = (int)read_control_u32((void *)base, 0x001D5FE8u);
    if (s_lineup != window) {
        s_lineup = window;
        s_lineup_actor = -1;
        s_lineup_held = s_lineup_dropping = 0;
        s_previous_directions = 0;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
    }
    if (s_lineup_dropping && native_actor < 0) s_lineup_dropping = 0;
    if (!input->controller_active || input->mouse_mode ||
        (input->held & XINPUT_LEFT_SHOULDER)) {
        if (s_lineup_held || s_lineup_dropping) {
            /* 失焦或切鼠标时先放回原阵位，避免接管结束时留下拖动中的头像。 */
            s_lineup_slot = s_lineup_origin;
            s_lineup_held = 0;
            s_lineup_dropping = native_actor >= 0;
            s_lineup_engaged = s_lineup_dropping;
        } else s_lineup_engaged = 0;
        return directions | (s_lineup_dropping ? PAL3_MENU_NAV_CONFIRM | PAL3_MENU_NAV_CANCEL : 0);
    }
    s_lineup_engaged = 1;
    s_selected = NULL;
    if (s_lineup_dropping) return directions | PAL3_MENU_NAV_CONFIRM | PAL3_MENU_NAV_CANCEL;
    for (i = 0; i < (s_lineup_held ? 6 : 5); ++i) {
        if (s_lineup_held) {
            const int *left = (const int *)(base + 0x0017024Cu);
            const int *top = (const int *)(base + 0x00170268u);
            rects[count] = (struct PAL3MenuNavRect){left[i+1], top[i+1], left[i+1]+40, top[i+1]+38};
            ids[count] = i + 1;
            if (i + 1 == s_lineup_slot) current = count;
        } else {
            struct UIWnd *face = &window->m_pFace[i]->baseclass;
            if (!face->m_bvisible || !face->m_benable) continue;
            rects[count] = (struct PAL3MenuNavRect){face->m_rect.left, face->m_rect.top,
                face->m_rect.right, face->m_rect.bottom};
            ids[count] = i;
            if (i == s_lineup_actor) current = count;
        }
        ++count;
    }
    if (!count) return directions;
    if (current < 0) current = 0;
    if ((directions & PAL3_MENU_NAV_UP) && direction_event(directions, PAL3_MENU_NAV_UP, 0, input->now_ms)) dy = -1;
    else if ((directions & PAL3_MENU_NAV_DOWN) && direction_event(directions, PAL3_MENU_NAV_DOWN, 1, input->now_ms)) dy = 1;
    else if ((directions & PAL3_MENU_NAV_LEFT) && direction_event(directions, PAL3_MENU_NAV_LEFT, 2, input->now_ms)) dx = -1;
    else if ((directions & PAL3_MENU_NAV_RIGHT) && direction_event(directions, PAL3_MENU_NAV_RIGHT, 3, input->now_ms)) dx = 1;
    if (dx || dy) current = (int)PAL3MenuNav_SelectNeighbor(rects, count, current, dx, dy);
    s_previous_directions = directions;
    if (s_lineup_held) s_lineup_slot = ids[current];
    else s_lineup_actor = ids[current];
    if (input->pressed & XINPUT_A) {
        if (!s_lineup_held) return directions;
        s_lineup_slot = s_lineup_origin;
        s_lineup_held = 0;
        s_lineup_dropping = 1;
        return directions | PAL3_MENU_NAV_CANCEL;
    }
    if (input->pressed & XINPUT_B) {
        if (s_lineup_held) {
            s_lineup_held = 0;
            s_lineup_dropping = 1;
        } else {
            s_lineup_origin = window->m_nInitRoleFaceLineup[s_lineup_actor];
            s_lineup_slot = s_lineup_origin;
            s_lineup_held = 1;
        }
        return directions | PAL3_MENU_NAV_CONFIRM;
    }
    return directions;
}

static uint32_t handle_combat_list(struct UIListBox *list,
    struct UIWnd *window, const struct PAL3MenuNavInput *input)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uint32_t consumed = 0;
    uint8_t directions = direction_state(input);
    int left = (input->held & XINPUT_LEFT_SHOULDER) != 0;
    int category = 0, i, tab_count = 0, current = -1;
    unsigned tabs[6];
    int previous_up, previous_down, previous_confirm;
    int up = 0, down = 0, confirm = 0;
    int rows = list->m_numcol > 0 && list->m_numcol <= 8 ?
        list->m_column[0].items.m_nSize : 0;
    int first_selection = s_list != list || s_list_row < 0;

    if (s_list != list) {
        s_list = list;
        s_list_row = -1;
    }
    s_mouse_mode = left || input->mouse_mode;

    if (s_list_window != window) {
        s_list_window = window;
        s_list_left_held = left;
        s_list_left_used = left;
        s_previous_directions = 0;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
    }
    if (left && (directions || input->mouse_combo_used)) s_list_left_used = 1;
    if (s_list_left_held && !left && !s_list_left_used) category = -1;
    if (!s_list_left_held && left) s_list_left_used = input->mouse_combo_used;
    s_list_left_held = left;
    if (input->pressed & XINPUT_RIGHT_SHOULDER) category = 1;
    /* L 保留鼠标组合，单独松开才向前切分类；R 直接向后切。 */
    for (i = 0; i < window->m_childs.m_nSize && tab_count < 6; ++i) {
        struct UIWnd *child = window->m_childs.m_pData[i];
        if (child && child->m_pfather == window && child->m_bvisible &&
            child->m_benable && child->vfptr == (struct UIWndVtbl *)
                (base + 0x0016AD88u)) {
            if ((list->baseclass.m_id >= 0x90u &&
                    child->m_id == list->baseclass.m_id + 0x30u) ||
                (list->baseclass.m_id < 0x90u &&
                    child->m_id == list->baseclass.m_id + 0x35u))
                current = tab_count;
            tabs[tab_count++] = child->m_id;
        }
    }
    if (category && current >= 0 && tab_count > 1 && window->vfptr->OnMessage) {
        THISCALL_WRAPPER(window->vfptr->OnMessage, window, 0x3E9u,
            tabs[(current + category + tab_count) % tab_count], 0);
        return PAL3_MENU_NAV_CATEGORY;
    }
    if (left || input->mouse_mode) {
        s_previous_directions = 0;
        return directions | ((input->pressed & XINPUT_B) ? PAL3_MENU_NAV_CONFIRM : 0);
    }
    if ((directions & PAL3_MENU_NAV_UP) &&
        direction_event(directions, PAL3_MENU_NAV_UP, 0, input->now_ms)) up = 1;
    if ((directions & PAL3_MENU_NAV_DOWN) &&
        direction_event(directions, PAL3_MENU_NAV_DOWN, 1, input->now_ms)) down = 1;
    confirm = (input->pressed & XINPUT_B) != 0;
    consumed = directions | (confirm ? PAL3_MENU_NAV_CONFIRM : 0);
    s_previous_directions = directions;
    s_selected = NULL;
    if ((!up && !down && !confirm) || rows <= 0) return consumed;
    /* 战斗列表跟随鼠标清空悬停行，手柄单独保留当前行供原版确认与绘制。 */
    if (s_list_row < 0 || s_list_row >= rows) s_list_row = 0;
    list->m_onsel = s_list_row - list->m_firstshow;
    if (first_selection && (up || down)) { up = 1; down = 0; }
    /* 与状态页相同，交给原版列表处理选择、滚动、禁用项和确认消息。 */
    previous_up = g_input.KeyInfo[4].PressTime;
    previous_down = g_input.KeyInfo[5].PressTime;
    previous_confirm = g_input.KeyInfo[7].PressTime;
    g_input.KeyInfo[4].PressTime = up;
    g_input.KeyInfo[5].PressTime = down;
    g_input.KeyInfo[7].PressTime = confirm;
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x0003F080u,
        void, struct UIListBox *), list);
    s_list_row = list->m_firstshow + list->m_onsel;
    g_input.KeyInfo[4].PressTime = previous_up;
    g_input.KeyInfo[5].PressTime = previous_down;
    g_input.KeyInfo[7].PressTime = previous_confirm;
    return consumed;
}
#endif

uint32_t PAL3CombatNav_Tick(const struct PAL3MenuNavInput *input)
{
    struct CCBSystem *system = PAL3_m_pCBSystem;
    int active = PAL3_s_gamestate == GAME_COMBAT && system &&
        system->m_pUI && system->m_eState == CBCS_Running;
    int targeting = active && s_target_step && s_target_tick == s_input_tick;
    if (input) s_target_input = *input;
    else memset(&s_target_input, 0, sizeof(s_target_input));
    ++s_input_tick;
    if (!active) {
        s_lineup = NULL;
        s_lineup_engaged = s_lineup_held = s_lineup_dropping = 0;
        s_target_step = NULL;
        s_target_engaged = 0;
        s_target_index = -1;
    }
#if PAL3_COMBAT_NAV_DIAGNOSTIC && defined(_WIN32)
    if (active) {
        diag_arm_current_ui(system->m_pUI);
        diag_observe_current_tick(system, system->m_pUI);
    } else {
        diag_reset_snapshot();
    }
#endif
    if (targeting && input && input->controller_active &&
        !input->mouse_mode && !(input->held & XINPUT_LEFT_SHOULDER)) {
        uint32_t consumed = direction_state(input);
        if (input->pressed & XINPUT_B) consumed |= PAL3_MENU_NAV_CONFIRM;
        if (input->pressed & XINPUT_A) consumed |= PAL3_MENU_NAV_CANCEL;
        return consumed;
    }
#ifndef PAL3_COMBAT_NAV_TEST
    if (active && input) {
        struct CCBLineupWindow *lineup = system->m_pUI->m_pLineupWindow;
        if (lineup && lineup->baseclass.baseclass.m_bvisible &&
            lineup->baseclass.baseclass.m_benable)
            return handle_lineup(lineup, input);
        s_lineup = NULL;
        s_lineup_engaged = s_lineup_held = s_lineup_dropping = 0;
    }
    if (active && input && input->controller_active) {
        struct UIWnd *owner;
        struct UIListBox *list = active_combat_list(system->m_pUI, &owner);
        if (list) return handle_combat_list(list, owner, input);
    }
#endif
    s_list_window = NULL;
    s_list = NULL;
    s_list_row = -1;
    return PAL3CombatNav_TickForUi(active ? system->m_pUI : NULL,
        active, input);
}

void PAL3CombatNav_Reset(void)
{
    int pending_lineup_return = 0;
#ifndef PAL3_COMBAT_NAV_TEST
    if (lineup_override() && (s_lineup_held || s_lineup_dropping) &&
        (int)read_control_u32((void *)GetModuleHandleA(NULL), 0x001D5FE8u) >= 0) {
        /* 失焦和断连会先调用 Reset；原版还没松开头像时，须保留原位落点到下一帧。 */
        s_lineup_slot = s_lineup_origin;
        s_lineup_held = 0;
        s_lineup_dropping = s_lineup_engaged = 1;
        pending_lineup_return = 1;
    }
#endif
    if (!pending_lineup_return) {
        s_lineup = NULL;
        s_lineup_actor = s_lineup_slot = s_lineup_origin = -1;
        s_lineup_engaged = s_lineup_held = s_lineup_dropping = 0;
    }
    s_list_window = NULL;
    s_list = NULL;
    s_list_row = -1;
    s_list_left_held = s_list_left_used = 0;
    s_target_step = NULL;
    s_target_role = NULL;
    s_target_index = -1;
    s_target_engaged = 0;
    s_hover_override = 0;
    memset(&s_target_input, 0, sizeof(s_target_input));
    s_ui = NULL;
    s_selected = NULL;
    s_last_command_id = 0;
    s_previous_directions = 0;
    s_mouse_mode = 0;
    memset(s_repeat_at, 0, sizeof(s_repeat_at));
}

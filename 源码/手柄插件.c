#define PLUGIN_NAME "PAL3Gamepad"
#define PLUGIN_FRIENDLY_NAME "仙剑奇侠传3功能增强"
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "0.3.0"
#endif
#define USE_PAL3_DEFINITIONS
#include "PAL3patch.h"
#include "镜头遮挡.h"
#include "模组开关.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <limits.h>

// PAL3patch 的键盘缓冲区使用 DirectInput 扫描码。
#define DIK_ESCAPE 0x01
#define DIK_P 0x19
#define DIK_R 0x13
#define DIK_A 0x1E
#define DIK_D 0x20
#define DIK_F 0x21
#define DIK_RETURN 0x1C
#define DIK_TAB 0x0F
#define DIK_SPACE 0x39
#define DIK_UP 0xC8
#define DIK_PRIOR 0xC9
#define DIK_LEFT 0xCB
#define DIK_RIGHT 0xCD
#define DIK_NEXT 0xD1
#define DIK_DOWN 0xD0
#define DIK_LCONTROL 0x1D
#define DIK_RCONTROL 0x9D
#define DIK_LSHIFT 0x2A
#define DIK_RSHIFT 0x36
#define DIK_F1 0x3B
#define DIK_F2 0x3C
#define DIK_F3 0x3D
#define DIK_F4 0x3E
#define DIK_F5 0x3F
#define DIK_F6 0x40
#define DIK_F7 0x41
#define DIK_F8 0x42
#define DIK_F9 0x43
#define DIK_F10 0x44
#define DIK_F11 0x57
#define DIK_F12 0x58

#define XINPUT_GAMEPAD_DPAD_UP        0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN      0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT      0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT     0x0008
#define XINPUT_GAMEPAD_START          0x0010
#define XINPUT_GAMEPAD_BACK           0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB     0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB    0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER  0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A              0x1000
#define XINPUT_GAMEPAD_B              0x2000
#define XINPUT_GAMEPAD_X              0x4000
#define XINPUT_GAMEPAD_Y              0x8000
#define XINPUT_GAMEPAD_LEFT_DEADZONE  7849
#define XINPUT_GAMEPAD_RIGHT_DEADZONE 8689
#define MOUSE_SPEED_MIN 450.0f
#define MOUSE_SPEED_STEP 225.0f
#define MOUSE_SPEED_MAX 1350.0f
#define MOUSE_SPEED_NOTICE_MS 2500
#define MOUSE_SPEED_DEFAULT_PERCENT 100
#define MOUSE_SPEED_CONFIG_SECTION "PAL3Gamepad"
#define MOUSE_SPEED_CONFIG_KEY "mouse_sensitivity_percent"
#define SCENE_CAMERA_EYE_HEIGHT 75.0f
#define SCENE_SPRINT_MULTIPLIER 2.0f
#include "手柄菜单导航.h"
#include "手柄场景输入.h"
#include "手柄战斗导航.h"

#if (defined(PAL3_MENU_TREE_DIAG) && PAL3_MENU_TREE_DIAG) || \
    (defined(PAL3_COMBAT_NAV_DIAGNOSTIC) && PAL3_COMBAT_NAV_DIAGNOSTIC)
#define PAL3_GAMEPAD_NAV_DIAGNOSTIC 1
#endif

#ifndef PAL3_KEYBOARD_GAMEPAD_DIAG
#define PAL3_KEYBOARD_GAMEPAD_DIAG 0
#endif

#if PAL3_KEYBOARD_GAMEPAD_DIAG
/* 高位只用于诊断轴样本，低 16 位保持真实 XInput 按钮布局。 */
#define PAL3_DIAG_LX_NEG 0x00010000UL
#define PAL3_DIAG_LX_POS 0x00020000UL
#define PAL3_DIAG_LY_POS 0x00040000UL
#define PAL3_DIAG_LY_NEG 0x00080000UL
#define PAL3_DIAG_RX_NEG 0x00100000UL
#define PAL3_DIAG_RX_POS 0x00200000UL
#define PAL3_DIAG_RY_POS 0x00400000UL
#define PAL3_DIAG_RY_NEG 0x00800000UL
#define PAL3_DIAG_ZL 0x01000000UL
#define PAL3_DIAG_ZR 0x02000000UL
#define PAL3_DIAG_TAP 0x04000000UL
#define PAL3_DIAG_AXES 0x00ff0000UL

enum pal3_keyboard_gamepad_phase {
    PAL3_KEYBOARD_GAMEPAD_IDLE,
    PAL3_KEYBOARD_GAMEPAD_NEUTRAL_BEFORE,
    PAL3_KEYBOARD_GAMEPAD_PRESS,
    PAL3_KEYBOARD_GAMEPAD_NEUTRAL_AFTER
};

struct pal3_keyboard_gamepad_state {
    int warmup_frames;
    int phase;
    int remaining_frames;
    DWORD action;
};

static volatile LONG keyboard_gamepad_diag_enabled;
static volatile LONG keyboard_gamepad_diag_ready;
static volatile LONG keyboard_gamepad_diag_start;
static volatile LONG keyboard_gamepad_diag_busy;
static volatile LONG keyboard_gamepad_diag_pending_action;
static volatile LONG keyboard_gamepad_diag_filter_pending;
static volatile LONG keyboard_gamepad_diag_sample_active;
static struct pal3_keyboard_gamepad_state keyboard_gamepad_diag_state;
#endif

struct gamepad_state {
    DWORD packet_number;
    struct {
        WORD buttons;
        BYTE left_trigger;
        BYTE right_trigger;
        SHORT thumb_lx;
        SHORT thumb_ly;
        SHORT thumb_rx;
        SHORT thumb_ry;
    } gamepad;
};

typedef DWORD (WINAPI *xinput_get_state_fn)(DWORD user_index, struct gamepad_state *state);

static HMODULE xinput_module;
static xinput_get_state_fn xinput_get_state;
static int mouse_left_down;
static int mouse_right_down;
static int focused;
static struct gamepad_state current_pad;
static int pad_connected;
static int cursor_override_active;
static int cursor_visibility_before_override;
static int previous_start_down;
static int menu_open_pending;
static DWORD last_mouse_tick;
static float mouse_x_remainder;
static float mouse_y_remainder;
static float mouse_speed = 900.0f;
static char mouse_speed_config_path[MAX_PATH];
static int experience_rates[2] = {1, 1};

int PAL3Gamepad_GetExperienceRate(int skill)
{
    if (!PAL3Mod_Enabled(PAL3_MOD_CHEATS)) return 1;
    return experience_rates[!!skill];
}

void PAL3Gamepad_SetExperienceRate(int skill, int rate)
{
    char value[16];
    if (!PAL3Mod_Enabled(PAL3_MOD_CHEATS)) return;
    if (rate < 1 || rate > 10) return;
    experience_rates[!!skill] = rate;
    if (!mouse_speed_config_path[0]) return;
    snprintf(value, sizeof(value), "%d", rate);
    WritePrivateProfileStringA(MOUSE_SPEED_CONFIG_SECTION,
        skill ? "skill_experience_rate" : "role_experience_rate", value,
        mouse_speed_config_path);
}
static DWORD mouse_speed_notice_tick;
static int previous_speed_left;
static int previous_speed_right;
static int movie_skip_armed;
static HWND modal_owner_window;
static HWND modal_dialog_window;
static UINT_PTR modal_timer_id;
static int modal_confirm_was_down;
static int modal_cancel_was_down;
static int modal_input_armed;
static int gamepad_actions_armed;
static int previous_gamepad_confirm;
static int previous_gamepad_cancel;
static int gamepad_confirm_edge;
static int gamepad_cancel_edge;
static uint16_t menu_nav_previous_buttons;
static uint32_t menu_nav_consumed;
static uint32_t combat_nav_consumed;
static struct PAL3SceneInputOutput scene_input_output;
static int scene_input_active;
static int scene_dialog_visible;
static int scene_walk_mode;
static int previous_walk_toggle;
static int scene_jump_pending;
static DWORD scene_sprint_pressed_at;
static int scene_sprint_holding;
static int scene_sprint_active;
static unsigned char scene_jump_direction;
static float scene_camera_vertical;
static float scene_camera_horizontal;
static float scene_camera_zoom;
static int previous_camera_reset;
static int scene_camera_reset_pending;
static float scene_camera_delta;
static void *scene_camera_control;
static float scene_camera_default_distance;
static float scene_camera_last_distance;
static float scene_camera_zoom_ratio = 1.0f;
static unsigned char scene_camera_scene_id[96];
static float scene_camera_actual_distance;
static void *scene_camera_world;
static DWORD scene_camera_clear_since;
static int scene_camera_first_person;
#ifndef PAL3_GAMEPAD_TEST
static POINT scene_jump_point;
static int scene_jump_point_active;
static int scene_jump_world_active;
static struct gbVec3D scene_jump_world;
#endif
static DWORD walk_notice_tick;
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
static int diagnostic_nav_without_controller;

/* 只记录可分类的公开字段；不含逐帧变化的计时值或对话文本。 */
struct pal3_scene_state_snapshot {
    int scene[6];
    int role_dialog[6];
    int select_dialog[5];
    int head_dialogs[5][4];
    int chat_rest[4];
    int rest_dialogs[4][8];
};

static struct pal3_scene_state_snapshot previous_scene_state_snapshot;
static DWORD previous_scene_state_log_tick;
static unsigned long scene_state_snapshot_id;
static int scene_state_snapshot_initialized;

#define PAL3_UI_MESSAGE_DIAG_SLOTS 16
#define PAL3_UI_MESSAGE_DIAG_TUPLES 16
struct pal3_ui_message_diag_tuple {
    unsigned int message;
    unsigned int wparam;
    unsigned int lparam;
    int used;
};

struct pal3_ui_message_diag_slot {
    struct UIWnd *window;
    struct UIWndVtbl *original;
    struct UIWndVtbl copy;
    const char *source;
    struct pal3_ui_message_diag_tuple tuples[PAL3_UI_MESSAGE_DIAG_TUPLES];
    int tuple_cursor;
    int tuple_game_state;
    int tuple_frame_id;
    int tuple_context_initialized;
    int used;
};
static struct pal3_ui_message_diag_slot ui_message_diag_slots[PAL3_UI_MESSAGE_DIAG_SLOTS];
static int ui_message_diag_slots_full_logged;
#define PAL3_GAMELOOP_DIAG_TYPE_COUNT (GAMEEVENT_MOVIE_ATEND + 1)
static volatile LONG game_loop_diag_counts[PAL3_GAMELOOP_DIAG_TYPE_COUNT];
static volatile LONG ui_preendscene_diag_count;
static volatile LONG ui_render_diag_count;
static volatile LONG ui_diag_log_lock;
static DWORD ui_diag_last_log_tick;
static int ui_diag_last_game_state;
static int ui_diag_last_frame_id;
static int ui_diag_logged_once;
#endif

static int axis_active(SHORT value, int deadzone);
static int button_down(WORD mask);
static HWND foreground_game_window(void);

static void reset_gamepad_action_edges(void)
{
    gamepad_actions_armed = 0;
    previous_gamepad_confirm = 0;
    previous_gamepad_cancel = 0;
    gamepad_confirm_edge = 0;
    gamepad_cancel_edge = 0;
}

static void reset_menu_navigation(void)
{
    if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) PAL3MenuNav_Reset();
    PAL3CombatNav_Reset();
    menu_nav_previous_buttons = 0;
    menu_nav_consumed = 0;
    combat_nav_consumed = 0;
    PAL3SceneInput_Reset();
    memset(&scene_input_output, 0, sizeof(scene_input_output));
    scene_input_active = 0;
    scene_dialog_visible = 0;
    scene_jump_pending = 0;
    previous_walk_toggle = 0;
}

static int mouse_mode_allowed(int state, int frame)
{
    return !(state == GAME_UI && frame == FRM_S_STATE);
}

static int menu_mouse_mode_active(int armed, int allowed);

static int active_mouse_mode(int connected, int state, int frame)
{
    static int combat_mouse_engaged;
    if (!PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) return 0;
    int left_held = connected && button_down(XINPUT_GAMEPAD_LEFT_SHOULDER);
    if (state != GAME_COMBAT || !left_held) combat_mouse_engaged = 0;
    if (state == GAME_COMBAT) {
        /* 战斗 L/R 也用于切分类；实际移动或点击鼠标后才接管，松开 L 时结束。 */
        if (menu_mouse_mode_active(connected, 1)) combat_mouse_engaged = 1;
        return combat_mouse_engaged;
    }
    return connected && button_down(XINPUT_GAMEPAD_LEFT_SHOULDER) &&
        mouse_mode_allowed(state, frame);
}

static int scene_mouse_mode_active(int connected)
{
    return active_mouse_mode(connected, PAL3_s_gamestate, g_gamefrm.m_curfrmid);
}

static int menu_mouse_mode_active(int armed, int allowed)
{
    return armed && allowed && button_down(XINPUT_GAMEPAD_LEFT_SHOULDER) &&
        (axis_active(current_pad.gamepad.thumb_rx, XINPUT_GAMEPAD_RIGHT_DEADZONE) ||
         axis_active(current_pad.gamepad.thumb_ry, XINPUT_GAMEPAD_RIGHT_DEADZONE) ||
         current_pad.gamepad.right_trigger > 30 || current_pad.gamepad.left_trigger > 30);
}

static uint16_t gamepad_pressed_edges(uint16_t held, uint16_t previous,
    int armed, int left_shoulder_held)
{
    uint16_t pressed = armed ? (uint16_t)(held & ~previous) : 0;
    /* L 鼠标模式下 Switch A 由 ZR 鼠标点击处理，不确认旧焦点。 */
    if (left_shoulder_held) pressed &= (uint16_t)~XINPUT_GAMEPAD_B;
    return pressed;
}

static int should_inject_gamepad_cancel(int cancel_edge,
    uint32_t menu_consumed, uint32_t combat_consumed)
{
    return cancel_edge && !(menu_consumed & PAL3_MENU_NAV_CANCEL) &&
        !(combat_consumed & PAL3_MENU_NAV_CANCEL);
}

static int gamepad_direction_injection_allowed(int left_shoulder_held)
{
    return !left_shoulder_held;
}

static int scene_direction_fallback_allowed(int game_scene,
    int select_dialog_active)
{
    /* GAME_SCENE 也包含剧情锁定状态；只在活动树中的文字选项框放行。 */
    return !game_scene || select_dialog_active;
}

static int uiwnd_is_active_descendant(struct UIWnd *root,
    struct UIWnd *window)
{
    unsigned int depth;

    for (depth = 0; window && depth <= 16; ++depth) {
        struct UIWnd *parent;
        int i;

        if (!window->m_bcreateok || !window->m_bvisible ||
            !window->m_benable)
            return 0;
        if (window == root) return 1;

        parent = window->m_pfather;
        if (!parent || !parent->m_bcreateok || !parent->m_bvisible ||
            !parent->m_benable || !parent->m_childs.m_pData ||
            parent->m_childs.m_nSize <= 0 ||
            parent->m_childs.m_nSize > 256)
            return 0;
        for (i = 0; i < parent->m_childs.m_nSize; ++i)
            if (parent->m_childs.m_pData[i] == window) break;
        if (i == parent->m_childs.m_nSize) return 0;
        window = parent;
    }
    return 0;
}

static int scene_select_dialog_is_active(void)
{
    if (PAL3_s_gamestate != GAME_SCENE ||
        g_gamefrm.m_curfrmid != FRM_SCENE || !g_gamefrm.m_scenefrm ||
        g_gamefrm.m_curfrm != &g_gamefrm.m_scenefrm->baseclass)
        return 0;
    if (uiwnd_is_active_descendant(&g_gamefrm.baseclass_0,
            &g_gamefrm.m_seldlg.baseclass.baseclass))
        return 1;
    return uiwnd_is_active_descendant(&g_gamefrm.m_curfrm->baseclass,
        &g_gamefrm.m_seldlg.baseclass.baseclass);
}

int pal3_gamepad_should_inject_direction(uint32_t menu_consumed,
    uint32_t combat_consumed, uint32_t direction, int pressed)
{
    return pressed && !(menu_consumed & direction) &&
        !(combat_consumed & direction);
}

static void update_menu_navigation(void)
{
    struct PAL3MenuNavInput input;
    uint16_t held = current_pad.gamepad.buttons;
    int armed = pad_connected && gamepad_actions_armed;

    memset(&input, 0, sizeof(input));
    input.held = armed ? held : 0;
    input.pressed = gamepad_pressed_edges(held, menu_nav_previous_buttons,
        armed, scene_mouse_mode_active(armed));
    input.stick_x = armed ? current_pad.gamepad.thumb_lx : 0;
    input.stick_y = armed ? current_pad.gamepad.thumb_ly : 0;
    input.right_stick_y = armed ? current_pad.gamepad.thumb_ry : 0;
    input.mouse_mode = menu_mouse_mode_active(armed, mouse_mode_allowed(PAL3_s_gamestate, g_gamefrm.m_curfrmid));
    input.mouse_combo_used = menu_mouse_mode_active(armed, mouse_mode_allowed(PAL3_s_gamestate, g_gamefrm.m_curfrmid));
    input.controller_active = armed;
    input.now_ms = GetTickCount();
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    if (input.pressed & (XINPUT_GAMEPAD_RIGHT_SHOULDER |
            XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
            XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT |
            XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B))
        plog("[PAL3MenuEdge] poll armed=%d connected=%d pad=0x%04x previous=0x%04x held=0x%04x pressed=0x%04x mouse=%d",
            armed, pad_connected, held, menu_nav_previous_buttons,
            input.held, input.pressed, input.mouse_mode);
#endif
    menu_nav_consumed = PAL3Mod_Enabled(PAL3_MOD_CONTROLLER) ?
        PAL3MenuNav_Tick(&input) : PAL3MenuNav_CheatTick(&input);
    combat_nav_consumed = PAL3Mod_Enabled(PAL3_MOD_CONTROLLER) ? PAL3CombatNav_Tick(&input) : 0;
    menu_nav_previous_buttons = armed ? held : 0;
}

static void update_scene_input(void)
{
    struct PAL3SceneInputSceneState scene;
    struct PAL3SceneInput input;

    memset(&scene, 0, sizeof(scene));
    scene.game_scene = PAL3_s_gamestate == GAME_SCENE;
    scene.frame_is_scene = g_gamefrm.m_curfrmid == FRM_SCENE;
    scene.scene_frame_exists = g_gamefrm.m_scenefrm != NULL;
    scene.current_frame_is_scene_frame = g_gamefrm.m_scenefrm &&
        g_gamefrm.m_curfrm == &g_gamefrm.m_scenefrm->baseclass;
    {
        HMODULE game_module = GetModuleHandleA(NULL);
        /* 与原UISceneFrm::Update共用同一门槛，暂停场景更新时不注入操作。 */
        scene.scene_update_enabled = game_module &&
            *(volatile int *)((uintptr_t)game_module + 0x0018B230u) > 0;
    }
    if (g_gamefrm.m_scenefrm) {
        scene.scene_frame_visible = g_gamefrm.m_scenefrm->baseclass.baseclass.m_bvisible;
        scene.scene_frame_enabled = g_gamefrm.m_scenefrm->baseclass.baseclass.m_benable;
    }
    scene.role_dialog_visible = g_gamefrm.m_roledlg.baseclass.baseclass.m_bvisible;
    scene.select_dialog_visible = g_gamefrm.m_seldlg.baseclass.baseclass.m_bvisible;
    {
        int i;
        for (i = 0; i < 5; ++i)
            if (g_gamefrm.m_headdlg[i].baseclass.baseclass.m_bvisible)
                scene.head_dialog_visible = 1;
    }
    scene_dialog_visible = scene.game_scene && (scene.role_dialog_visible ||
        scene.select_dialog_visible || scene.head_dialog_visible);

    memset(&input, 0, sizeof(input));
    input.free_exploration = PAL3SceneInput_IsFreeExploration(&scene);
    input.focused = focused;
    input.connected = pad_connected && gamepad_actions_armed;
    input.mouse_mode = scene_mouse_mode_active(input.connected);
    if (input.connected && !button_down(XINPUT_GAMEPAD_LEFT_SHOULDER)) {
        if ((current_pad.gamepad.buttons & XINPUT_GAMEPAD_DPAD_UP) ||
            current_pad.gamepad.thumb_ly > XINPUT_GAMEPAD_LEFT_DEADZONE)
            input.directions |= PAL3_SCENE_INPUT_UP;
        if ((current_pad.gamepad.buttons & XINPUT_GAMEPAD_DPAD_DOWN) ||
            current_pad.gamepad.thumb_ly < -XINPUT_GAMEPAD_LEFT_DEADZONE)
            input.directions |= PAL3_SCENE_INPUT_DOWN;
        if ((current_pad.gamepad.buttons & XINPUT_GAMEPAD_DPAD_LEFT) ||
            current_pad.gamepad.thumb_lx < -XINPUT_GAMEPAD_LEFT_DEADZONE)
            input.directions |= PAL3_SCENE_INPUT_LEFT;
        if ((current_pad.gamepad.buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ||
            current_pad.gamepad.thumb_lx > XINPUT_GAMEPAD_LEFT_DEADZONE)
            input.directions |= PAL3_SCENE_INPUT_RIGHT;
        input.right_x = (float)current_pad.gamepad.thumb_rx / 32767.0f;
        input.right_y = (float)current_pad.gamepad.thumb_ry / 32767.0f;
    }
    input.now_ms = GetTickCount();
    scene_camera_vertical = input.free_exploration && input.focused &&
        input.connected && !input.mouse_mode && !button_down(XINPUT_GAMEPAD_RIGHT_SHOULDER) ? input.right_y : 0.0f;
    scene_camera_horizontal = input.free_exploration && input.focused &&
        input.connected && !input.mouse_mode ? input.right_x : 0.0f;
    scene_camera_zoom = input.free_exploration && input.focused &&
        input.connected && !input.mouse_mode && button_down(XINPUT_GAMEPAD_RIGHT_SHOULDER) ? input.right_y : 0.0f;
    if (!PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) input.directions = 0;
    if (input.free_exploration && input.focused && input.connected &&
        !input.mouse_mode && input.directions)
        scene_jump_direction = input.directions;
    PAL3SceneInput_Tick(&input, &scene_input_output);
    /* 镜头两轴统一按摇杆幅度转动，不再叠加原先每帧固定角度的 A/D 模拟。 */
    scene_input_output.camera_keys = 0;
    {
        int reset = button_down(XINPUT_GAMEPAD_RIGHT_THUMB);
        scene_camera_reset_pending = input.free_exploration && input.focused &&
            input.connected && !input.mouse_mode && reset && !previous_camera_reset;
        previous_camera_reset = reset;
    }
    {
        int toggle = button_down(XINPUT_GAMEPAD_LEFT_THUMB);
        if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER) && input.free_exploration && input.focused && input.connected &&
            !input.mouse_mode && gamepad_actions_armed && toggle && !previous_walk_toggle) {
            scene_walk_mode = !scene_walk_mode;
            walk_notice_tick = GetTickCount();
            if (mouse_speed_config_path[0])
                WritePrivateProfileStringA(MOUSE_SPEED_CONFIG_SECTION, "default_walk",
                    scene_walk_mode ? "1" : "0", mouse_speed_config_path);
        }
        previous_walk_toggle = toggle;
    }
    if (!PAL3Mod_Enabled(PAL3_MOD_CAMERA)) {
        scene_camera_vertical = scene_camera_horizontal = scene_camera_zoom = 0;
        scene_camera_reset_pending = 0;
    }
    if (!PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) {
        scene_input_output.directions = 0;
        scene_jump_pending = 0;
    }
    if (menu_open_pending) {
        /* Plus 只对当前可自由操作的有效输入生效；对话或失焦时直接丢弃。 */
        if (input.free_exploration && input.focused && input.connected &&
            g_gamefrm.m_scenefrm) {
#if !defined(PAL3_GAMEPAD_TEST)
            PAL3SceneInput_OpenOriginalMenu(&g_gamefrm.m_scenefrm->m_face);
#endif
        }
        menu_open_pending = 0;
    }
    scene_input_active = input.free_exploration && input.focused && input.connected &&
        !input.mouse_mode;
    {
        int held = scene_input_active && button_down(XINPUT_GAMEPAD_A);
        if (held && !scene_sprint_holding) scene_sprint_pressed_at = input.now_ms;
        scene_sprint_holding = held;
        if (!held) scene_sprint_active = 0;
    }
}

#ifndef PAL3_GAMEPAD_TEST
static MAKE_THISCALL(void, combat_experience_count, void *state, int prepare, int thread)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    int before[5], i;
    for (i = 0; i < 5; ++i)
        memcpy(&before[i], (unsigned char *)state + 0x2C4u + i * 0x37Cu, 4);
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0xFA930u,
        void, void *, int, int), state, prepare, thread);
    /* 待结算经验先放大，原版随后统一处理等级、能力、解锁和结果页。 */
    if (prepare && PAL3Mod_Enabled(PAL3_MOD_CHEATS)) for (i = 0; i < 5; ++i) {
        int *pending = (int *)((unsigned char *)state + 0x2C4u + i * 0x37Cu);
        if (*pending > before[i]) {
            int64_t value = before[i] + (int64_t)(*pending - before[i]) * experience_rates[0];
            *pending = value > INT_MAX ? INT_MAX : (int)value;
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
            plog("[PAL3Experience] role=%d pending=%d rate=%d scaled=%d", i,
                before[i], experience_rates[0], *pending);
#endif
        }
    }
}

static MAKE_THISCALL(unsigned char, combat_skill_use, void *state, void *command)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    unsigned char before[5][48], result;
    int i, j;
    for (i = 0; i < 5; ++i)
        memcpy(before[i], (unsigned char *)state + 0x3D8u + i * 0x37Cu, 48);
    result = THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0xF1070u,
        unsigned char, void *, void *), state, command);
    /* 仙术熟练度是单字节使用次数；只放大本次真正增加的次数，保留 255 上限。 */
    for (i = 0; i < 5; ++i) for (j = 0; j < 48; ++j) {
        unsigned char *count = (unsigned char *)state + 0x3D8u + i * 0x37Cu + j;
        if (PAL3Mod_Enabled(PAL3_MOD_CHEATS) && *count > before[i][j]) {
            unsigned value = before[i][j] + (*count - before[i][j]) * experience_rates[1];
            *count = value > 255 ? 255 : (unsigned char)value;
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
            plog("[PAL3Experience] skill role=%d index=%d before=%u rate=%d after=%u",
                i, j, before[i][j], experience_rates[1], *count);
#endif
        }
    }
    return result;
}

static MAKE_THISCALL(void, scene_camera_preset, void *control, int preset)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x0001D100u,
        void, void *, int), control, preset);
    /* 原版预设区分室内外；存档里的当前距离不能拿来当 R3 的默认距离。 */
    if (preset >= 0 && preset <= 3) {
        scene_camera_control = control;
        scene_camera_default_distance = *(float *)((unsigned char *)control + 0x1D4u);
        scene_camera_last_distance = 0;
    }
}

static MAKE_THISCALL(void, scene_camera_apply, void *control, int flags)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    unsigned char *camera = control;
    void *role = *(void **)(base + 0x008116ECu);
    void *world = NULL;
    struct gbVec3D target;
    /* 原版控制器把 gbCamera 内嵌在 +4，前四字节是 fov，不是相机指针。 */
    struct gbCamera *game_camera = control ? (struct gbCamera *)(camera + 4) : NULL;
    float previous_near = 0, previous_far = 0, render_distance = 0;
    int scene_view = PAL3_s_gamestate == GAME_SCENE ||
        (PAL3_s_gamestate == GAME_UI && g_gamefrm.m_curfrmid == FRM_S_STATE);
    int first_person_view = scene_view && scene_camera_zoom_ratio <= 0.0251f;
    int first_person_dialog = scene_dialog_visible && first_person_view;
    scene_camera_first_person = 0;
    /* 远景范围、跟随高度和遮挡显示不随输入锁关闭，否则拉远后对话或开菜单会大片变黑。 */
    int adjust = scene_view && control && role &&
        *(void **)(camera + 0x17Cu) == role && *(int *)(camera + 0x1C0u);
    if (adjust) world = ((void *(__cdecl *)(void))(base + 0x000226F0u))();
    if (adjust && world) {
        struct gbVec3D direction;
        float wanted = *(float *)(camera + 0x1D4u);
        float pitch = *(float *)(camera + 0x1C4u), yaw = *(float *)(camera + 0x1C8u);
        memcpy(&target, camera + 0x1E0u, sizeof(target));
        /* 方向只由角度确定，不能从已经抬高的镜头位置反推，避免近景支点影响俯仰。 */
        direction = (struct gbVec3D){-cosf(pitch)*sinf(yaw), -sinf(pitch), cosf(pitch)*cosf(yaw)};
        if (wanted > 1.0f) {
            struct gbVec3D pivot = target, actual_eye, actual_target, role_position;
            float near_blend = (0.10f - scene_camera_zoom_ratio) / 0.075f;
            if (near_blend < 0) near_blend = 0;
            if (near_blend > 1) near_blend = 1;
            near_blend = near_blend * near_blend * (3.0f - 2.0f * near_blend);
            memcpy(&role_position, (unsigned char *)role + 0xA0u, sizeof(role_position));
            pivot.x += (role_position.x - pivot.x) * near_blend;
            pivot.y += (role_position.y - pivot.y) * near_blend;
            pivot.z += (role_position.z - pivot.z) * near_blend;
            /* 抬到人物身体处再看过去，低角度时不把脚边地面当作遮挡。 */
            {
                float head_blend = (0.15f - scene_camera_zoom_ratio) / 0.125f;
                if (head_blend < 0) head_blend = 0;
                if (head_blend > 1) head_blend = 1;
                head_blend = head_blend * head_blend * (3.0f - 2.0f * head_blend);
                pivot.y += 48.0f + (SCENE_CAMERA_EYE_HEIGHT - 48.0f) * head_blend;
            }
            if (scene_camera_zoom_ratio <= 0.0251f) {
                /* 只在玩家手动拉到最近端时进入第一人称，山墙自动收近不会意外切换模式。 */
                memcpy(&pivot, (unsigned char *)role + 0xA0u, sizeof(pivot));
                pivot.y += SCENE_CAMERA_EYE_HEIGHT;
                actual_eye = pivot;
                pivot.x -= direction.x * 256.0f;
                pivot.y -= direction.y * 256.0f;
                pivot.z -= direction.z * 256.0f;
                memcpy(camera + 0x1E0u, &pivot, sizeof(pivot));
                memcpy(camera + 0x1ECu, &actual_eye, sizeof(actual_eye));
                scene_camera_first_person = 1;
                scene_camera_actual_distance = 0;
                goto camera_ready;
            }
            /* 恢复原版场景绘制，第三人称保留玩家选择的距离，不自动隐去整组模型。 */
            scene_camera_world = world;
            render_distance = wanted * (1.0f - near_blend);
            scene_camera_actual_distance = render_distance;
            {
                /* 接近水平后改为从当前视点向上看，不继续绕脚底把镜头沉入地面。 */
                float orbit_pitch = pitch > -0.12f ? -0.12f : pitch;
                struct gbVec3D orbit = {-cosf(orbit_pitch)*sinf(yaw), -sinf(orbit_pitch),
                    cosf(orbit_pitch)*cosf(yaw)};
                float look_distance = render_distance > 256.0f ? render_distance : 256.0f;
                actual_eye = (struct gbVec3D){pivot.x + orbit.x * render_distance,
                    pivot.y + orbit.y * render_distance, pivot.z + orbit.z * render_distance};
                actual_target = (struct gbVec3D){actual_eye.x - direction.x * look_distance,
                    actual_eye.y - direction.y * look_distance, actual_eye.z - direction.z * look_distance};
            }
            memcpy(camera + 0x1E0u, &actual_target, sizeof(actual_target));
            memcpy(camera + 0x1ECu, &actual_eye, sizeof(actual_eye));
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
            {
                static DWORD next_camera_notice;
                if ((int32_t)(GetTickCount() - next_camera_notice) >= 0) {
                    next_camera_notice = GetTickCount() + 500;
                    plog("[PAL3Camera] pitch=%.3f yaw=%.3f wanted=%.1f actual=%.1f",
                        *(float *)(camera+0x1C4u), *(float *)(camera+0x1C8u),
                        wanted, scene_camera_actual_distance);
                }
            }
#endif
        } else adjust = 0;
    } else adjust = 0;
camera_ready:
    if (adjust && game_camera) {
        previous_near = game_camera->zNear;
        previous_far = game_camera->zFar;
        if (scene_camera_first_person || render_distance < 64.0f) game_camera->zNear = 2.0f;
        if (game_camera->zFar < *(float *)(camera + 0x1D4u) * 2.0f)
            game_camera->zFar = *(float *)(camera + 0x1D4u) * 2.0f;
    }
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x0001C630u,
        void, void *, int), control, flags);
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    {
        static int previous_dialog;
        if (first_person_dialog != previous_dialog && game_camera && role) {
            plog("[PAL3FirstPersonDialog] visible=%d enhanced=%d eye_y=%.3f role_y=%.3f distance=%.1f",
                first_person_dialog, scene_camera_first_person, game_camera->eyeLoc.y,
                *(float *)((unsigned char *)role + 0xA4u), *(float *)(camera + 0x1D4u));
            previous_dialog = first_person_dialog;
        }
    }
#endif
    /* 只抬高实际绘制的注视点，不把高度反复加到原版跟随计算中。 */
    if (adjust) {
        memcpy(camera + 0x1E0u, &target, sizeof(target));
        if (game_camera) { game_camera->zNear = previous_near; game_camera->zFar = previous_far; }
    }
    else { scene_camera_actual_distance = 0; scene_camera_world = NULL; }
}

static MAKE_THISCALL(void, scene_party_render, void *role, void *graphics, int flags)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    /* 只跳过第一人称当前角色这一帧的模型绘制，角色、碰撞和剧情可见状态不变。 */
    if ((scene_camera_first_person || (scene_camera_zoom_ratio < 0.15f && scene_input_active)) &&
        role == *(void **)(base + 0x008116ECu)) return;
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x00012400u,
        void, void *, void *, int), role, graphics, flags);
}

static MAKE_THISCALL(int, scene_npc_direction_check, void *role, struct gbVec3D *point)
{
    struct gbVec3D previous, direction, origin;
    float length;
    int result;
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    int original_result = 0;
    static DWORD next_notice;
#endif
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (!scene_input_active || !role || !point)
        return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x00019EF0u,
            int, void *, struct gbVec3D *), role, point);
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    original_result = THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x00019EF0u,
        int, void *, struct gbVec3D *), role, point);
#endif
    memcpy(&origin, (unsigned char *)role + 4, sizeof(origin));
    direction = (struct gbVec3D){point->x - origin.x, point->y - origin.y, point->z - origin.z};
    length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    memcpy(&previous, (unsigned char *)role + 0x14u, sizeof(previous));
    if (length > 0.001f) {
        direction.x /= length; direction.y /= length; direction.z /= length;
        memcpy((unsigned char *)role + 0x14u, &direction, sizeof(direction));
    }
    /* 只在原版候选检查时朝向该 NPC；距离、障碍、剧情条件仍由原版判断。 */
    result = THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x00019EF0u,
        int, void *, struct gbVec3D *), role, point);
    memcpy((unsigned char *)role + 0x14u, &previous, sizeof(previous));
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    if (result != original_result && (int32_t)(GetTickCount() - next_notice) >= 0) {
        next_notice = GetTickCount() + 500;
        plog("[PAL3NpcDirection] original=%d nearby=%d distance=%.2f restored=%d",
            original_result, result, length,
            memcmp((unsigned char *)role + 0x14u, &previous, sizeof(previous)) == 0);
    }
#endif
    return result;
}

static void *__cdecl scene_find_nearby_npc(void)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    void *original = ((void *(__cdecl *)(void))(base + 0x000230D0u))();
    unsigned char *world, *role;
    void *nearest = NULL;
    float nearest_distance = 180.0f;
    struct gbVec3D origin;
    int count, i;
    if (original || !scene_input_active) return original;
    role = *(void **)(base + 0x008116ECu);
    world = ((void *(__cdecl *)(void))(base + 0x000226F0u))();
    if (!role || !world) return NULL;
    PAL3CameraGeometry_Select((const char *)world, (const char *)world + 64);
    if (!PAL3CameraGeometry_Ready()) return NULL;
    count = *(int *)(world + 0xE0E0u);
    if (count < 0 || count > 50) return NULL;
    memcpy(&origin, role + 0xA0u, sizeof(origin));
    origin.y += 80.0f;
    for (i = 0; i < count; ++i) {
        unsigned char *npc = world + 0xE0E4u + i * 0x2200u;
        struct gbVec3D point, direction;
        float distance;
        /* +0x278 是剧情可见状态；+0x90 是模型绘制结果，第一人称背向 NPC 时不能据此拒绝交谈。 */
        if (!*(int *)(npc+0x278u) ||
            *(int *)(npc+0x274u) != *(int *)(role+0x274u) ||
            *(int *)(npc+0x4F0u) == 7 || npc[0xADu] == 2 || !*(int *)(npc+0x108u)) continue;
        memcpy(&point, npc + 0xA0u, sizeof(point));
        point.y += 80.0f;
        direction = (struct gbVec3D){point.x-origin.x, point.y-origin.y, point.z-origin.z};
        distance = sqrtf(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
        if (distance < 0.001f || distance > nearest_distance) continue;
        direction.x /= distance; direction.y /= distance; direction.z /= distance;
        /* 行走不能跨柜台，但说话可以；人物头部之间有山墙时仍不触发。 */
        if (PAL3CameraGeometry_HitDistance(&origin, &direction, distance) < distance - 1.0f) continue;
        nearest = npc;
        nearest_distance = distance;
    }
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    if (nearest)
        plog("[PAL3NpcNearby] matched=%p distance=%.1f range=180", nearest, nearest_distance);
#endif
    return nearest;
}

static BOOL WINAPI scene_read_cursor(LPPOINT point)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    BOOL result = ((BOOL (WINAPI *)(LPPOINT))(base + 0x000022E0u))(point);
    /* 只替换这一帧原版场景的取点，不移动系统鼠标，也不影响菜单和 L 鼠标。 */
    if (scene_jump_point_active) {
        *point = scene_jump_point;
        return TRUE;
    }
    return result;
}

static int __cdecl scene_pick_jump_ground(POINT *point, struct gbVec3D *world, int *layer)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    void *role = *(void **)(base + 0x008116ECu);
    /* 仅替换第一人称 B 的地面取点，忙碌状态、可跳格和落点限制仍走原版流程。 */
    if (scene_jump_world_active && scene_jump_point_active && role) {
        *world = scene_jump_world;
        *layer = *(int *)((unsigned char *)role + 0x274u);
        return 1;
    }
    return ((int (__cdecl *)(POINT *, struct gbVec3D *, int *))
        (base + 0x0001E7D0u))(point, world, layer);
}

static int scene_prepare_jump_point(uintptr_t base)
{
    void *role = *(void **)(base + 0x008116ECu);
    void *control = *(void **)(base + 0x007FDA68u);
    struct gbVec3D world;
    struct gbCamera *camera;
    float previous_far, distance;
    float x = 100000.0f, y = 100000.0f;
    int dx = ((scene_jump_direction & PAL3_SCENE_INPUT_RIGHT) != 0) -
        ((scene_jump_direction & PAL3_SCENE_INPUT_LEFT) != 0);
    int dy = ((scene_jump_direction & PAL3_SCENE_INPUT_DOWN) != 0) -
        ((scene_jump_direction & PAL3_SCENE_INPUT_UP) != 0);
    float reach = PAL3_s_drvinfo.height * 0.4f;
    if (!role || !control || (!dx && !dy)) return 0;
    camera = (struct gbCamera *)((unsigned char *)control + 4);
    distance = *(float *)((unsigned char *)control + 0x1D4u);
    if (distance == 2.0f) {
        float yaw = *(float *)((unsigned char *)control + 0x1C8u);
        float length = sqrtf((float)(dx * dx + dy * dy));
        float reach_world = *(float *)(base + 0x00810D48u) / length;
        /* 第一人称脚底位于镜头下方，不能投影成鼠标点；方向只随水平朝向，不受抬头低头影响。 */
        memcpy(&scene_jump_world, (unsigned char *)role + 0xA0u, sizeof(scene_jump_world));
        scene_jump_world.x += (cosf(yaw) * dx - sinf(yaw) * dy) * reach_world;
        scene_jump_world.z += (sinf(yaw) * dx + cosf(yaw) * dy) * reach_world;
        scene_jump_world_active = 1;
        scene_jump_point.x = PAL3_s_drvinfo.width / 2;
        scene_jump_point.y = PAL3_s_drvinfo.height / 2;
        return 1;
    }
    /* 拉远后同样的屏幕位移会落到更远的地面；按镜头距离缩小取点，保留原版跳跃范围。 */
    if (scene_camera_default_distance > 0.0f &&
        distance > scene_camera_default_distance)
        reach *= scene_camera_default_distance / distance;
    memcpy(&world, (unsigned char *)role + 0xA0u, sizeof(world));
    /* 原版投影会拒绝 6000 之外的点；取点与远景绘制使用同一范围，调用后立即还原。 */
    previous_far = camera->zFar;
    if (camera->zFar < distance * 2.0f) camera->zFar = distance * 2.0f;
    /* 与原版跳跃提示共用世界坐标投影入口，保持当前镜头和宽屏修正。 */
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        *(uintptr_t *)(base + 0x0016A554u), void, void *,
        struct gbVec3D *, float *, float *),
        (unsigned char *)control + 4, &world, &x, &y);
    camera->zFar = previous_far;
    if (!(x > -1.0f && x < 1.0f && y > -1.0f && y < 1.0f)) return 0;
    scene_jump_point.x = (LONG)((x + 1.0f) * PAL3_s_drvinfo.width * 0.5f + dx * reach);
    scene_jump_point.y = (LONG)((1.0f - y) * PAL3_s_drvinfo.height * 0.5f + dy * reach);
    if (scene_jump_point.x < 1) scene_jump_point.x = 1;
    if (scene_jump_point.y < 1) scene_jump_point.y = 1;
    if (scene_jump_point.x >= PAL3_s_drvinfo.width)
        scene_jump_point.x = PAL3_s_drvinfo.width - 1;
    if (scene_jump_point.y >= PAL3_s_drvinfo.height)
        scene_jump_point.y = PAL3_s_drvinfo.height - 1;
    return 1;
}

static void __cdecl scene_control_input(float delta)
{
    long previous_wheel = g_input.MouseState.lZ;
    struct GRP_KEYREG previous_right = g_input.KeyInfo[1];
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    scene_camera_delta = delta > 0.0f && delta < 0.25f ? delta : 0.0f;
    scene_sprint_active = 0;
    scene_jump_world_active = 0;
    if (scene_input_active) {
        void *control = *(void **)(base + 0x007FDA68u);
        void *role = *(void **)(base + 0x008116ECu);
        int can_jump = role && ((unsigned char (__cdecl *)(void))(base + 0x0001EE60u))();
        /* B 短按仍请求跳跃；只有当前导航格不可跳，长按才变成加速。 */
        scene_sprint_active = PAL3Mod_Enabled(PAL3_MOD_CONTROLLER) && role && !can_jump && scene_sprint_holding &&
            GetTickCount() - scene_sprint_pressed_at >= 320u;
        /* 剧情把镜头交给其他目标时，仍由原版控制。 */
        if (PAL3Mod_Enabled(PAL3_MOD_CAMERA) && control && role && *(void **)((unsigned char *)control + 0x17Cu) == role &&
            *(int *)((unsigned char *)control + 0x1C0u) && scene_camera_delta) {
                void *world = ((void *(__cdecl *)(void))(base + 0x000226F0u))();
                float *pitch = (float *)((unsigned char *)control + 0x1C4u);
                float *yaw = (float *)((unsigned char *)control + 0x1C8u);
                float *distance = (float *)((unsigned char *)control + 0x1D4u);
                float x = scene_camera_horizontal, y = scene_camera_vertical;
                float max_ratio = 2.0f;
                if (scene_camera_control != control || !scene_camera_default_distance) {
                    scene_camera_control = control;
                    scene_camera_default_distance = *distance;
                    scene_camera_last_distance = 0;
                }
                if (*distance != scene_camera_last_distance ||
                    (world && memcmp(scene_camera_scene_id, world, sizeof(scene_camera_scene_id)))) {
                    /* 读档或原版剧情改过距离时，从当前值恢复远近；2 是本补丁的第一人称端点。 */
                    scene_camera_zoom_ratio = *distance == 2.0f ? 0.025f :
                        *distance / scene_camera_default_distance;
                    if (world) memcpy(scene_camera_scene_id, world, sizeof(scene_camera_scene_id));
                    scene_camera_actual_distance = 0;
                    scene_camera_world = NULL;
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
                    plog("[PAL3CameraRestore] default=%.1f distance=%.1f ratio=%.4f",
                        scene_camera_default_distance, *distance, scene_camera_zoom_ratio);
#endif
                }
                if (scene_camera_reset_pending) {
                    struct gbVec3D forward;
                    memcpy(&forward, (unsigned char *)role + 0x14u, sizeof(forward));
                    /* 原版前向为 (sin(yaw), sin(pitch), -cos(yaw))。 */
                    if (forward.x * forward.x + forward.z * forward.z > 0.001f)
                        *yaw = atan2f(forward.x, -forward.z);
                    *pitch = -0.53f;
                    *(float *)((unsigned char *)control + 0x1CCu) = 0.0f;
                    scene_camera_zoom_ratio = 1.0f;
                } else {
                    /* 死区之外从零速度起步，水平、纵向及缩放使用同一连续输入曲线。 */
                    x = fabsf(x) <= 0.24f ? 0.0f : copysignf((fminf(fabsf(x), 1.0f) - 0.24f) / 0.76f, x);
                    y = fabsf(y) <= 0.24f ? 0.0f : copysignf((fminf(fabsf(y), 1.0f) - 0.24f) / 0.76f, y);
                    *yaw += x * scene_camera_delta * 1.8f;
                    *pitch += y * scene_camera_delta * 1.8f;
                    if (fabsf(scene_camera_zoom) > 0.24f) {
                        float zoom = copysignf((fminf(fabsf(scene_camera_zoom), 1.0f) - 0.24f) / 0.76f, scene_camera_zoom);
                        scene_camera_zoom_ratio *= expf(-zoom * scene_camera_delta * 1.5f);
                    }
                }
                if (world) {
                    struct gbCamera *view = (struct gbCamera *)((unsigned char *)control + 4);
                    struct gbVec3D position;
                    float overview;
                    memcpy(&position, (unsigned char *)role + 0xA0u, sizeof(position));
                    PAL3CameraGeometry_Select((const char *)world, (const char *)world + 64);
                    overview = view && view->Hscreen > 0 ? PAL3CameraGeometry_MapDistance(&position,
                        view->fov, view->Wscreen / view->Hscreen) : 0;
                    if (overview > scene_camera_default_distance)
                        max_ratio = overview / scene_camera_default_distance;
                }
                if (scene_camera_zoom_ratio < 0.025f) scene_camera_zoom_ratio = 0.025f;
                if (scene_camera_zoom_ratio > max_ratio) scene_camera_zoom_ratio = max_ratio;
                if (*pitch < -1.22f) *pitch = -1.22f;
                {
                    float head_blend = (0.15f - scene_camera_zoom_ratio) / 0.125f;
                    float upper;
                    if (head_blend < 0) head_blend = 0;
                    if (head_blend > 1) head_blend = 1;
                    upper = 0.65f + 0.45f * head_blend;
                    if (*pitch > upper) *pitch = upper;
                }
                *distance = scene_camera_zoom_ratio <= 0.0251f ? 2.0f :
                    scene_camera_default_distance * scene_camera_zoom_ratio;
                scene_camera_last_distance = *distance;
            }
        if (scene_jump_pending && can_jump &&
            scene_prepare_jump_point(base)) {
            /* B 的落点由当前或最近一次左摇杆方向确定，不能沿用隐藏鼠标旧位置。 */
            scene_jump_point_active = 1;
            g_input.KeyInfo[1].Data = 1;
            g_input.KeyInfo[1].PressTime = 1;
        }
    }
    scene_jump_pending = 0;
    scene_camera_reset_pending = 0;
    scene_input_output.wheel_delta = 0;
    ((void (__cdecl *)(float))(base + 0x0001D9A0u))(delta);
    scene_jump_point_active = 0;
    scene_jump_world_active = 0;
    g_input.MouseState.lZ = previous_wheel;
    g_input.KeyInfo[1] = previous_right;
}

static MAKE_THISCALL(void, scene_move, void *role, struct gbVec3D *direction, int mode)
{
    int sprint = scene_input_active && scene_input_output.directions && scene_sprint_active &&
        role == *(void **)((uintptr_t)GetModuleHandleA(NULL) + 0x008116ECu);
    /* 原版已检查可操作、导航与角色状态后才到此处；仅改变手柄此次移动的走跑参数。 */
    if (scene_input_active && scene_input_output.directions)
        mode = sprint ? 2 : (scene_walk_mode ? 1 : 2);
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) + 0x00014980u,
        void, void *, struct gbVec3D *, int), role, direction, mode);
    if (sprint) {
        struct gbVec3D *velocity = (struct gbVec3D *)((unsigned char *)role + 0x51Cu);
        /* 原版已把方向归一化并乘跑速；只放大此次速度，不改全局时间和导航碰撞。 */
        velocity->x *= SCENE_SPRINT_MULTIPLIER;
        velocity->z *= SCENE_SPRINT_MULTIPLIER;
    }
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    if (scene_input_active && scene_input_output.directions) {
        static DWORD next_sprint_notice;
        if ((int32_t)(GetTickCount() - next_sprint_notice) >= 0) {
            struct gbVec3D *velocity = (struct gbVec3D *)((unsigned char *)role + 0x51Cu);
            next_sprint_notice = GetTickCount() + 200;
            plog("[PAL3Sprint] active=%d factor=%.2f velocity=(%.1f,%.1f) mode=%d",
                sprint, sprint ? SCENE_SPRINT_MULTIPLIER : 1.0f, velocity->x, velocity->z, mode);
        }
    }
#endif
}
#endif

static HWND foreground_game_window(void)
{
    HWND foreground = GetForegroundWindow();
    DWORD process_id = 0;
    if (!foreground) return NULL;
    GetWindowThreadProcessId(foreground, &process_id);
    return process_id == GetCurrentProcessId() ? foreground : NULL;
}

static int window_is_or_child_of(HWND window, HWND parent)
{
    while (window) {
        if (window == parent) return 1;
        window = GetParent(window);
    }
    return 0;
}

static int game_has_focus(void)
{
    return foreground_game_window() != NULL;
}

static int movie_skip_should_post(int is_movie, int focused_now, int connected, int start_down)
{
    if (!is_movie || !focused_now || !connected) {
        movie_skip_armed = 0;
        return 0;
    }

    if (!start_down) {
        movie_skip_armed = 1;
        return 0;
    }

    if (!movie_skip_armed) return 0;
    movie_skip_armed = 0;
    return 1;
}

static int mouse_button_wanted(int is_focused, int connected, int cursor_inside, BYTE trigger)
{
    return is_focused && connected && cursor_inside && trigger > 30;
}

static int mouse_button_needs_event(int held, int wanted)
{
    return held != wanted;
}

static int mouse_button_result_state(int held, int wanted, int send_succeeded)
{
    return mouse_button_needs_event(held, wanted) && send_succeeded ? wanted : held;
}

static int game_client_screen_rect(HWND window, RECT *rect)
{
    POINT top_left;
    POINT bottom_right;
    if (!window || !GetClientRect(window, rect)) return 0;
    top_left.x = rect->left;
    top_left.y = rect->top;
    bottom_right.x = rect->right - 1;
    bottom_right.y = rect->bottom - 1;
    if (!ClientToScreen(window, &top_left) || !ClientToScreen(window, &bottom_right)) return 0;
    rect->left = top_left.x;
    rect->top = top_left.y;
    rect->right = bottom_right.x;
    rect->bottom = bottom_right.y;
    return rect->right >= rect->left && rect->bottom >= rect->top;
}

static int cursor_inside_game_client(HWND window)
{
    POINT cursor;
    RECT client;
    return GetCursorPos(&cursor) && game_client_screen_rect(window, &client) &&
        cursor.x >= client.left && cursor.x <= client.right &&
        cursor.y >= client.top && cursor.y <= client.bottom;
}

static void set_mouse_button(DWORD flag, int *held, int should_hold)
{
    INPUT input;
    if (!mouse_button_needs_event(*held, should_hold)) return;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = should_hold ? flag : (flag == MOUSEEVENTF_LEFTDOWN ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP);
    *held = mouse_button_result_state(*held, should_hold, SendInput(1, &input, sizeof(input)) == 1);
}

static void release_mouse_buttons(void)
{
    set_mouse_button(MOUSEEVENTF_LEFTDOWN, &mouse_left_down, 0);
    set_mouse_button(MOUSEEVENTF_RIGHTDOWN, &mouse_right_down, 0);
}

static int belongs_to_current_process(HWND window)
{
    DWORD process_id = 0;
    return window && IsWindow(window) &&
        GetWindowThreadProcessId(window, &process_id) != 0 &&
        process_id == GetCurrentProcessId();
}

static HWND modal_button(HWND dialog, int id)
{
    HWND button = GetDlgItem(dialog, id);
    char class_name[16];
    if (!button || !IsWindowVisible(button) || !IsWindowEnabled(button) ||
        !GetClassNameA(button, class_name, sizeof(class_name)))
        return NULL;
    return _stricmp(class_name, "Button") == 0 ? button : NULL;
}

static int modal_affirmative_button_id(int has_ok, int has_yes,
    int has_retry, int has_continue)
{
    if (has_ok) return IDOK;
    if (has_yes) return IDYES;
    if (has_retry) return IDRETRY;
    if (has_continue) return IDCONTINUE;
    return 0;
}

static int modal_cancel_button_id(int has_cancel, int has_no)
{
    if (has_cancel) return IDCANCEL;
    if (has_no) return IDNO;
    return 0;
}

static HWND modal_affirmative_button(HWND dialog)
{
    int id = modal_affirmative_button_id(
        modal_button(dialog, IDOK) != NULL,
        modal_button(dialog, IDYES) != NULL,
        modal_button(dialog, IDRETRY) != NULL,
        modal_button(dialog, IDCONTINUE) != NULL);
    return id ? modal_button(dialog, id) : NULL;
}

static HWND modal_cancel_button(HWND dialog)
{
    int id = modal_cancel_button_id(
        modal_button(dialog, IDCANCEL) != NULL,
        modal_button(dialog, IDNO) != NULL);
    return id ? modal_button(dialog, id) : NULL;
}

static int modal_session_matches(HWND owner, HWND dialog)
{
    return modal_timer_id && modal_owner_window == owner &&
        modal_dialog_window == dialog;
}

enum modal_action {
    MODAL_ACTION_NONE,
    MODAL_ACTION_CONFIRM,
    MODAL_ACTION_CANCEL
};

static enum modal_action modal_action_from_edges(int confirm_edge,
    int cancel_edge)
{
    if (confirm_edge) return MODAL_ACTION_CONFIRM;
    if (cancel_edge) return MODAL_ACTION_CANCEL;
    return MODAL_ACTION_NONE;
}

static void stop_modal_input(void)
{
    if (modal_owner_window && modal_timer_id)
        KillTimer(modal_owner_window, modal_timer_id);
    modal_owner_window = NULL;
    modal_dialog_window = NULL;
    modal_timer_id = 0;
    modal_confirm_was_down = 0;
    modal_cancel_was_down = 0;
    modal_input_armed = 0;
    reset_gamepad_action_edges();
}

static void modal_input_timer(void)
{
    HWND foreground;
    struct gamepad_state pad;
    int confirm_down;
    int cancel_down;
    enum modal_action action;
    HWND button;
    DWORD result;

    if (!belongs_to_current_process(modal_owner_window) ||
        !belongs_to_current_process(modal_dialog_window) ||
        !IsWindowVisible(modal_dialog_window) ||
        GetWindow(modal_dialog_window, GW_OWNER) != modal_owner_window) {
        stop_modal_input();
        return;
    }

    if (!xinput_get_state) {
        modal_confirm_was_down = 0;
        modal_cancel_was_down = 0;
        modal_input_armed = 0;
        return;
    }

    result = xinput_get_state(0, &pad);
    if (result != ERROR_SUCCESS) {
        modal_confirm_was_down = 0;
        modal_cancel_was_down = 0;
        modal_input_armed = 0;
        return;
    }

    // Switch A（XInput B）只选择明确的肯定按钮，避免焦点默认在“否”时误操作。
    confirm_down = (pad.gamepad.buttons & XINPUT_GAMEPAD_B) != 0;
    cancel_down = (pad.gamepad.buttons & XINPUT_GAMEPAD_A) != 0;
    foreground = foreground_game_window();
    if (!foreground || !window_is_or_child_of(foreground, modal_dialog_window)) {
        modal_confirm_was_down = confirm_down;
        modal_cancel_was_down = cancel_down;
        modal_input_armed = 0;
        return;
    }

    // 新弹窗先等 A、B 都松开，避免打开弹窗时仍按住的按键直接确认下一层。
    if (!modal_input_armed) {
        modal_confirm_was_down = confirm_down;
        modal_cancel_was_down = cancel_down;
        if (!confirm_down && !cancel_down) modal_input_armed = 1;
        return;
    }

    action = modal_action_from_edges(confirm_down && !modal_confirm_was_down,
        cancel_down && !modal_cancel_was_down);
    modal_confirm_was_down = confirm_down;
    modal_cancel_was_down = cancel_down;
    if (action == MODAL_ACTION_CONFIRM) {
        button = modal_affirmative_button(modal_dialog_window);
        if (button) SendMessageA(button, BM_CLICK, 0, 0);
        return;
    }

    if (action == MODAL_ACTION_CANCEL) {
        button = modal_cancel_button(modal_dialog_window);
        if (button) SendMessageA(button, BM_CLICK, 0, 0);
        return;
    }
}

#if PAL3_KEYBOARD_GAMEPAD_DIAG
static DWORD keyboard_gamepad_diag_action(unsigned int key, int control_down)
{
    /* 短按与长按分别验收，避免持续方向样本越过只有三行的剧情选项。 */
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        if (key == VK_F1) return PAL3_DIAG_TAP | XINPUT_GAMEPAD_DPAD_UP;
        if (key == VK_F2) return PAL3_DIAG_TAP | XINPUT_GAMEPAD_DPAD_DOWN;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        if (key == VK_F1) return PAL3_DIAG_ZL;
        if (key == VK_F2) return PAL3_DIAG_ZR;
        if (key == VK_F12) return XINPUT_GAMEPAD_RIGHT_THUMB;
        if (key == VK_F9) return XINPUT_GAMEPAD_RIGHT_SHOULDER | PAL3_DIAG_RY_POS;
        if (key == VK_F10) return XINPUT_GAMEPAD_RIGHT_SHOULDER | PAL3_DIAG_RY_NEG;
        /* 本地诊断复用真实减号按钮路径，验证重复命令时不直接按原版 R。 */
        if (key == VK_F11) return XINPUT_GAMEPAD_BACK;
        if (key == VK_F3) return XINPUT_GAMEPAD_A | PAL3_DIAG_LX_NEG;
        if (key == VK_F4) return XINPUT_GAMEPAD_A | PAL3_DIAG_LX_POS;
    }
    if (control_down) {
        switch (key) {
        case VK_F1: return PAL3_DIAG_LX_NEG;
        case VK_F2: return PAL3_DIAG_LX_POS;
        case VK_F3: return XINPUT_GAMEPAD_LEFT_SHOULDER |
            XINPUT_GAMEPAD_DPAD_LEFT;
        case VK_F4: return XINPUT_GAMEPAD_LEFT_SHOULDER |
            XINPUT_GAMEPAD_DPAD_RIGHT;
        case VK_F5: return PAL3_DIAG_LY_POS;
        case VK_F6: return PAL3_DIAG_LY_NEG;
        case VK_F7: return PAL3_DIAG_RX_NEG;
        case VK_F8: return PAL3_DIAG_RX_POS;
        case VK_F9: return PAL3_DIAG_RY_POS;
        case VK_F10: return PAL3_DIAG_RY_NEG;
        case VK_F11: return XINPUT_GAMEPAD_LEFT_SHOULDER | PAL3_DIAG_RX_POS;
        default: return 0;
        }
    }

    switch (key) {
    case VK_F1: return XINPUT_GAMEPAD_DPAD_UP;
    case VK_F2: return XINPUT_GAMEPAD_DPAD_DOWN;
    case VK_F3: return XINPUT_GAMEPAD_DPAD_LEFT;
    case VK_F4: return XINPUT_GAMEPAD_DPAD_RIGHT;
    case VK_F5: return XINPUT_GAMEPAD_B;
    case VK_F12: return XINPUT_GAMEPAD_LEFT_THUMB;
    case VK_F6: return XINPUT_GAMEPAD_A;
    case VK_F7: return XINPUT_GAMEPAD_START;
    case VK_F8: return XINPUT_GAMEPAD_Y;
    case VK_F9: return XINPUT_GAMEPAD_LEFT_SHOULDER;
    case VK_F10: return XINPUT_GAMEPAD_RIGHT_SHOULDER;
    case VK_F11: return XINPUT_GAMEPAD_X;
    default: return 0;
    }
}

static int keyboard_gamepad_diag_is_driver_key(unsigned int key)
{
    return key >= VK_F1 && key <= VK_F12;
}

static int keyboard_gamepad_diag_press_samples(DWORD action)
{
    const WORD directions = XINPUT_GAMEPAD_DPAD_UP |
        XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
        XINPUT_GAMEPAD_DPAD_RIGHT;
    if (action & PAL3_DIAG_TAP) return 1;
    /* 诊断轴持续超过列表的 360ms 首次重复门槛，才能实测长按。 */
    if (action & (PAL3_DIAG_AXES | PAL3_DIAG_ZL | PAL3_DIAG_ZR)) return 96;
    if (!(action & directions)) return 1;
    if (!(action & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN))) return 8;
#if !defined(PAL3_GAMEPAD_TEST)
    /* 场景移动要持续一小段时间才能验证跑步；菜单与调速组合仍用短按。 */
    if (!(action & XINPUT_GAMEPAD_LEFT_SHOULDER) &&
        PAL3_s_gamestate == GAME_SCENE &&
        g_gamefrm.m_curfrmid == FRM_SCENE)
        return 64;
#endif
    return 96;
}

static void keyboard_gamepad_diag_begin(
    struct pal3_keyboard_gamepad_state *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->warmup_frames = 2;
}

static int keyboard_gamepad_diag_queue(
    struct pal3_keyboard_gamepad_state *state, DWORD action)
{
    if (!state || !action || state->warmup_frames > 0 ||
        state->phase != PAL3_KEYBOARD_GAMEPAD_IDLE) return 0;
    state->phase = PAL3_KEYBOARD_GAMEPAD_NEUTRAL_BEFORE;
    state->remaining_frames = 2;
    state->action = action;
    return 1;
}

static void keyboard_gamepad_diag_sample(
    struct pal3_keyboard_gamepad_state *state, DWORD *buttons)
{
    if (!state || !buttons) return;
    *buttons = 0;
    if (state->warmup_frames > 0) {
        --state->warmup_frames;
        return;
    }

    switch (state->phase) {
    case PAL3_KEYBOARD_GAMEPAD_NEUTRAL_BEFORE:
        if (--state->remaining_frames <= 0) {
            state->phase = PAL3_KEYBOARD_GAMEPAD_PRESS;
            state->remaining_frames = keyboard_gamepad_diag_press_samples(
                state->action);
        }
        break;
    case PAL3_KEYBOARD_GAMEPAD_PRESS:
        *buttons = state->action;
        if (--state->remaining_frames <= 0) {
            state->phase = PAL3_KEYBOARD_GAMEPAD_NEUTRAL_AFTER;
            state->remaining_frames = 2;
        }
        break;
    case PAL3_KEYBOARD_GAMEPAD_NEUTRAL_AFTER:
        if (--state->remaining_frames <= 0) {
            state->phase = PAL3_KEYBOARD_GAMEPAD_IDLE;
            state->action = 0;
        }
        break;
    default:
        break;
    }
}

static void keyboard_gamepad_diag_decode(DWORD action, struct gamepad_state *pad)
{
    memset(pad, 0, sizeof(*pad));
    pad->gamepad.buttons = (WORD)action;
    pad->gamepad.left_trigger = (action & PAL3_DIAG_ZL) ? 255 : 0;
    pad->gamepad.right_trigger = (action & PAL3_DIAG_ZR) ? 255 : 0;
    pad->gamepad.thumb_lx = (action & PAL3_DIAG_LX_NEG) ? -24000 :
        (action & PAL3_DIAG_LX_POS) ? 24000 : 0;
    pad->gamepad.thumb_ly = (action & PAL3_DIAG_LY_NEG) ? -24000 :
        (action & PAL3_DIAG_LY_POS) ? 24000 : 0;
    pad->gamepad.thumb_rx = (action & PAL3_DIAG_RX_NEG) ? -24000 :
        (action & PAL3_DIAG_RX_POS) ? 24000 : 0;
    pad->gamepad.thumb_ry = (action & PAL3_DIAG_RY_NEG) ? -24000 :
        (action & PAL3_DIAG_RY_POS) ? 24000 : 0;
}

/* 只在正常游戏或视频的输入采样中推进，生成的数据仍走原有手柄处理。 */
static void keyboard_gamepad_diag_disable(const char *reason)
{
    int was_enabled = InterlockedExchange(&keyboard_gamepad_diag_enabled, 0);
    if (!was_enabled) return;
    InterlockedExchange(&keyboard_gamepad_diag_ready, 0);
    InterlockedExchange(&keyboard_gamepad_diag_start, 0);
    InterlockedExchange(&keyboard_gamepad_diag_busy, 0);
    InterlockedExchange(&keyboard_gamepad_diag_pending_action, 0);
    InterlockedExchange(&keyboard_gamepad_diag_filter_pending, 2);
    InterlockedExchange(&keyboard_gamepad_diag_sample_active, 0);
    memset(&keyboard_gamepad_diag_state, 0,
        sizeof(keyboard_gamepad_diag_state));
    if (was_enabled)
        plog("[PAL3KeyboardPadDiag] disabled: %s", reason);
}

static int keyboard_gamepad_diag_sample_pad(struct gamepad_state *pad)
{
    DWORD buttons = 0;
    LONG pending_action;
    if (!pad) return 0;
    if (!InterlockedCompareExchange(&keyboard_gamepad_diag_enabled, 0, 0)) {
        InterlockedExchange(&keyboard_gamepad_diag_sample_active, 0);
        if (keyboard_gamepad_diag_state.warmup_frames ||
            keyboard_gamepad_diag_state.phase ||
            keyboard_gamepad_diag_state.remaining_frames ||
            keyboard_gamepad_diag_state.action)
            memset(&keyboard_gamepad_diag_state, 0,
                sizeof(keyboard_gamepad_diag_state));
        return 0;
    }

    memset(pad, 0, sizeof(*pad));
    InterlockedExchange(&keyboard_gamepad_diag_sample_active, 1);
    if (InterlockedExchange(&keyboard_gamepad_diag_start, 0)) {
        keyboard_gamepad_diag_begin(&keyboard_gamepad_diag_state);
        plog("[PAL3KeyboardPadDiag] enabled; waiting for 2 neutral game samples");
    }

    if (keyboard_gamepad_diag_state.warmup_frames > 0) {
        keyboard_gamepad_diag_sample(&keyboard_gamepad_diag_state, &buttons);
        if (!keyboard_gamepad_diag_state.warmup_frames) {
            InterlockedExchange(&keyboard_gamepad_diag_ready, 1);
            plog("[PAL3KeyboardPadDiag] ready");
        }
        return 1;
    }

    if (keyboard_gamepad_diag_state.phase == PAL3_KEYBOARD_GAMEPAD_IDLE) {
        pending_action = InterlockedExchange(
            &keyboard_gamepad_diag_pending_action, 0);
        if (pending_action) {
            if (keyboard_gamepad_diag_queue(&keyboard_gamepad_diag_state,
                    (DWORD)pending_action)) {
                plog("[PAL3KeyboardPadDiag] action queued: action=0x%08lx",
                    (unsigned long)(DWORD)pending_action);
            } else {
                InterlockedExchange(&keyboard_gamepad_diag_busy, 0);
                plog("[PAL3KeyboardPadDiag] action dropped: provider is busy");
            }
        }
    }

    keyboard_gamepad_diag_sample(&keyboard_gamepad_diag_state, &buttons);
    keyboard_gamepad_diag_decode(buttons, pad);
    if (keyboard_gamepad_diag_state.phase == PAL3_KEYBOARD_GAMEPAD_IDLE &&
        InterlockedCompareExchange(&keyboard_gamepad_diag_busy, 0, 0)) {
        InterlockedExchange(&keyboard_gamepad_diag_busy, 0);
        plog("[PAL3KeyboardPadDiag] action sequence complete");
    }
    return 1;
}

static void keyboard_gamepad_diag_filter_raw_keys(void)
{
    static const BYTE scan_codes[] = {
        DIK_LCONTROL, DIK_RCONTROL, DIK_LSHIFT, DIK_RSHIFT,
        0x38, 0xB8, /* Alt 仅用于本地镜头诊断，不能流进游戏操作。 */
        DIK_F1, DIK_F2, DIK_F3, DIK_F4,
        DIK_F5, DIK_F6, DIK_F7, DIK_F8, DIK_F9, DIK_F10, DIK_F11, DIK_F12
    };
    size_t i;
    for (i = 0; i < sizeof(scan_codes) / sizeof(scan_codes[0]); ++i)
        g_input.m_keyRaw[scan_codes[i]] = 0;
}

static int keyboard_gamepad_diag_toggle(void)
{
    if (InterlockedCompareExchange(&keyboard_gamepad_diag_enabled, 0, 0)) {
        InterlockedExchange(&keyboard_gamepad_diag_enabled, 0);
        InterlockedExchange(&keyboard_gamepad_diag_ready, 0);
        InterlockedExchange(&keyboard_gamepad_diag_start, 0);
        InterlockedExchange(&keyboard_gamepad_diag_busy, 0);
        InterlockedExchange(&keyboard_gamepad_diag_pending_action, 0);
        InterlockedExchange(&keyboard_gamepad_diag_filter_pending, 2);
        InterlockedExchange(&keyboard_gamepad_diag_sample_active, 0);
        plog("[PAL3KeyboardPadDiag] disabled: Ctrl+F12");
        return 0;
    }
    InterlockedExchange(&keyboard_gamepad_diag_ready, 0);
    InterlockedExchange(&keyboard_gamepad_diag_start, 1);
    InterlockedExchange(&keyboard_gamepad_diag_busy, 0);
    InterlockedExchange(&keyboard_gamepad_diag_pending_action, 0);
    InterlockedExchange(&keyboard_gamepad_diag_filter_pending, 2);
    InterlockedExchange(&keyboard_gamepad_diag_enabled, 1);
    plog("[PAL3KeyboardPadDiag] enabling; Ctrl+F12 disables");
    return 1;
}

static void keyboard_gamepad_diag_enqueue(DWORD action)
{
    if (!InterlockedCompareExchange(&keyboard_gamepad_diag_ready, 0, 0)) {
        InterlockedExchange(&keyboard_gamepad_diag_filter_pending, 2);
        plog("[PAL3KeyboardPadDiag] action dropped: provider is warming up");
        return;
    }
    InterlockedExchange(&keyboard_gamepad_diag_filter_pending, 2);
    if (InterlockedCompareExchange(&keyboard_gamepad_diag_busy, 1, 0)) {
        plog("[PAL3KeyboardPadDiag] action dropped: previous sequence is busy");
        return;
    }
    InterlockedExchange(&keyboard_gamepad_diag_pending_action, action);
}
#endif

static void wndproc_hook(void *arg)
{
    struct wndproc_hook_data *data = (struct wndproc_hook_data *)arg;
    HWND dialog;
    DWORD owner_process = 0;
    DWORD dialog_process = 0;

    if (!data) return;

    if (PAL3Mod_Enabled(PAL3_MOD_CHEATS) && data->Msg == WM_KEYUP && data->wParam == 'Y' &&
        data->hWnd == foreground_game_window() && PAL3MenuNav_ToggleCheat()) {
        data->retvalue = 0;
        data->processed = 1;
        return;
    }

    if (PAL3MenuNav_MouseMessage(data->Msg, (long)data->lParam)) {
        data->retvalue = 0;
        data->processed = 1;
        return;
    }

#if PAL3_KEYBOARD_GAMEPAD_DIAG
    if ((data->Msg == WM_KEYDOWN || data->Msg == WM_SYSKEYDOWN) &&
        data->hWnd == foreground_game_window()) {
        unsigned int key = (unsigned int)data->wParam;
        int control_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int repeat = ((unsigned long)data->lParam & (1UL << 30)) != 0;
        if (key == VK_F12 && control_down) {
            if (!repeat) keyboard_gamepad_diag_toggle();
            data->retvalue = 0;
            data->processed = 1;
            return;
        }
        if (InterlockedCompareExchange(&keyboard_gamepad_diag_enabled, 0, 0)) {
            DWORD action = keyboard_gamepad_diag_action(key, control_down);
            /* 同一样本确认对话并持有摇杆，才能覆盖很短的剧情接管过程。 */
            if ((action & PAL3_DIAG_AXES) &&
                !(action & XINPUT_GAMEPAD_LEFT_SHOULDER) &&
                (GetKeyState(VK_SHIFT) & 0x8000))
                action |= XINPUT_GAMEPAD_B;
            if (action) {
                InterlockedExchange(&keyboard_gamepad_diag_filter_pending, 2);
                if (!repeat) keyboard_gamepad_diag_enqueue(action);
                data->retvalue = 0;
                data->processed = 1;
                return;
            }
        }
    }
    if ((data->Msg == WM_KEYUP || data->Msg == WM_SYSKEYUP) &&
        data->hWnd == foreground_game_window()) {
        unsigned int key = (unsigned int)data->wParam;
        if (key == VK_F12 ||
            (InterlockedCompareExchange(&keyboard_gamepad_diag_enabled,
                0, 0) && keyboard_gamepad_diag_is_driver_key(key))) {
            data->retvalue = 0;
            data->processed = 1;
            return;
        }
    }
#endif

    if (!PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) return;
    if (data->Msg == WM_ENTERIDLE && data->wParam == MSGF_DIALOGBOX) {
        dialog = (HWND)data->lParam;
        if (!belongs_to_current_process(data->hWnd) ||
            !belongs_to_current_process(dialog) || !IsWindowVisible(dialog) ||
            GetWindow(dialog, GW_OWNER) != data->hWnd)
            return;
        GetWindowThreadProcessId(data->hWnd, &owner_process);
        GetWindowThreadProcessId(dialog, &dialog_process);
        if (owner_process != dialog_process) return;
        {
            char class_name[16];
            if (!GetClassNameA(dialog, class_name, sizeof(class_name)) ||
                strcmp(class_name, "#32770") != 0)
                return;
        }
        if (modal_session_matches(data->hWnd, dialog))
            return;
        stop_modal_input();
        modal_owner_window = data->hWnd;
        modal_dialog_window = dialog;
        modal_timer_id = SetTimer(modal_owner_window, 0, 16, NULL);
        if (!modal_timer_id) stop_modal_input();
        return;
    }

    if (data->Msg == WM_TIMER && modal_timer_id &&
        data->hWnd == modal_owner_window && data->wParam == modal_timer_id)
        modal_input_timer();
    else if ((data->Msg == WM_NCDESTROY && data->hWnd == modal_owner_window) ||
        (data->Msg == WM_DESTROY && data->hWnd == modal_dialog_window))
        stop_modal_input();
}

static int button_down(WORD mask)
{
    return pad_connected && (current_pad.gamepad.buttons & mask) != 0;
}

static void update_gamepad_action_edges(void)
{
    int confirm = button_down(XINPUT_GAMEPAD_B);
    int cancel = button_down(XINPUT_GAMEPAD_A);
    gamepad_confirm_edge = 0;
    gamepad_cancel_edge = 0;
    if (!gamepad_actions_armed) {
        previous_gamepad_confirm = confirm;
        previous_gamepad_cancel = cancel;
        if (!confirm && !cancel) gamepad_actions_armed = 1;
        return;
    }
    gamepad_confirm_edge = confirm && !previous_gamepad_confirm;
    gamepad_cancel_edge = cancel && !previous_gamepad_cancel;
    previous_gamepad_confirm = confirm;
    previous_gamepad_cancel = cancel;
}

static void update_mouse_speed(int direction)
{
    float next_speed = mouse_speed + (float)direction * MOUSE_SPEED_STEP;
    char percent_text[8];
    int percent;
    if (next_speed < MOUSE_SPEED_MIN) next_speed = MOUSE_SPEED_MAX;
    if (next_speed > MOUSE_SPEED_MAX) next_speed = MOUSE_SPEED_MIN;
    if (next_speed == mouse_speed) return;
    mouse_speed = next_speed;
    mouse_speed_notice_tick = GetTickCount();
    percent = (int)(mouse_speed / 9.0f + 0.5f);
    snprintf(percent_text, sizeof(percent_text), "%d", percent);
#if PAL3_KEYBOARD_GAMEPAD_DIAG
    if (InterlockedCompareExchange(&keyboard_gamepad_diag_sample_active, 0, 0)) {
#if !defined(PAL3_GAMEPAD_TEST)
        plog("[PAL3KeyboardPadDiag] mouse speed changed in memory; not saved");
#endif
    } else
#endif
    if (!mouse_speed_config_path[0]) {
#if !defined(PAL3_GAMEPAD_TEST)
        plog("Cannot save mouse sensitivity: config path unavailable");
#endif
    } else if (!WritePrivateProfileStringA(MOUSE_SPEED_CONFIG_SECTION,
            MOUSE_SPEED_CONFIG_KEY, percent_text, mouse_speed_config_path)) {
#if !defined(PAL3_GAMEPAD_TEST)
        plog("Failed to save mouse sensitivity (error=%lu)", GetLastError());
#endif
    }
#if !defined(PAL3_GAMEPAD_TEST)
    plog("Mouse cursor speed changed to %.0f pixels per second", mouse_speed);
#endif
}

static int build_mouse_speed_config_path(const char *exe_path, char *out, size_t out_size)
{
    const char *backslash;
    const char *slash;
    const char *last_separator;
    size_t directory_length;
    static const char relative_path[] = "save\\PAL3Gamepad.ini";

    if (!exe_path || !out || out_size == 0) return 0;
    if (strlen(exe_path) < 3) return 0;
    if (!((exe_path[0] >= 'A' && exe_path[0] <= 'Z') ||
        (exe_path[0] >= 'a' && exe_path[0] <= 'z')) ||
        exe_path[1] != ':' || (exe_path[2] != '\\' && exe_path[2] != '/'))
        return 0;
    backslash = strrchr(exe_path, '\\');
    slash = strrchr(exe_path, '/');
    if (!backslash) last_separator = slash;
    else if (!slash) last_separator = backslash;
    else last_separator = backslash > slash ? backslash : slash;
    if (!last_separator) return 0;
    directory_length = (size_t)(last_separator - exe_path) + 1;
    if (directory_length + sizeof(relative_path) > out_size) return 0;
    memcpy(out, exe_path, directory_length);
    memcpy(out + directory_length, relative_path, sizeof(relative_path));
    return 1;
}

static int initialize_mouse_speed_config_path(void)
{
    char exe_path[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (length == 0 || length >= sizeof(exe_path)) return 0;
    exe_path[length] = '\0';
    return build_mouse_speed_config_path(exe_path, mouse_speed_config_path,
        sizeof(mouse_speed_config_path));
}

static float mouse_speed_from_percent(int percent)
{
    if (percent < 50 || percent > 150 || (percent - 50) % 25 != 0)
        percent = MOUSE_SPEED_DEFAULT_PERCENT;
    return (float)percent * 9.0f;
}

static int mouse_speed_adjustment_direction(int modifier_down, int left_down,
    int right_down, int previous_left, int previous_right)
{
    if (!modifier_down) return 0;
    if (left_down && !previous_left && !right_down) return -1;
    if (right_down && !previous_right && !left_down) return 1;
    return 0;
}

static void load_mouse_speed(void)
{
    int percent;
    if (!mouse_speed_config_path[0]) {
#if !defined(PAL3_GAMEPAD_TEST)
        plog("Mouse sensitivity config path unavailable; using default 100%%");
#endif
        return;
    }
    percent = (int)GetPrivateProfileIntA(MOUSE_SPEED_CONFIG_SECTION,
        MOUSE_SPEED_CONFIG_KEY, MOUSE_SPEED_DEFAULT_PERCENT, mouse_speed_config_path);
    mouse_speed = mouse_speed_from_percent(percent);
    scene_walk_mode = GetPrivateProfileIntA(MOUSE_SPEED_CONFIG_SECTION,
        "default_walk", 0, mouse_speed_config_path) == 1;
    {
        int i;
        for (i = 0; i < 2; ++i) {
            int rate = GetPrivateProfileIntA(MOUSE_SPEED_CONFIG_SECTION,
                i ? "skill_experience_rate" : "role_experience_rate", 1,
                mouse_speed_config_path);
            experience_rates[i] = PAL3Mod_Enabled(PAL3_MOD_CHEATS) && rate >= 1 && rate <= 10 ? rate : 1;
        }
    }
    percent = (int)(mouse_speed / 9.0f + 0.5f);
#if !defined(PAL3_GAMEPAD_TEST)
    plog("Mouse cursor speed loaded at %d%%", percent);
#endif
}

static void mouse_speed_onendscene(void)
{
    wchar_t notice[64];
    DWORD elapsed;
    int percent;
    if (walk_notice_tick && GetTickCount() - walk_notice_tick < MOUSE_SPEED_NOTICE_MS) {
        print_wstring_begin();
        print_wstring(FONTID_U12, scene_walk_mode ? L"默认移动：走路" : L"默认移动：跑步",
            12, 34, 0xFFFFFF00);
        print_wstring_end();
    }
    if (!mouse_speed_notice_tick) return;
    elapsed = GetTickCount() - mouse_speed_notice_tick;
    if (elapsed >= MOUSE_SPEED_NOTICE_MS) {
        mouse_speed_notice_tick = 0;
        return;
    }

    percent = (int)(mouse_speed / 9.0f + 0.5f);
    swprintf(notice, sizeof(notice) / sizeof(notice[0]), L"鼠标灵敏度：%d%%", percent);
    print_wstring_begin();
    print_wstring(FONTID_U12, notice, 12, 12, 0xFFFFFF00);
    print_wstring_end();
}

static void update_game_cursor_override(int managed, int visible)
{
    // 游戏启动早期尚未创建光标对象；等进入有效游戏状态后再设置。
    if (PAL3_s_gamestate == GAME_NONE) {
        cursor_override_active = 0;
        return;
    }

    if (!managed) {
        if (cursor_override_active) {
            set_showcursor_state(cursor_visibility_before_override);
            cursor_override_active = 0;
        }
        return;
    }

    if (!cursor_override_active) {
        cursor_visibility_before_override = get_showcursor_state();
        cursor_override_active = 1;
    }

    if (get_showcursor_state() != !!visible)
        set_showcursor_state(visible);
}

static int axis_active(SHORT value, int deadzone)
{
    return value > deadzone || value < -deadzone;
}

static void keyboard_state_hook(void)
{
    int up;
    int down;
    int left;
    int right;
    int controller_navigation_allowed;
#if PAL3_KEYBOARD_GAMEPAD_DIAG
    if (InterlockedCompareExchange(&keyboard_gamepad_diag_enabled, 0, 0) ||
        InterlockedCompareExchange(&keyboard_gamepad_diag_filter_pending,
            0, 0) > 0) {
        keyboard_gamepad_diag_filter_raw_keys();
        if (!InterlockedCompareExchange(&keyboard_gamepad_diag_enabled, 0, 0) &&
            InterlockedCompareExchange(&keyboard_gamepad_diag_filter_pending,
                0, 0) > 0)
            InterlockedDecrement(&keyboard_gamepad_diag_filter_pending);
    }
#endif
    if (!PAL3Mod_Enabled(PAL3_MOD_CONTROLLER) || !focused || !pad_connected) return;

#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    if (gamepad_confirm_edge || gamepad_cancel_edge)
        plog("[PAL3InputEdge] keyboard confirm=%d cancel=%d menu=0x%lx combat=0x%lx",
            gamepad_confirm_edge, gamepad_cancel_edge,
            (unsigned long)menu_nav_consumed, (unsigned long)combat_nav_consumed);
#endif

    // 方向和镜头状态必须来自同一份成功的 XInput 采样，不能落后一帧。
    update_scene_input();
    scene_jump_pending = scene_input_active && gamepad_cancel_edge &&
        !(menu_nav_consumed & PAL3_MENU_NAV_CANCEL);
    if (combat_nav_consumed & PAL3_MENU_NAV_REPEAT) {
        /* 原版 R 已实现重复上次命令及其有效性检查，不直接写命令队列。 */
        g_input.m_keyRaw[DIK_R] = 0x80;
        combat_nav_consumed &= ~PAL3_MENU_NAV_REPEAT;
    }

    // 只叠加到本次 DirectInput 采样结果上；下一次采样会重新给出真实键盘状态。
    // Switch 实体 A/B 与 XInput 的 A/B 标签位置相反。
    if (gamepad_confirm_edge &&
        !scene_mouse_mode_active(pad_connected) &&
        !(menu_nav_consumed & PAL3_MENU_NAV_CONFIRM) &&
        !(combat_nav_consumed & PAL3_MENU_NAV_CONFIRM))
        g_input.m_keyRaw[DIK_SPACE] = 0x80;
    if ((PAL3_s_gamestate == GAME_UI || PAL3_s_gamestate == GAME_COMBAT) &&
        should_inject_gamepad_cancel(gamepad_cancel_edge,
            menu_nav_consumed, combat_nav_consumed))
        g_input.m_keyRaw[DIK_ESCAPE] = 0x80;
    gamepad_confirm_edge = 0;
    gamepad_cancel_edge = 0;

    controller_navigation_allowed = gamepad_direction_injection_allowed(
        button_down(XINPUT_GAMEPAD_LEFT_SHOULDER)) &&
        scene_direction_fallback_allowed(PAL3_s_gamestate == GAME_SCENE,
            scene_select_dialog_is_active());
    up = scene_input_active ?
        (scene_input_output.directions & PAL3_SCENE_INPUT_UP) != 0 :
        (controller_navigation_allowed &&
         ((current_pad.gamepad.buttons & XINPUT_GAMEPAD_DPAD_UP) ||
          current_pad.gamepad.thumb_ly > XINPUT_GAMEPAD_LEFT_DEADZONE));
    down = scene_input_active ?
        (scene_input_output.directions & PAL3_SCENE_INPUT_DOWN) != 0 :
        (controller_navigation_allowed &&
         ((current_pad.gamepad.buttons & XINPUT_GAMEPAD_DPAD_DOWN) ||
          current_pad.gamepad.thumb_ly < -XINPUT_GAMEPAD_LEFT_DEADZONE));
    left = scene_input_active ?
        (scene_input_output.directions & PAL3_SCENE_INPUT_LEFT) != 0 :
        (controller_navigation_allowed &&
         ((current_pad.gamepad.buttons & XINPUT_GAMEPAD_DPAD_LEFT) ||
          current_pad.gamepad.thumb_lx < -XINPUT_GAMEPAD_LEFT_DEADZONE));
    right = scene_input_active ?
        (scene_input_output.directions & PAL3_SCENE_INPUT_RIGHT) != 0 :
        (controller_navigation_allowed &&
         ((current_pad.gamepad.buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ||
          current_pad.gamepad.thumb_lx > XINPUT_GAMEPAD_LEFT_DEADZONE));
    if (pal3_gamepad_should_inject_direction(menu_nav_consumed,
            combat_nav_consumed, PAL3_MENU_NAV_UP, up))
        g_input.m_keyRaw[DIK_UP] = 0x80;
    if (pal3_gamepad_should_inject_direction(menu_nav_consumed,
            combat_nav_consumed, PAL3_MENU_NAV_DOWN, down))
        g_input.m_keyRaw[DIK_DOWN] = 0x80;
    if (pal3_gamepad_should_inject_direction(menu_nav_consumed,
            combat_nav_consumed, PAL3_MENU_NAV_LEFT, left))
        g_input.m_keyRaw[DIK_LEFT] = 0x80;
    if (pal3_gamepad_should_inject_direction(menu_nav_consumed,
            combat_nav_consumed, PAL3_MENU_NAV_RIGHT, right))
        g_input.m_keyRaw[DIK_RIGHT] = 0x80;
    if (scene_input_active) {
        if (scene_input_output.camera_keys & PAL3_SCENE_INPUT_CAMERA_A)
            g_input.m_keyRaw[DIK_A] = 0x80;
        if (scene_input_output.camera_keys & PAL3_SCENE_INPUT_CAMERA_D)
            g_input.m_keyRaw[DIK_D] = 0x80;
    }
}

#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
static void read_dialog_state(int output[4], const struct UIDialog *dialog)
{
    output[0] = dialog->baseclass.m_bcreateok;
    output[1] = dialog->baseclass.m_bvisible;
    output[2] = dialog->baseclass.m_benable;
    output[3] = dialog->m_bmodal;
}

static void read_rest_dialog_state(int output[8], const struct MUIDialog *dialog)
{
    read_dialog_state(output, &dialog->baseclass_0);
    output[4] = dialog->m_IsModel;
    output[5] = dialog->m_isDoModel;
    output[6] = dialog->m_type;
    output[7] = dialog->m_getMsg;
}

static void log_scene_state_snapshot(void)
{
    struct pal3_scene_state_snapshot snapshot;
    DWORD now = GetTickCount();
    int i;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.scene[0] = PAL3_s_gamestate;
    snapshot.scene[1] = g_gamefrm.m_curfrmid;
    snapshot.scene[2] = g_gamefrm.m_scenefrm != NULL;
    if (g_gamefrm.m_scenefrm) {
        const struct UIWnd *frame = &g_gamefrm.m_scenefrm->baseclass.baseclass;
        snapshot.scene[3] = frame->m_bcreateok;
        snapshot.scene[4] = frame->m_bvisible;
        snapshot.scene[5] = frame->m_benable;
    }

    read_dialog_state(snapshot.role_dialog, &g_gamefrm.m_roledlg.baseclass);
    snapshot.role_dialog[4] = g_gamefrm.m_roledlg.m_mode;
    snapshot.role_dialog[5] = g_gamefrm.m_roledlg.m_sel;
    read_dialog_state(snapshot.select_dialog, &g_gamefrm.m_seldlg.baseclass);
    snapshot.select_dialog[4] = g_gamefrm.m_seldlg.m_sel;
    for (i = 0; i < 5; ++i)
        read_dialog_state(snapshot.head_dialogs[i], &g_gamefrm.m_headdlg[i].baseclass);

    snapshot.chat_rest[0] = g_gamefrm.m_ChatRest.baseclass.m_bcreateok;
    snapshot.chat_rest[1] = g_gamefrm.m_ChatRest.baseclass.m_bvisible;
    snapshot.chat_rest[2] = g_gamefrm.m_ChatRest.baseclass.m_benable;
    snapshot.chat_rest[3] = g_gamefrm.m_ChatRest.m_bActive;
    read_rest_dialog_state(snapshot.rest_dialogs[0], &g_gamefrm.m_ChatRest.m_answerDlg);
    read_rest_dialog_state(snapshot.rest_dialogs[1], &g_gamefrm.m_ChatRest.m_noenoughmsg);
    read_rest_dialog_state(snapshot.rest_dialogs[2], &g_gamefrm.m_ChatRest.m_enough);
    read_rest_dialog_state(snapshot.rest_dialogs[3], &g_gamefrm.m_ChatRest.m_restover);

    if (scene_state_snapshot_initialized &&
        memcmp(&snapshot, &previous_scene_state_snapshot, sizeof(snapshot)) == 0)
        return;
    if (scene_state_snapshot_initialized &&
        (DWORD)(now - previous_scene_state_log_tick) < 500)
        return;

    previous_scene_state_snapshot = snapshot;
    previous_scene_state_log_tick = now;
    scene_state_snapshot_initialized = 1;
    ++scene_state_snapshot_id;

    plog("[PAL3SceneStateDiag] snap=%lu scene(gs/frame/exist/create/visible/enable)=%d/%d/%d/%d/%d/%d role(create/visible/enable/modal/mode/sel)=%d/%d/%d/%d/%d/%d select(create/visible/enable/modal/sel)=%d/%d/%d/%d/%d chatrest(create/visible/enable/active)=%d/%d/%d/%d",
        scene_state_snapshot_id,
        snapshot.scene[0], snapshot.scene[1], snapshot.scene[2],
        snapshot.scene[3], snapshot.scene[4], snapshot.scene[5],
        snapshot.role_dialog[0], snapshot.role_dialog[1], snapshot.role_dialog[2],
        snapshot.role_dialog[3], snapshot.role_dialog[4], snapshot.role_dialog[5],
        snapshot.select_dialog[0], snapshot.select_dialog[1], snapshot.select_dialog[2],
        snapshot.select_dialog[3], snapshot.select_dialog[4],
        snapshot.chat_rest[0], snapshot.chat_rest[1], snapshot.chat_rest[2],
        snapshot.chat_rest[3]);
    plog("[PAL3SceneStateDiag] snap=%lu heads(create/visible/enable/modal)=%d/%d/%d/%d;%d/%d/%d/%d;%d/%d/%d/%d;%d/%d/%d/%d;%d/%d/%d/%d",
        scene_state_snapshot_id,
        snapshot.head_dialogs[0][0], snapshot.head_dialogs[0][1], snapshot.head_dialogs[0][2], snapshot.head_dialogs[0][3],
        snapshot.head_dialogs[1][0], snapshot.head_dialogs[1][1], snapshot.head_dialogs[1][2], snapshot.head_dialogs[1][3],
        snapshot.head_dialogs[2][0], snapshot.head_dialogs[2][1], snapshot.head_dialogs[2][2], snapshot.head_dialogs[2][3],
        snapshot.head_dialogs[3][0], snapshot.head_dialogs[3][1], snapshot.head_dialogs[3][2], snapshot.head_dialogs[3][3],
        snapshot.head_dialogs[4][0], snapshot.head_dialogs[4][1], snapshot.head_dialogs[4][2], snapshot.head_dialogs[4][3]);
    plog("[PAL3SceneStateDiag] snap=%lu rest(answer/noenough/enough/over)(create/visible/enable/modal/isModel/isDoModel/type/getMsg)=%d/%d/%d/%d/%d/%d/%d/%d;%d/%d/%d/%d/%d/%d/%d/%d;%d/%d/%d/%d/%d/%d/%d/%d;%d/%d/%d/%d/%d/%d/%d/%d",
        scene_state_snapshot_id,
        snapshot.rest_dialogs[0][0], snapshot.rest_dialogs[0][1], snapshot.rest_dialogs[0][2], snapshot.rest_dialogs[0][3], snapshot.rest_dialogs[0][4], snapshot.rest_dialogs[0][5], snapshot.rest_dialogs[0][6], snapshot.rest_dialogs[0][7],
        snapshot.rest_dialogs[1][0], snapshot.rest_dialogs[1][1], snapshot.rest_dialogs[1][2], snapshot.rest_dialogs[1][3], snapshot.rest_dialogs[1][4], snapshot.rest_dialogs[1][5], snapshot.rest_dialogs[1][6], snapshot.rest_dialogs[1][7],
        snapshot.rest_dialogs[2][0], snapshot.rest_dialogs[2][1], snapshot.rest_dialogs[2][2], snapshot.rest_dialogs[2][3], snapshot.rest_dialogs[2][4], snapshot.rest_dialogs[2][5], snapshot.rest_dialogs[2][6], snapshot.rest_dialogs[2][7],
        snapshot.rest_dialogs[3][0], snapshot.rest_dialogs[3][1], snapshot.rest_dialogs[3][2], snapshot.rest_dialogs[3][3], snapshot.rest_dialogs[3][4], snapshot.rest_dialogs[3][5], snapshot.rest_dialogs[3][6], snapshot.rest_dialogs[3][7]);
}

static struct pal3_ui_message_diag_slot *find_ui_message_diag_slot(
    struct UIWndVtbl *vtable)
{
    int i;
    for (i = 0; i < PAL3_UI_MESSAGE_DIAG_SLOTS; ++i)
        if (ui_message_diag_slots[i].used &&
            &ui_message_diag_slots[i].copy == vtable)
            return &ui_message_diag_slots[i];
    return NULL;
}

static struct pal3_ui_message_diag_slot *find_ui_message_diag_window(
    struct UIWnd *window)
{
    int i;
    for (i = 0; i < PAL3_UI_MESSAGE_DIAG_SLOTS; ++i)
        if (ui_message_diag_slots[i].used &&
            ui_message_diag_slots[i].window == window)
            return &ui_message_diag_slots[i];
    return NULL;
}

static MAKE_THISCALL(void, observe_ui_render, struct UIWnd *this)
{
    struct pal3_ui_message_diag_slot *slot = find_ui_message_diag_slot(this->vfptr);
    if (!slot || !slot->original || !slot->original->Render) return;
    InterlockedIncrement(&ui_render_diag_count);
    THISCALL_WRAPPER(slot->original->Render, this);
}

static MAKE_THISCALL(int, observe_ui_on_message, struct UIWnd *this,
    unsigned int message, unsigned int wparam, unsigned int lparam)
{
    struct pal3_ui_message_diag_slot *slot = find_ui_message_diag_slot(this->vfptr);
    int before_game_state = PAL3_s_gamestate;
    int before_frame_id = g_gamefrm.m_curfrmid;
    int result;
    int duplicate;
    int i;

    if (!slot || !slot->original || !slot->original->OnMessage) return 0;
    result = THISCALL_WRAPPER(slot->original->OnMessage, this,
        message, wparam, lparam);

    if (message == WM_MOUSEMOVE) return result;
    if (!slot->tuple_context_initialized ||
        slot->tuple_game_state != before_game_state ||
        slot->tuple_frame_id != before_frame_id) {
        memset(slot->tuples, 0, sizeof(slot->tuples));
        slot->tuple_cursor = 0;
        slot->tuple_game_state = before_game_state;
        slot->tuple_frame_id = before_frame_id;
        slot->tuple_context_initialized = 1;
    }
    duplicate = 0;
    for (i = 0; i < PAL3_UI_MESSAGE_DIAG_TUPLES; ++i) {
        struct pal3_ui_message_diag_tuple *tuple = &slot->tuples[i];
        if (tuple->used && tuple->message == message &&
            tuple->wparam == wparam && tuple->lparam == lparam) {
            duplicate = 1;
            break;
        }
    }
    if (!duplicate) {
        plog("[PAL3UiMessageDiag] source=%s frame=%d->%d state=%d->%d msg=%u wparam=%u lparam=%u result=%d tick=%lu",
            slot->source, before_frame_id, g_gamefrm.m_curfrmid,
            before_game_state, PAL3_s_gamestate, message, wparam, lparam,
            result, (unsigned long)GetTickCount());
        slot->tuples[slot->tuple_cursor].message = message;
        slot->tuples[slot->tuple_cursor].wparam = wparam;
        slot->tuples[slot->tuple_cursor].lparam = lparam;
        slot->tuples[slot->tuple_cursor].used = 1;
        slot->tuple_cursor = (slot->tuple_cursor + 1) % PAL3_UI_MESSAGE_DIAG_TUPLES;
    }
    return result;
}

static void observe_ui_message_window(struct UIWnd *window, const char *source)
{
    struct pal3_ui_message_diag_slot *slot;
    int i;

    if (!window || !window->m_bcreateok || !window->vfptr ||
        !window->vfptr->OnMessage)
        return;
    slot = find_ui_message_diag_window(window);
    if (slot) return;
    slot = find_ui_message_diag_slot(window->vfptr);
    if (slot) return;

    for (i = 0; i < PAL3_UI_MESSAGE_DIAG_SLOTS; ++i) {
        if (!ui_message_diag_slots[i].used) {
            slot = &ui_message_diag_slots[i];
            slot->used = 1;
            slot->window = window;
            slot->original = window->vfptr;
            slot->copy = *window->vfptr;
            slot->source = source;
            slot->copy.Render = observe_ui_render;
            slot->copy.OnMessage = observe_ui_on_message;
            window->vfptr = &slot->copy;
            plog("[PAL3UiMessageDiag] installed source=%s", source);
            return;
        }
    }
    if (!ui_message_diag_slots_full_logged) {
        ui_message_diag_slots_full_logged = 1;
        plog("[PAL3UiMessageDiag] observer slots full; further windows ignored");
    }
}

static void observe_current_ui_messages(void)
{
    struct UIGameFrm *game = &g_gamefrm;
    observe_ui_message_window(&game->baseclass_0, "gamefrm.root");
    if (game->m_curfrm)
        observe_ui_message_window(&game->m_curfrm->baseclass, "current.frame");
    if (game->m_scenefrm) {
        observe_ui_message_window(&game->m_scenefrm->baseclass.baseclass,
            "scene.root");
        observe_ui_message_window(&game->m_scenefrm->m_face.baseclass,
            "scene.face");
    }
}

static void record_game_loop_diag(int type)
{
    if (type >= 0 && type < PAL3_GAMELOOP_DIAG_TYPE_COUNT)
        InterlockedIncrement(&game_loop_diag_counts[type]);
}

static void log_ui_loop_diag_if_due(void)
{
    DWORD now = GetTickCount();
    int state = PAL3_s_gamestate;
    int frame = g_gamefrm.m_curfrmid;
    int changed;
    int due;

    if (InterlockedCompareExchange(&ui_diag_log_lock, 1, 0) != 0) return;
    changed = !ui_diag_logged_once || state != ui_diag_last_game_state ||
        frame != ui_diag_last_frame_id;
    due = (DWORD)(now - ui_diag_last_log_tick) >= 1500;
    if (changed || due) {
        plog("[PAL3LoopDiag] tick=%lu state=%d frame=%d loops(normal/sleep/lost/movie/open/begin/end)=%ld/%ld/%ld/%ld/%ld/%ld/%ld preEndScene=%ld uiRender=%ld",
            (unsigned long)now, state, frame,
            InterlockedCompareExchange(&game_loop_diag_counts[GAMELOOP_NORMAL], 0, 0),
            InterlockedCompareExchange(&game_loop_diag_counts[GAMELOOP_SLEEP], 0, 0),
            InterlockedCompareExchange(&game_loop_diag_counts[GAMELOOP_DEVICELOST], 0, 0),
            InterlockedCompareExchange(&game_loop_diag_counts[GAMELOOP_MOVIE], 0, 0),
            InterlockedCompareExchange(&game_loop_diag_counts[GAMEEVENT_MOVIE_ATOPEN], 0, 0),
            InterlockedCompareExchange(&game_loop_diag_counts[GAMEEVENT_MOVIE_ATBEGIN], 0, 0),
            InterlockedCompareExchange(&game_loop_diag_counts[GAMEEVENT_MOVIE_ATEND], 0, 0),
            InterlockedCompareExchange(&ui_preendscene_diag_count, 0, 0),
            InterlockedCompareExchange(&ui_render_diag_count, 0, 0));
        ui_diag_last_log_tick = now;
        ui_diag_last_game_state = state;
        ui_diag_last_frame_id = frame;
        ui_diag_logged_once = 1;
    }
    InterlockedExchange(&ui_diag_log_lock, 0);
}

static void ui_message_diag_onendscene(void)
{
    InterlockedIncrement(&ui_preendscene_diag_count);
    log_scene_state_snapshot();
    observe_current_ui_messages();
#if defined(PAL3_MENU_TREE_DIAG) && PAL3_MENU_TREE_DIAG
    PAL3MenuNav_DiagnosticObserve();
#endif
    log_ui_loop_diag_if_due();
}
#endif

static void game_loop_hook(void *arg)
{
    struct game_loop_hook_data *data = (struct game_loop_hook_data *)arg;
    HWND game_window;
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    record_game_loop_diag(data ? (int)data->type : -1);
    log_ui_loop_diag_if_due();
#endif
    focused = game_has_focus();

    if (data && data->type != GAMELOOP_NORMAL) {
#if PAL3_KEYBOARD_GAMEPAD_DIAG
        if (!focused || (data->type != GAMELOOP_MOVIE &&
            data->type != GAMEEVENT_MOVIE_ATOPEN &&
            data->type != GAMEEVENT_MOVIE_ATBEGIN &&
            data->type != GAMEEVENT_MOVIE_ATEND))
            keyboard_gamepad_diag_disable("non-normal game loop");
#endif
        pad_connected = 0;
        reset_gamepad_action_edges();
        reset_menu_navigation();
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
        diagnostic_nav_without_controller = 0;
#endif
        release_mouse_buttons();
        update_game_cursor_override(0, 0);
        if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER) && data->type == GAMELOOP_MOVIE && focused && xinput_get_state) {
            struct gamepad_state movie_pad;
            int movie_connected = xinput_get_state(0, &movie_pad) == ERROR_SUCCESS;
#if PAL3_KEYBOARD_GAMEPAD_DIAG
            if (movie_connected)
                keyboard_gamepad_diag_disable("real XInput controller connected");
            else
                movie_connected = keyboard_gamepad_diag_sample_pad(&movie_pad);
#endif
            if (movie_connected) {
                if (movie_skip_should_post(1, 1, 1,
                        (movie_pad.gamepad.buttons & XINPUT_GAMEPAD_START) != 0)) {
                    game_window = foreground_game_window();
                    if (game_window) {
                        PostMessageA(game_window, WM_CHAR, VK_ESCAPE, 1 | (0x01L << 16));
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
                        plog("[PAL3MovieSkip] posted original Escape from Plus");
#endif
                    }
                }
            } else {
                movie_skip_should_post(1, 1, 0, 0);
            }
        } else {
            movie_skip_should_post(data && data->type == GAMELOOP_MOVIE, focused, 0, 0);
        }
        return;
    }

    movie_skip_should_post(0, focused, 0, 0);

    if (!focused) {
#if PAL3_KEYBOARD_GAMEPAD_DIAG
        keyboard_gamepad_diag_disable("game window lost focus");
#endif
    }

#if PAL3_KEYBOARD_GAMEPAD_DIAG
    pad_connected = focused && xinput_get_state &&
        xinput_get_state(0, &current_pad) == ERROR_SUCCESS;
    if (pad_connected) {
        keyboard_gamepad_diag_disable("real XInput controller connected");
    } else if (focused) {
        pad_connected = keyboard_gamepad_diag_sample_pad(&current_pad);
    }
    if (!pad_connected) {
        reset_gamepad_action_edges();
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
        if (!focused) {
            reset_menu_navigation();
            diagnostic_nav_without_controller = 0;
        } else {
            if (!diagnostic_nav_without_controller) reset_menu_navigation();
            diagnostic_nav_without_controller = 1;
            update_menu_navigation();
        }
#else
        reset_menu_navigation();
#endif
        previous_start_down = 0;
        menu_open_pending = 0;
        previous_speed_left = 0;
        previous_speed_right = 0;
        release_mouse_buttons();
        update_game_cursor_override(0, 0);
        return;
    }
#else
    if (!focused || !xinput_get_state) {
        pad_connected = 0;
        reset_gamepad_action_edges();
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
        if (focused && !diagnostic_nav_without_controller) {
            reset_menu_navigation();
            diagnostic_nav_without_controller = 1;
            update_menu_navigation();
        } else if (!focused) {
            reset_menu_navigation();
            diagnostic_nav_without_controller = 0;
        } else {
            update_menu_navigation();
        }
#else
        reset_menu_navigation();
#endif
        previous_start_down = 0;
        menu_open_pending = 0;
        previous_speed_left = 0;
        previous_speed_right = 0;
        release_mouse_buttons();
        update_game_cursor_override(0, 0);
        return;
    }

    pad_connected = xinput_get_state(0, &current_pad) == ERROR_SUCCESS;
    if (!pad_connected) {
        reset_gamepad_action_edges();
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
        if (!diagnostic_nav_without_controller) reset_menu_navigation();
        diagnostic_nav_without_controller = 1;
        update_menu_navigation();
#else
        reset_menu_navigation();
#endif
        previous_start_down = 0;
        menu_open_pending = 0;
        previous_speed_left = 0;
        previous_speed_right = 0;
        release_mouse_buttons();
        update_game_cursor_override(0, 0);
        return;
    }
#endif

    update_gamepad_action_edges();
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    diagnostic_nav_without_controller = 0;
#endif
    update_menu_navigation();

    /* 视角和金手指可单独读取手柄，但不能附带接管移动、菜单或鼠标。 */
    if (!PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) {
        if (PAL3Mod_Enabled(PAL3_MOD_CAMERA)) update_scene_input();
        return;
    }

#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    if (gamepad_confirm_edge || gamepad_cancel_edge)
        plog("[PAL3InputEdge] sample confirm=%d cancel=%d menu=0x%lx combat=0x%lx",
            gamepad_confirm_edge, gamepad_cancel_edge,
            (unsigned long)menu_nav_consumed, (unsigned long)combat_nav_consumed);
#endif

    {
        int start_down = button_down(XINPUT_GAMEPAD_START);
        int mouse_mode = scene_mouse_mode_active(pad_connected);
        int speed_left = mouse_mode && button_down(XINPUT_GAMEPAD_DPAD_LEFT);
        int speed_right = mouse_mode && button_down(XINPUT_GAMEPAD_DPAD_RIGHT);
        int speed_direction;
        if (start_down && !previous_start_down && PAL3_s_gamestate == GAME_SCENE)
            menu_open_pending = 1;
        previous_start_down = start_down;
        speed_direction = mouse_speed_adjustment_direction(mouse_mode,
            speed_left, speed_right, previous_speed_left, previous_speed_right);
        if (speed_direction) update_mouse_speed(speed_direction);
        previous_speed_left = speed_left;
        previous_speed_right = speed_right;
        update_game_cursor_override(1, mouse_mode);

        // 只有按住 Switch L 才把右摇杆交给鼠标；松开 L 就停用并释放鼠标键。
        if (mouse_mode && (axis_active(current_pad.gamepad.thumb_rx, XINPUT_GAMEPAD_RIGHT_DEADZONE) ||
            axis_active(current_pad.gamepad.thumb_ry, XINPUT_GAMEPAD_RIGHT_DEADZONE))) {
            POINT cursor;
            HWND game_window = GetForegroundWindow();
            RECT client;
            DWORD now = GetTickCount();
            DWORD elapsed = now - last_mouse_tick;
            float dt;
            float x;
            float y;
            float move_x;
            float move_y;
            int step_x;
            int step_y;
            if (elapsed == 0 || elapsed > 100) elapsed = 16;
            dt = (float)elapsed / 1000.0f;
            last_mouse_tick = now;
            x = (float)current_pad.gamepad.thumb_rx;
            y = (float)current_pad.gamepad.thumb_ry;
            if (x > XINPUT_GAMEPAD_RIGHT_DEADZONE)
                x = (x - XINPUT_GAMEPAD_RIGHT_DEADZONE) / (32767.0f - XINPUT_GAMEPAD_RIGHT_DEADZONE);
            else if (x < -XINPUT_GAMEPAD_RIGHT_DEADZONE)
                x = (x + XINPUT_GAMEPAD_RIGHT_DEADZONE) / (32768.0f - XINPUT_GAMEPAD_RIGHT_DEADZONE);
            else x = 0.0f;
            if (y > XINPUT_GAMEPAD_RIGHT_DEADZONE)
                y = (y - XINPUT_GAMEPAD_RIGHT_DEADZONE) / (32767.0f - XINPUT_GAMEPAD_RIGHT_DEADZONE);
            else if (y < -XINPUT_GAMEPAD_RIGHT_DEADZONE)
                y = (y + XINPUT_GAMEPAD_RIGHT_DEADZONE) / (32768.0f - XINPUT_GAMEPAD_RIGHT_DEADZONE);
            else y = 0.0f;
            move_x = mouse_x_remainder + x * mouse_speed * dt;
            move_y = mouse_y_remainder - y * mouse_speed * dt;
            step_x = (int)move_x;
            step_y = (int)move_y;
            mouse_x_remainder = move_x - (float)step_x;
            mouse_y_remainder = move_y - (float)step_y;
            if (GetCursorPos(&cursor) && game_client_screen_rect(game_window, &client)) {
                LONG target_x = cursor.x + step_x;
                LONG target_y = cursor.y + step_y;
                if (target_x < client.left) target_x = client.left;
                if (target_x > client.right) target_x = client.right;
                if (target_y < client.top) target_y = client.top;
                if (target_y > client.bottom) target_y = client.bottom;
                if (step_x || step_y) SetCursorPos(target_x, target_y);
            }
        } else {
            last_mouse_tick = GetTickCount();
            mouse_x_remainder = 0.0f;
            mouse_y_remainder = 0.0f;
        }

        {
            HWND game_window = GetForegroundWindow();
            int cursor_inside = cursor_inside_game_client(game_window);
            set_mouse_button(MOUSEEVENTF_LEFTDOWN, &mouse_left_down,
                mouse_button_wanted(mouse_mode && focused, pad_connected, cursor_inside, current_pad.gamepad.right_trigger));
            set_mouse_button(MOUSEEVENTF_RIGHTDOWN, &mouse_right_down,
                mouse_button_wanted(mouse_mode && focused, pad_connected, cursor_inside, current_pad.gamepad.left_trigger));
        }
    }
}

static void load_xinput(void)
{
    static const char *dll_names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", NULL};
    int i;
    for (i = 0; dll_names[i]; ++i) {
        xinput_module = LoadLibraryA(dll_names[i]);
        if (xinput_module) {
            xinput_get_state = (xinput_get_state_fn)GetProcAddress(xinput_module, "XInputGetState");
            if (xinput_get_state) return;
            FreeLibrary(xinput_module);
            xinput_module = NULL;
        }
    }
}

static void unload_xinput(void)
{
#if PAL3_KEYBOARD_GAMEPAD_DIAG
    keyboard_gamepad_diag_disable("plugin unloading");
#endif
    stop_modal_input();
    release_mouse_buttons();
    update_game_cursor_override(0, 0);
    if (xinput_module) {
        FreeLibrary(xinput_module);
        xinput_module = NULL;
        xinput_get_state = NULL;
    }
}

#ifndef PAL3_GAMEPAD_TEST
MAKE_PLUGINABOUT();
MAKE_PLUGINENTRY()
{
    PAL3_STRUCT_SELFCHECK();
    PAL3Mod_Initialize();
    load_xinput();
    if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) PAL3CombatNav_Initialize();
    if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER | PAL3_MOD_CHEATS)) PAL3MenuNav_Initialize();
    {
        uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER | PAL3_MOD_CAMERA)) {
        make_call((unsigned)(base + 0x0000572Eu), scene_control_input);
        make_call((unsigned)(base + 0x00005843u), scene_control_input);
        make_call((unsigned)(base + 0x0000591Fu), scene_control_input);
        }
        if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) {
        make_call((unsigned)(base + 0x0001E6A7u), scene_move);
        make_call((unsigned)(base + 0x0001D9CBu), scene_read_cursor);
        make_call((unsigned)(base + 0x0001DFFDu), scene_pick_jump_ground);
        make_call((unsigned)(base + 0x0002315Fu), scene_npc_direction_check);
        make_call((unsigned)(base + 0x0001E379u), scene_find_nearby_npc);
        }
        if (PAL3Mod_Enabled(PAL3_MOD_CAMERA)) {
        make_call((unsigned)(base + 0x00005CAFu), scene_camera_apply);
        make_call((unsigned)(base + 0x0000516Fu), scene_camera_preset);
        make_call((unsigned)(base + 0x0001BF78u), scene_camera_preset);
        make_call((unsigned)(base + 0x000234B5u), scene_camera_preset);
        make_call((unsigned)(base + 0x000234C3u), scene_camera_preset);
        make_call((unsigned)(base + 0x000356ECu), scene_camera_preset);
        make_call((unsigned)(base + 0x0000C650u), scene_party_render);
        /* 当前主角不在前面的队员循环绘制，在这处单独绘制，第一人称也必须跳过。 */
        make_call((unsigned)(base + 0x0000C6FDu), scene_party_render);
        }
        if (PAL3Mod_Enabled(PAL3_MOD_CHEATS)) {
        make_call((unsigned)(base + 0xF7B25u), combat_experience_count);
        make_call((unsigned)(base + 0xF7CFBu), combat_experience_count);
        make_call((unsigned)(base + 0xF7E1Fu), combat_experience_count);
        make_call((unsigned)(base + 0xF7EBDu), combat_experience_count);
        make_call((unsigned)(base + 0xEF8F9u), combat_skill_use);
        }
    }
    if (initialize_mouse_speed_config_path())
        plog("Mouse sensitivity config file: %s", mouse_speed_config_path);
    else
        plog("Mouse sensitivity config path unavailable; keeping default");
    load_mouse_speed();
    if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER | PAL3_MOD_CAMERA | PAL3_MOD_CHEATS)) {
        add_gameloop_hook(game_loop_hook);
        add_grpkbdstate_hook(keyboard_state_hook);
        add_prewndproc_hook(wndproc_hook);
    }
    if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) add_preendscene_hook(mouse_speed_onendscene);
    if (PAL3Mod_Enabled(PAL3_MOD_CONTROLLER | PAL3_MOD_CHEATS)) add_preendscene_hook(PAL3MenuNav_Render);
#if PAL3_GAMEPAD_NAV_DIAGNOSTIC
    add_preendscene_hook(ui_message_diag_onendscene);
#endif
    add_atexit_hook(unload_xinput);
    plog("PAL3Modules controller=%d camera=%d cheats=%d", PAL3Mod_Enabled(PAL3_MOD_CONTROLLER),
        PAL3Mod_Enabled(PAL3_MOD_CAMERA), PAL3Mod_Enabled(PAL3_MOD_CHEATS));
    plog("PAL3Gamepad=%s XInputGetState=%d", PLUGIN_VERSION,
        xinput_get_state ? 1 : 0);
    return 0;
}
#endif

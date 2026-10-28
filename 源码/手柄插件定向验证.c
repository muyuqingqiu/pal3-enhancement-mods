#define PAL3_GAMEPAD_TEST
#include "手柄插件.c"

static int failures;

static void expect(int condition, const char *name)
{
    if (condition) printf("通过：%s\n", name);
    else {
        printf("失败：%s\n", name);
        ++failures;
    }
}

static void test_combat_mouse_activation(void)
{
    pad_connected = 1;
    memset(&current_pad, 0, sizeof(current_pad));
    current_pad.gamepad.buttons = XINPUT_GAMEPAD_LEFT_SHOULDER;
    expect(!active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "战斗单按L切分类不显示鼠标");
    current_pad.gamepad.buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    expect(!active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "战斗同时按LR不显示鼠标");
    current_pad.gamepad.buttons = XINPUT_GAMEPAD_RIGHT_SHOULDER;
    expect(!active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "战斗单按R不显示鼠标");
    current_pad.gamepad.buttons = XINPUT_GAMEPAD_LEFT_SHOULDER;
    current_pad.gamepad.thumb_rx = XINPUT_GAMEPAD_RIGHT_DEADZONE;
    expect(!active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "右摇杆死区内不误触鼠标");
    current_pad.gamepad.thumb_rx++;
    expect(active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "战斗L配合右摇杆启用鼠标");
    current_pad.gamepad.thumb_rx = 0;
    expect(active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "摇杆回中但仍按L时保留鼠标便于瞄准点击");
    current_pad.gamepad.buttons = 0;
    expect(!active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "松开L后立即隐藏鼠标");
    current_pad.gamepad.buttons = XINPUT_GAMEPAD_LEFT_SHOULDER;
    expect(!active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "再次单按L不继承上次鼠标状态");
    current_pad.gamepad.right_trigger = 31;
    expect(active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "战斗L配合ZR仍可使用鼠标点击");
    expect(!active_mouse_mode(0, GAME_COMBAT, FRM_SCENE),
        "手柄断开时清空战斗鼠标状态");
    current_pad.gamepad.right_trigger = 0;
    expect(!active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "重连时单按L不继承鼠标状态");
    current_pad.gamepad.left_trigger = 31;
    expect(active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "战斗L配合ZL仍可使用鼠标点击");
    expect(!active_mouse_mode(1, GAME_UI, FRM_S_STATE),
        "进入系统菜单仍禁用鼠标");
    current_pad.gamepad.left_trigger = 0;
    expect(!active_mouse_mode(1, GAME_COMBAT, FRM_SCENE),
        "跨页面后不继承战斗鼠标状态");
    expect(active_mouse_mode(1, GAME_SCENE, FRM_SCENE),
        "场景中按L的鼠标操作保持原样");
    current_pad.gamepad.buttons = 0;
    expect(!active_mouse_mode(1, GAME_SCENE, FRM_SCENE),
        "场景松开L后鼠标关闭");
}

static void test_config_path(void)
{
    char path[MAX_PATH];
    char tiny[8];
    expect(build_mouse_speed_config_path("C:\\PAL3.exe", path, sizeof(path)) &&
        strcmp(path, "C:\\save\\PAL3Gamepad.ini") == 0, "盘符根目录配置路径");
    expect(build_mouse_speed_config_path("D:/games/PAL3/PAL3.exe", path, sizeof(path)) &&
        strcmp(path, "D:/games/PAL3/save\\PAL3Gamepad.ini") == 0, "正斜杠绝对路径");
    expect(!build_mouse_speed_config_path("PAL3.exe", path, sizeof(path)), "拒绝相对路径");
    expect(!build_mouse_speed_config_path("C:", path, sizeof(path)), "拒绝过短路径");
    expect(!build_mouse_speed_config_path("C:\\PAL3.exe", tiny, sizeof(tiny)), "拒绝输出缓冲区不足");
}

static void test_sensitivity_config(void)
{
    char temp_path[MAX_PATH];
    char config_path[MAX_PATH];
    char percent[4];
    int values[] = {50, 75, 100, 125, 150, 49, 151, 60};
    float expected[] = {450.0f, 675.0f, 900.0f, 1125.0f, 1350.0f,
        900.0f, 900.0f, 900.0f};
    size_t i;

    if (!GetTempPathA(sizeof(temp_path), temp_path) ||
        !GetTempFileNameA(temp_path, "p3g", 0, config_path)) {
        expect(0, "创建灵敏度临时配置文件");
        return;
    }
    strncpy(mouse_speed_config_path, config_path, sizeof(mouse_speed_config_path) - 1);
    mouse_speed_config_path[sizeof(mouse_speed_config_path) - 1] = '\0';
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        snprintf(percent, sizeof(percent), "%d", values[i]);
        WritePrivateProfileStringA(MOUSE_SPEED_CONFIG_SECTION,
            MOUSE_SPEED_CONFIG_KEY, percent, mouse_speed_config_path);
        expect(GetPrivateProfileIntA(MOUSE_SPEED_CONFIG_SECTION,
            MOUSE_SPEED_CONFIG_KEY, -1, mouse_speed_config_path) == values[i],
            "Windows 配置文件写入后回读");
        expect(mouse_speed_from_percent(values[i]) == expected[i],
            "灵敏度范围、步进和非法值回默认");
    }
    mouse_speed = 900.0f;
    update_mouse_speed(1);
    expect(mouse_speed == 1125.0f,
        "生产灵敏度调节函数增加一档到125%");
    expect(GetPrivateProfileIntA(MOUSE_SPEED_CONFIG_SECTION,
        MOUSE_SPEED_CONFIG_KEY, -1, mouse_speed_config_path) == 125,
        "生产灵敏度调节函数写入正确节和键");
    mouse_speed = 900.0f;
    load_mouse_speed();
    expect(mouse_speed == 1125.0f,
        "生产加载函数读取并恢复125%灵敏度");
    mouse_speed = MOUSE_SPEED_MAX;
    update_mouse_speed(1);
    mouse_speed = 900.0f;
    load_mouse_speed();
    expect(mouse_speed == MOUSE_SPEED_MIN, "最高档向右循环到最低档并保存");
    update_mouse_speed(-1);
    mouse_speed = 900.0f;
    load_mouse_speed();
    expect(mouse_speed == MOUSE_SPEED_MAX, "最低档向左循环到最高档并保存");
    DeleteFileA(config_path);
}

static void test_action_press_edges(void)
{
    gamepad_actions_armed = 0;
    current_pad.gamepad.buttons = XINPUT_GAMEPAD_B;
    pad_connected = 1;
    update_gamepad_action_edges();
    expect(!gamepad_confirm_edge, "首次按住不触发确认");
    update_gamepad_action_edges();
    expect(!gamepad_confirm_edge, "持续按住不重复触发确认");

    current_pad.gamepad.buttons = 0;
    update_gamepad_action_edges();
    current_pad.gamepad.buttons = XINPUT_GAMEPAD_B;
    update_gamepad_action_edges();
    expect(gamepad_confirm_edge, "松开后重新按下触发一次确认");
    update_gamepad_action_edges();
    expect(!gamepad_confirm_edge, "确认按住跨帧不重复触发");

    reset_gamepad_action_edges();
    current_pad.gamepad.buttons = XINPUT_GAMEPAD_B;
    update_gamepad_action_edges();
    expect(!gamepad_confirm_edge, "失焦或断连复位后须先松开");
}

static void test_modal_button_policy(void)
{
    expect(modal_affirmative_button_id(1, 1, 1, 1) == IDOK,
        "弹窗优先选择确定按钮");
    expect(modal_affirmative_button_id(0, 1, 1, 1) == IDYES,
        "没有确定时选择是按钮");
    expect(modal_affirmative_button_id(0, 0, 1, 1) == IDRETRY,
        "重试按钮可作为肯定操作");
    expect(modal_affirmative_button_id(0, 0, 0, 1) == IDCONTINUE,
        "继续按钮可作为肯定操作");
    expect(modal_affirmative_button_id(0, 0, 0, 0) == 0,
        "没有明确肯定按钮时不触发");
    expect(modal_cancel_button_id(1, 1) == IDCANCEL,
        "取消键优先选择取消按钮");
    expect(modal_cancel_button_id(0, 1) == IDNO,
        "没有取消按钮时只选择否按钮");
    expect(modal_cancel_button_id(0, 0) == 0,
        "没有否或取消按钮时不触发");
}

static void test_navigation_input_policies(void)
{
    struct UIWnd root;
    struct UIWnd parent;
    struct UIWnd dialog;
    void *root_children[1];
    void *parent_children[1];

    memset(&root, 0, sizeof(root));
    memset(&parent, 0, sizeof(parent));
    memset(&dialog, 0, sizeof(dialog));
    root.m_bcreateok = root.m_bvisible = root.m_benable = 1;
    parent.m_bcreateok = parent.m_bvisible = parent.m_benable = 1;
    dialog.m_bcreateok = dialog.m_bvisible = dialog.m_benable = 1;
    root_children[0] = &parent;
    parent_children[0] = &dialog;
    root.m_childs.m_pData = root_children;
    root.m_childs.m_nSize = 1;
    parent.m_childs.m_pData = parent_children;
    parent.m_childs.m_nSize = 1;
    parent.m_pfather = &root;
    dialog.m_pfather = &parent;
    expect(uiwnd_is_active_descendant(&root, &dialog),
        "可见且沿父子数组连接的选择框属于活动树");
    parent.m_bvisible = 0;
    expect(!uiwnd_is_active_descendant(&root, &dialog),
        "隐藏祖先会让选择框失去活动状态");
    parent.m_bvisible = 1;
    parent.m_childs.m_nSize = 0;
    expect(!uiwnd_is_active_descendant(&root, &dialog),
        "visible=1 但不在父窗口子项中的选择框不活动");
    parent.m_childs.m_nSize = 1;

    pad_connected = 1;
    current_pad.gamepad.buttons = XINPUT_GAMEPAD_LEFT_SHOULDER;
    current_pad.gamepad.thumb_rx = 0;
    current_pad.gamepad.thumb_ry = 0;
    current_pad.gamepad.right_trigger = 0;
    current_pad.gamepad.left_trigger = 0;
    expect(active_mouse_mode(1, GAME_SCENE, FRM_SCENE),
        "场景输入按当前采样的 L 停止方向和镜头控制");
    expect(!active_mouse_mode(0, GAME_SCENE, FRM_SCENE), "未连接时场景输入不进入鼠标模式");
    expect(!menu_mouse_mode_active(1, 1), "只按 L 不视为已经使用鼠标");

    expect(!active_mouse_mode(1, GAME_UI, FRM_S_STATE), "系统菜单禁用L鼠标模式");
    current_pad.gamepad.thumb_rx = XINPUT_GAMEPAD_RIGHT_DEADZONE + 1;
    expect(menu_mouse_mode_active(1, 1), "L 加右摇杆活动启用菜单鼠标模式");
    current_pad.gamepad.thumb_rx = 0;
    current_pad.gamepad.right_trigger = 31;
    expect(menu_mouse_mode_active(1, 1), "L 加 ZR 点击启用菜单鼠标模式");
    current_pad.gamepad.right_trigger = 0;
    current_pad.gamepad.left_trigger = 31;
    expect(menu_mouse_mode_active(1, 1), "L 加 ZL 点击启用菜单鼠标模式");
    current_pad.gamepad.left_trigger = 0;
    current_pad.gamepad.buttons = 0;
    expect(!menu_mouse_mode_active(1, 1), "松开 L 后不进入菜单鼠标模式");

    expect(gamepad_direction_injection_allowed(0), "普通模式保留控制器方向输入");
    expect(!gamepad_direction_injection_allowed(1),
        "L 按住时不通过原版回退路径注入控制器方向");
    expect(!scene_direction_fallback_allowed(1, 0),
        "GAME_SCENE 剧情锁定时不通过方向回退注入移动");
    expect(scene_direction_fallback_allowed(1,
            uiwnd_is_active_descendant(&root, &dialog)),
        "场景树中的文字选项框显示时保留方向导航");
    expect(scene_direction_fallback_allowed(0, 0),
        "菜单和战斗状态继续保留方向回退");
    expect((gamepad_pressed_edges(XINPUT_GAMEPAD_B, 0, 1, 0) &
        XINPUT_GAMEPAD_B) != 0, "普通模式保留 Switch A 确认边沿");
    expect((gamepad_pressed_edges(XINPUT_GAMEPAD_B, 0, 1, 1) &
        XINPUT_GAMEPAD_B) == 0, "L 按住时不确认旧焦点，鼠标点击由扳机处理");
}

static void test_modal_session_and_edges(void)
{
    HWND owner = (HWND)(uintptr_t)1;
    HWND dialog = (HWND)(uintptr_t)2;

    modal_owner_window = owner;
    modal_dialog_window = dialog;
    modal_timer_id = 7;
    expect(modal_session_matches(owner, dialog),
        "同一 owner 和 dialog 重复空闲消息保留现有 timer");
    expect(!modal_session_matches(owner, (HWND)(uintptr_t)3),
        "新 dialog 不复用旧 dialog 的 timer 状态");
    expect(modal_action_from_edges(1, 1) == MODAL_ACTION_CONFIRM,
        "A、B 同帧按下只选择一次确认操作");
    expect(modal_action_from_edges(0, 1) == MODAL_ACTION_CANCEL,
        "单独按下 B 仍选择取消操作");
    expect(modal_action_from_edges(0, 0) == MODAL_ACTION_NONE,
        "没有新按键边沿时不操作弹窗");
    modal_owner_window = NULL;
    modal_dialog_window = NULL;
    modal_timer_id = 0;
}

static void test_navigation_consumption(void)
{
    expect(should_inject_gamepad_cancel(1, 0, 0),
        "未消费的 Switch B 继续走原版取消路径");
    expect(!should_inject_gamepad_cancel(1, PAL3_MENU_NAV_CANCEL, 0),
        "菜单模块已消费取消时不再重复注入 Escape");
    expect(!should_inject_gamepad_cancel(1, 0, PAL3_MENU_NAV_CANCEL),
        "战斗模块已消费取消时不再重复注入 Escape");
    expect(!should_inject_gamepad_cancel(0, 0, 0),
        "没有取消边沿时不注入 Escape");
}

static void test_direction_injection_policy(void)
{
    expect(pal3_gamepad_should_inject_direction(0, 0,
        PAL3_MENU_NAV_UP, 1), "未消费的上方向继续传给原版");
    expect(!pal3_gamepad_should_inject_direction(PAL3_MENU_NAV_UP, 0,
        PAL3_MENU_NAV_UP, 1), "菜单已消费的方向不再注入");
    expect(!pal3_gamepad_should_inject_direction(0, PAL3_MENU_NAV_UP,
        PAL3_MENU_NAV_UP, 1), "战斗已消费的方向不被菜单分支重新注入");
    expect(!pal3_gamepad_should_inject_direction(PAL3_MENU_NAV_DOWN,
        PAL3_MENU_NAV_DOWN, PAL3_MENU_NAV_DOWN, 1),
        "菜单和战斗都消费时不注入");
    expect(pal3_gamepad_should_inject_direction(PAL3_MENU_NAV_LEFT, 0,
        PAL3_MENU_NAV_RIGHT, 1), "消费其他方向不影响右方向");
    expect(!pal3_gamepad_should_inject_direction(0, 0,
        PAL3_MENU_NAV_LEFT, 0), "未按下的方向不注入");
}

#if PAL3_KEYBOARD_GAMEPAD_DIAG
static void test_keyboard_gamepad_provider(void)
{
    struct pal3_keyboard_gamepad_state state;
    struct gamepad_state pad;
    DWORD buttons = 0;

    expect(keyboard_gamepad_diag_action(VK_F1, 0) ==
        XINPUT_GAMEPAD_DPAD_UP, "诊断 F1 映射为十字上");
    expect(keyboard_gamepad_diag_action(VK_F4, 0) ==
        XINPUT_GAMEPAD_DPAD_RIGHT, "诊断 F4 映射为十字右");
    expect(keyboard_gamepad_diag_action(VK_F5, 0) ==
        XINPUT_GAMEPAD_B, "诊断 F5 映射为 Switch A");
    expect(keyboard_gamepad_diag_action(VK_F6, 0) ==
        XINPUT_GAMEPAD_A, "诊断 F6 映射为 Switch B");
    expect(keyboard_gamepad_diag_action(VK_F7, 0) ==
        XINPUT_GAMEPAD_START, "诊断 F7 映射为 Plus");
    expect(keyboard_gamepad_diag_action(VK_F9, 0) ==
        XINPUT_GAMEPAD_LEFT_SHOULDER, "诊断 F9 映射为 L");
    expect(keyboard_gamepad_diag_action(VK_F10, 0) ==
        XINPUT_GAMEPAD_RIGHT_SHOULDER, "诊断 F10 映射为 R");
    expect(keyboard_gamepad_diag_action(VK_F3, 1) ==
        (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_DPAD_LEFT),
        "诊断 Ctrl+F3 映射为 L 加十字左");
    expect(keyboard_gamepad_diag_action(VK_F4, 1) ==
        (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_DPAD_RIGHT),
        "诊断 Ctrl+F4 映射为 L 加十字右");
    expect(keyboard_gamepad_diag_action(VK_F8, 0) == XINPUT_GAMEPAD_Y &&
        keyboard_gamepad_diag_action(VK_F11, 0) == XINPUT_GAMEPAD_X,
        "F8 和 F11 对应 Switch X 和 Y 切换角色");
    expect(keyboard_gamepad_diag_is_driver_key(VK_F1) &&
        keyboard_gamepad_diag_is_driver_key(VK_F12) &&
        keyboard_gamepad_diag_is_driver_key(VK_F8) &&
        keyboard_gamepad_diag_is_driver_key(VK_F11) &&
        !keyboard_gamepad_diag_is_driver_key(VK_ESCAPE),
        "诊断功能键都被过滤，其他键保持原样");

    keyboard_gamepad_diag_begin(&state);
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0 && state.warmup_frames == 1,
        "模拟设备启用后第一个样本保持中立");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0 && state.warmup_frames == 0,
        "模拟设备启用后第二个样本保持中立");

    expect(keyboard_gamepad_diag_queue(&state, XINPUT_GAMEPAD_B),
        "空闲时接受一条诊断按键");
    expect(!keyboard_gamepad_diag_queue(&state, XINPUT_GAMEPAD_A),
        "按键序列繁忙时拒绝新动作");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0, "动作触发前第一个中立样本");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0, "动作触发前第二个中立样本");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == XINPUT_GAMEPAD_B,
        "按下阶段只输出一个游戏输入样本");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0, "动作后第一个中立样本");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0 && state.phase == PAL3_KEYBOARD_GAMEPAD_IDLE,
        "动作后第二个中立样本结束序列");

    expect(keyboard_gamepad_diag_queue(&state, XINPUT_GAMEPAD_DPAD_UP),
        "空闲时接受方向诊断按键");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0, "方向动作前第一个中立样本");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0, "方向动作前第二个中立样本");
    {
        int press_samples = 0;
        int i;
        for (i = 0; i < 8; ++i) {
            keyboard_gamepad_diag_sample(&state, &buttons);
            if (buttons == XINPUT_GAMEPAD_DPAD_UP) ++press_samples;
        }
        expect(press_samples == 8,
            "方向动作连续输出八个游戏输入样本");
        expect(state.phase == PAL3_KEYBOARD_GAMEPAD_NEUTRAL_AFTER &&
            state.remaining_frames == 2,
            "八个方向样本后进入松开阶段");
    }
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0, "方向动作后第一个中立样本");
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0 && state.phase == PAL3_KEYBOARD_GAMEPAD_IDLE,
        "方向动作后第二个中立样本结束序列");

    expect(keyboard_gamepad_diag_queue(&state,
        XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_DPAD_LEFT),
        "空闲时接受肩键加方向组合");
    keyboard_gamepad_diag_sample(&state, &buttons);
    keyboard_gamepad_diag_sample(&state, &buttons);
    expect(buttons == 0, "组合动作前仍保留两个中立样本");
    {
        int press_samples = 0;
        int i;
        for (i = 0; i < 8; ++i) {
            keyboard_gamepad_diag_sample(&state, &buttons);
            if (buttons == (XINPUT_GAMEPAD_LEFT_SHOULDER |
                    XINPUT_GAMEPAD_DPAD_LEFT)) ++press_samples;
        }
        expect(press_samples == 8,
            "肩键加方向组合也连续输出八个游戏输入样本");
    }

    {
        const unsigned int keys[] = {VK_F1, VK_F2, VK_F5, VK_F6,
            VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F2, VK_F8};
        const SHORT axes[][4] = {{-24000,0,0,0}, {24000,0,0,0},
            {0,24000,0,0}, {0,-24000,0,0}, {0,0,-24000,0},
            {0,0,24000,0}, {0,0,0,24000}, {0,0,0,-24000},
            {0,0,24000,0}, {24000,0,0,0}, {0,0,24000,0}};
        size_t k;
        for (k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k) {
            DWORD action = keyboard_gamepad_diag_action(keys[k], 1);
            WORD expected_buttons = keys[k] == VK_F11 ?
                XINPUT_GAMEPAD_LEFT_SHOULDER : 0;
            int i;
            if (k >= 9) {
                action |= XINPUT_GAMEPAD_B;
                expected_buttons = XINPUT_GAMEPAD_B;
            }
            keyboard_gamepad_diag_begin(&state);
            keyboard_gamepad_diag_sample(&state, &buttons);
            keyboard_gamepad_diag_sample(&state, &buttons);
            expect(keyboard_gamepad_diag_queue(&state, action),
                "诊断轴高位可进入输入队列");
            for (i = 0; i < 36; ++i) {
                int pressing = i >= 2 && i < 34;
                keyboard_gamepad_diag_sample(&state, &buttons);
                keyboard_gamepad_diag_decode(buttons, &pad);
                expect(pad.gamepad.buttons == (pressing ? expected_buttons : 0) &&
                    pad.gamepad.thumb_lx == (pressing ? axes[k][0] : 0) &&
                    pad.gamepad.thumb_ly == (pressing ? axes[k][1] : 0) &&
                    pad.gamepad.thumb_rx == (pressing ? axes[k][2] : 0) &&
                    pad.gamepad.thumb_ry == (pressing ? axes[k][3] : 0),
                    "轴动作保持三十二个样本，前后中立无残留按钮和轴");
            }
            expect(state.phase == PAL3_KEYBOARD_GAMEPAD_IDLE && !state.action,
                "诊断轴序列结束后清空动作");
        }
    }
}
#endif

static void test_mouse_speed_modifier(void)
{
    expect(mouse_speed_adjustment_direction(1, 1, 0, 0, 0) == -1,
        "按住 L 加左键只调低灵敏度一档");
    expect(mouse_speed_adjustment_direction(1, 0, 1, 0, 0) == 1,
        "按住 L 加右键只调高灵敏度一档");
    expect(mouse_speed_adjustment_direction(1, 1, 0, 1, 0) == 0,
        "长按灵敏度组合不重复调节");
    expect(mouse_speed_adjustment_direction(1, 1, 1, 0, 0) == 0,
        "同时按左右不调节灵敏度");
    expect(mouse_speed_adjustment_direction(0, 1, 0, 0, 0) == 0,
        "未按 L 时方向键不调灵敏度");
}

int main(void)
{
#if defined(PAL3_COMBAT_MOUSE_TEST)
    test_combat_mouse_activation();
#else
    test_config_path();
    test_sensitivity_config();
    test_action_press_edges();
    test_modal_button_policy();
    test_navigation_input_policies();
    test_modal_session_and_edges();
    test_navigation_consumption();
    test_direction_injection_policy();
#if PAL3_KEYBOARD_GAMEPAD_DIAG
    test_keyboard_gamepad_provider();
#endif
    test_mouse_speed_modifier();
#endif
    return failures ? 1 : 0;
}

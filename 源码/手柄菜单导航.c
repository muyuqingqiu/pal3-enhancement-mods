#define USE_PAL3_DEFINITIONS
#include "PAL3patch.h"
#include "手柄菜单导航.h"
#include "模组开关.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PAL3_MENU_NAV_BUTTON_VTABLE_RVA 0x0016B018u
#define PAL3_MENU_NAV_CATEGORY_BUTTON_VTABLE_RVA 0x0016AC70u
#define PAL3_MENU_NAV_LISTBOX_VTABLE_RVA 0x0016AD40u
#define PAL3_MENU_NAV_BUTTON_MESSAGE 0x3E9u
#define PAL3_MENU_NAV_CATEGORY_HOVER_MESSAGE 0x3EAu
#define PAL3_MENU_NAV_CATEGORY_MOUSE_DOWN_MESSAGE 0x3FCu
#define PAL3_MENU_NAV_CATEGORY_ID_FIRST 0x00A00102u
#define PAL3_MENU_NAV_CATEGORY_COUNT 8u
#define PAL3_MENU_NAV_STATE_PAGE_INDEX_OFFSET 0x2AFD4u
#define PAL3_MENU_NAV_STATE_SET_PAGE_RVA 0x000A5010u
#define PAL3_MENU_NAV_STATE_CLOSE_BUTTON_ID 10486032u
#define PAL3_MENU_NAV_COVER_ACTIVE_PAGE_OFFSET 0x3628u
#define PAL3_MENU_NAV_COVER_STATE_OFFSET 0x362Cu
#define PAL3_MENU_NAV_COVER_SUBPAGE_STATE 4u
#define PAL3_MENU_NAV_LOAD_KEY_HANDLER_RVA 0x000562C0u
#define PAL3_MENU_NAV_LIST_KEY_HANDLER_RVA 0x0003F080u
#define PAL3_MENU_NAV_MAX_BUTTONS 64
#define PAL3_MENU_NAV_MAX_CATEGORIES 16
/* 原版装备页有 320 个子控件，系统页有 825 个。 */
#define PAL3_MENU_NAV_MAX_CHILDREN 1024
#define PAL3_MENU_NAV_MAX_DEPTH 10
#define PAL3_MENU_NAV_DEADZONE 7849
#define PAL3_MENU_NAV_INITIAL_REPEAT_MS 360u
#define PAL3_MENU_NAV_REPEAT_MS 140u

#ifndef PAL3_MENU_TREE_DIAG
#define PAL3_MENU_TREE_DIAG 0
#endif

#if PAL3_MENU_TREE_DIAG
#define PAL3_MENU_TREE_DIAG_MAX_CHILDREN 1024
#define PAL3_MENU_TREE_DIAG_MAX_DEPTH 10
#define PAL3_MENU_TREE_DIAG_TAB 0x0Fu
#endif

#define XINPUT_DPAD_UP        0x0001u
#define XINPUT_DPAD_DOWN      0x0002u
#define XINPUT_DPAD_LEFT      0x0004u
#define XINPUT_DPAD_RIGHT     0x0008u
#define XINPUT_LEFT_SHOULDER  0x0100u
#define XINPUT_RIGHT_SHOULDER 0x0200u
#define XINPUT_A              0x1000u
#define XINPUT_B              0x2000u
#define XINPUT_X              0x4000u
#define XINPUT_Y              0x8000u

struct menu_button {
    struct UIButton *button;
    struct PAL3MenuNavRect rect;
    int32_t center_x;
    int32_t center_y;
};

struct category_button {
    struct UIWnd *window;
    uint32_t index;
    int32_t center_x;
    uint8_t enabled;
};

static struct UIFrameWnd *s_frame;
static struct UIButton *s_selected;
static uint8_t s_selection_engaged;
static uint8_t s_previous_directions;
static uint8_t s_mouse_mode;
static uint32_t s_repeat_at[4];
static struct UIListBox *s_scroll_list;
static int s_scroll_direction;
static uint32_t s_scroll_repeat_at;
static struct UIWnd *s_archive_scroll_root;
static int s_archive_scroll_direction;
static uint32_t s_archive_scroll_at;
static struct UIWnd *s_system_root;
static int s_system_focus;
static int s_cheat_open;
static int s_cheat_row;
static int s_cheat_close_frames;
static uintptr_t s_system_update;
static struct UIFrameWnd *s_category_frame;
static uint8_t s_category_previous_left;
static uint8_t s_category_left_used;
static uint8_t s_category_left_directions;
static struct UIWnd *s_state_lineup;
static void *s_state_lineup_focus;
static void *s_state_lineup_click;
static int s_state_lineup_override;
static struct UIWnd *s_state_target_root;
static struct UIWnd *active_cover_subpage(void);
static uint32_t cover_subpage_state(void);
static MAKE_THISCALL(int, system_page_update, struct UIWnd *window,
    float delta, int have_input)
{
    /* 原版系统页不检查 have_input 就处理鼠标和键盘，覆盖页打开时须停止整页更新。 */
    if (s_cheat_open || s_cheat_close_frames) {
        if (!s_cheat_open && s_cheat_close_frames) --s_cheat_close_frames;
        return 1;
    }
    return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(s_system_update,
        int, struct UIWnd *, float, int), window, delta, have_input);
}
#if PAL3_MENU_TREE_DIAG
static uint8_t s_trace_menu_edge;
#endif

#if PAL3_MENU_TREE_DIAG
#define PAL3_MENU_EDGE_LOG(...) do { if (s_trace_menu_edge) plog(__VA_ARGS__); } while (0)
#else
#define PAL3_MENU_EDGE_LOG(...) ((void)0)
#endif

#if PAL3_MENU_TREE_DIAG
static uint8_t s_diag_tab_held;
static uint8_t s_diag_pending;
static uint8_t s_diag_pending_reasons;
static uint32_t s_diag_snapshot_id;
static uint32_t s_diag_node_count;
static uint8_t s_diag_was_in_ui;
static uint8_t s_diag_signature_valid;
static struct UIFrameWnd *s_diag_frame;
static enum PAL3_FRAME s_diag_frame_id;
static uint32_t s_diag_signature;
static struct UIWnd *s_diag_list_in_tree;
#endif

#if PAL3_MENU_TREE_DIAG
static unsigned long menu_diag_vtable_rva(const struct UIWnd *window)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t vtable = window ? (uintptr_t)window->vfptr : 0;
    return vtable >= base ? (unsigned long)(vtable - base) : 0xFFFFFFFFUL;
}

static void hash_diag_u32(uint32_t *hash, uint32_t value)
{
    unsigned int i;
    for (i = 0; i < 4; ++i) {
        *hash ^= value & 0xFFu;
        *hash *= 16777619u;
        value >>= 8;
    }
}

static void fingerprint_menu_tree_node(struct UIWnd *window,
    int ancestors_visible, unsigned int depth, uint32_t *hash)
{
    struct PtrArray *children;
    int visible;
    int i;

    if (!window || depth > PAL3_MENU_TREE_DIAG_MAX_DEPTH ||
        s_diag_node_count >= PAL3_MENU_TREE_DIAG_MAX_CHILDREN) return;
    visible = ancestors_visible && window->m_bcreateok && window->m_bvisible;
    if (!visible) return;

    ++s_diag_node_count;
    hash_diag_u32(hash, window->m_id);
    hash_diag_u32(hash, (uint32_t)menu_diag_vtable_rva(window));
    hash_diag_u32(hash, window->m_pfather ? window->m_pfather->m_id : 0);
    hash_diag_u32(hash, (uint32_t)!!window->m_bcreateok);
    hash_diag_u32(hash, (uint32_t)!!window->m_bvisible);
    hash_diag_u32(hash, (uint32_t)!!window->m_benable);
    if (menu_diag_vtable_rva(window) == PAL3_MENU_NAV_BUTTON_VTABLE_RVA) {
        const struct UIButton *button = (const struct UIButton *)window;
        /* 仅记录公开的输入锁字段；hover 和鼠标状态会逐帧变化，不能触发快照。 */
        hash_diag_u32(hash, (uint32_t)!!button->m_bLockUpdate);
        hash_diag_u32(hash, (uint32_t)!!button->m_canctrl);
    }
    if (menu_diag_vtable_rva(window) == PAL3_MENU_NAV_LISTBOX_VTABLE_RVA) {
        const struct UIListBox *list = (const struct UIListBox *)window;
        hash_diag_u32(hash, (uint32_t)!!list->m_hasKeyCtrl);
        hash_diag_u32(hash, (uint32_t)!!window->m_bfocus);
        hash_diag_u32(hash, (uint32_t)list->m_firstshow);
        hash_diag_u32(hash, (uint32_t)list->m_onsel);
        hash_diag_u32(hash, (uint32_t)list->m_locksel);
    }

    children = &window->m_childs;
    if (!children->m_pData || children->m_nSize <= 0 ||
        children->m_nSize > PAL3_MENU_TREE_DIAG_MAX_CHILDREN) return;
    for (i = 0; i < children->m_nSize; ++i) {
        struct UIWnd *child = (struct UIWnd *)children->m_pData[i];
        if (child && child->m_pfather == window)
            fingerprint_menu_tree_node(child, visible, depth + 1, hash);
    }
}

static uint32_t fingerprint_menu_tree(void)
{
    uint32_t hash = 2166136261u;
    struct UIWnd *current = g_gamefrm.m_curfrm ?
        &g_gamefrm.m_curfrm->baseclass : NULL;
    s_diag_node_count = 0;
    hash_diag_u32(&hash, 0x4746524Du); /* GFRM */
    fingerprint_menu_tree_node(&g_gamefrm.baseclass_0, 1, 0, &hash);
    if (current && current != &g_gamefrm.baseclass_0) {
        hash_diag_u32(&hash, 0x43555246u); /* CURF */
        fingerprint_menu_tree_node(current, 1, 0, &hash);
    }
    /* 不把焦点与矩形写入指纹，避免动画或光标移动造成重复快照。 */
    hash_diag_u32(&hash, s_diag_node_count);
    return hash;
}

static void dump_menu_tree_node(struct UIWnd *window, int ancestors_visible,
    unsigned int depth, const char *root_name)
{
    struct PtrArray *children;
    int available;
    int i;

    if (!window || depth > PAL3_MENU_TREE_DIAG_MAX_DEPTH ||
        s_diag_node_count >= PAL3_MENU_TREE_DIAG_MAX_CHILDREN) return;
    available = ancestors_visible && window->m_bcreateok && window->m_bvisible;
    if (!available) return;

    ++s_diag_node_count;
    if (window == &g_gamefrm.m_seldlg.m_list.baseclass)
        s_diag_list_in_tree = window;
    plog("[PAL3MenuTreeDiag] snap=%lu root=%s depth=%u id=%lu vtable_rva=%08lX parent_id=%lu rect=(%ld,%ld,%ld,%ld) create=%d visible=%d enable=%d",
        (unsigned long)s_diag_snapshot_id, root_name, depth,
        (unsigned long)window->m_id, menu_diag_vtable_rva(window),
        (unsigned long)(window->m_pfather ? window->m_pfather->m_id : 0),
        window->m_rect.left, window->m_rect.top,
        window->m_rect.right, window->m_rect.bottom,
        window->m_bcreateok, window->m_bvisible, window->m_benable);
    if (menu_diag_vtable_rva(window) == PAL3_MENU_NAV_BUTTON_VTABLE_RVA) {
        const struct UIButton *button = (const struct UIButton *)window;
        plog("[PAL3MenuTreeDiag] snap=%lu button id=%lu canctrl=%d lock_update=%d",
            (unsigned long)s_diag_snapshot_id, (unsigned long)window->m_id,
            button->m_canctrl, button->m_bLockUpdate);
    }
    if (menu_diag_vtable_rva(window) ==
            PAL3_MENU_NAV_CATEGORY_BUTTON_VTABLE_RVA) {
        plog("[PAL3MenuTreeDiag] snap=%lu category id=%lu enable=%d rect=(%ld,%ld,%ld,%ld)",
            (unsigned long)s_diag_snapshot_id, (unsigned long)window->m_id,
            window->m_benable,
            window->m_rect.left, window->m_rect.top,
            window->m_rect.right, window->m_rect.bottom);
    }
    if (menu_diag_vtable_rva(window) == PAL3_MENU_NAV_LISTBOX_VTABLE_RVA) {
        const struct UIListBox *list = (const struct UIListBox *)window;
        int item_count = 0;
        if (list->m_numcol > 0 && list->m_numcol <= 8 &&
            list->m_column[0].items.m_nSize >= 0 &&
            list->m_column[0].items.m_nSize <= 4096)
            item_count = list->m_column[0].items.m_nSize;
        plog("[PAL3MenuTreeDiag] snap=%lu listbox id=%lu keyctrl=%d focus=%d columns=%d items=%d firstshow=%d onsel=%d locksel=%d movesel=%d scroll=(%d,%d,%d,%d,%d)",
            (unsigned long)s_diag_snapshot_id, (unsigned long)window->m_id,
            list->m_hasKeyCtrl, window->m_bfocus, list->m_numcol,
            item_count, list->m_firstshow, list->m_onsel, list->m_locksel,
            list->m_movesel, list->m_scrollbar.m_min,
            list->m_scrollbar.m_max, list->m_scrollbar.m_pos,
            list->m_scrollbar.m_step, list->m_scrollbar.m_pagesize);
    }

    children = &window->m_childs;
    if (!children->m_pData || children->m_nSize <= 0 ||
        children->m_nSize > PAL3_MENU_TREE_DIAG_MAX_CHILDREN) return;
    for (i = 0; i < children->m_nSize; ++i) {
        struct UIWnd *child = (struct UIWnd *)children->m_pData[i];
        if (child && child->m_pfather == window)
            dump_menu_tree_node(child, available, depth + 1, root_name);
    }
}

static void dump_menu_tree(uint8_t reasons)
{
    struct UIWnd *current = g_gamefrm.m_curfrm ?
        &g_gamefrm.m_curfrm->baseclass : NULL;
    struct UIListBox *list = &g_gamefrm.m_seldlg.m_list;
    s_diag_list_in_tree = NULL;
    s_diag_node_count = 0;
    ++s_diag_snapshot_id;
    plog("[PAL3MenuTreeDiag] snap=%lu reasons=0x%02X game_state=%u frame_id=%u",
        (unsigned long)s_diag_snapshot_id, reasons,
        (unsigned int)PAL3_s_gamestate, (unsigned int)g_gamefrm.m_curfrmid);

    dump_menu_tree_node(&g_gamefrm.baseclass_0, 1, 0, "gamefrm");
    if (current && current != &g_gamefrm.baseclass_0)
        dump_menu_tree_node(current, 1, 0, "curfrm");

    plog("[PAL3MenuTreeDiag] snap=%lu seldlg_list vtable_rva=%08lX parent_id=%lu rect=(%ld,%ld,%ld,%ld) create=%d visible=%d enable=%d in_tree=%d keyctrl=%d columns=%d itemheight=%d fontsize=%d firstshow=%d onsel=%d locksel=%d",
        (unsigned long)s_diag_snapshot_id,
        menu_diag_vtable_rva(&list->baseclass),
        (unsigned long)(list->baseclass.m_pfather ? list->baseclass.m_pfather->m_id : 0),
        list->baseclass.m_rect.left, list->baseclass.m_rect.top,
        list->baseclass.m_rect.right, list->baseclass.m_rect.bottom,
        list->baseclass.m_bcreateok, list->baseclass.m_bvisible,
        list->baseclass.m_benable, s_diag_list_in_tree != NULL,
        list->m_hasKeyCtrl, list->m_numcol, list->m_itemheight,
        list->m_fontsize, list->m_firstshow, list->m_onsel, list->m_locksel);
}

static void menu_tree_diag_observe(void)
{
    int ui_active = PAL3_s_gamestate == GAME_UI && g_gamefrm.m_curfrm != NULL;
    int tab_held = (g_input.m_keyRaw[PAL3_MENU_TREE_DIAG_TAB] & 0x80) != 0;
    uint32_t signature = 0;

    if (s_diag_pending) {
        if (ui_active) dump_menu_tree(s_diag_pending_reasons);
        s_diag_pending = 0;
        s_diag_pending_reasons = 0;
    }

    if (ui_active) {
        signature = fingerprint_menu_tree();
        if (!s_diag_was_in_ui || s_diag_frame != g_gamefrm.m_curfrm ||
            s_diag_frame_id != g_gamefrm.m_curfrmid) {
            s_diag_pending = 1;
            s_diag_pending_reasons |= 0x01;
        }
        if (s_diag_signature_valid && signature != s_diag_signature) {
            s_diag_pending = 1;
            s_diag_pending_reasons |= 0x04;
        }
        s_diag_frame = g_gamefrm.m_curfrm;
        s_diag_frame_id = g_gamefrm.m_curfrmid;
        s_diag_signature = signature;
        s_diag_signature_valid = 1;
        s_diag_was_in_ui = 1;
    } else {
        s_diag_frame = NULL;
        s_diag_signature_valid = 0;
        s_diag_was_in_ui = 0;
    }
    if (tab_held && !s_diag_tab_held) {
        s_diag_pending = 1;
        s_diag_pending_reasons |= 0x02;
    }
    s_diag_tab_held = (uint8_t)tab_held;
}
#endif

void PAL3MenuNav_DiagnosticObserve(void)
{
#if PAL3_MENU_TREE_DIAG
    menu_tree_diag_observe();
#endif
}

static int time_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

uint32_t PAL3MenuNav_CategoryPageIndex(uint32_t category_id)
{
    /* 原版标签 ID 的顺序与 SetPage 页面编号不同，按 StateUI::OnMessage 映射。 */
    static const uint8_t page_indexes[PAL3_MENU_NAV_CATEGORY_COUNT] = {
        0, 3, 2, 1, 5, 6, 4, 7
    };
    uint32_t index;

    if (category_id < PAL3_MENU_NAV_CATEGORY_ID_FIRST ||
        category_id >= PAL3_MENU_NAV_CATEGORY_ID_FIRST +
            PAL3_MENU_NAV_CATEGORY_COUNT)
        return PAL3_MENU_NAV_CATEGORY_COUNT;
    index = category_id - PAL3_MENU_NAV_CATEGORY_ID_FIRST;
    return page_indexes[index];
}

int32_t PAL3MenuNav_FindCategoryForPage(const uint32_t *page_indexes,
    uint32_t count, uint32_t current_page)
{
    uint32_t i;
    if (!page_indexes) return -1;
    for (i = 0; i < count; ++i)
        if (page_indexes[i] == current_page) return (int32_t)i;
    return -1;
}

int PAL3MenuNav_ShouldReturnToStateRoot(uint32_t page_index)
{
    return page_index > 0 && page_index < PAL3_MENU_NAV_CATEGORY_COUNT;
}

enum PAL3MenuNavDialogChoice PAL3MenuNav_SelectDialogChoice(
    uint32_t dialog_type, int physical_a, int yes_available, int no_available)
{
    if (dialog_type == PAL3_MENU_NAV_DIALOG_TYPE_YESNO) {
        if (physical_a && yes_available) return PAL3_MENU_NAV_DIALOG_YES;
        if (!physical_a && no_available) return PAL3_MENU_NAV_DIALOG_NO;
    } else if ((dialog_type == PAL3_MENU_NAV_DIALOG_TYPE_YES ||
            dialog_type == PAL3_MENU_NAV_DIALOG_TYPE_MSGBOX) &&
        physical_a && yes_available) {
        /* DLG_MSGBOX 的原版确认按钮占用 mb_Yes；不把隐藏 mb_No 当成取消。 */
        return PAL3_MENU_NAV_DIALOG_YES;
    } else if (dialog_type == PAL3_MENU_NAV_DIALOG_TYPE_NO &&
        !physical_a && no_available) {
        return PAL3_MENU_NAV_DIALOG_NO;
    }
    return PAL3_MENU_NAV_DIALOG_NONE;
}

uint32_t PAL3MenuNav_SelectNeighbor(const struct PAL3MenuNavRect *rects,
    uint32_t count, uint32_t from, int32_t dx, int32_t dy)
{
    int64_t cx;
    int64_t cy;
    int64_t best_score = INT64_MAX;
    uint32_t best = from;
    uint32_t i;

    if (!rects || from >= count || (!dx && !dy)) return from;
    cx = ((int64_t)rects[from].left + rects[from].right) / 2;
    cy = ((int64_t)rects[from].top + rects[from].bottom) / 2;
    for (i = 0; i < count; ++i) {
        int64_t nx;
        int64_t ny;
        int64_t primary;
        int64_t secondary;
        int64_t score;

        if (i == from) continue;
        nx = ((int64_t)rects[i].left + rects[i].right) / 2;
        ny = ((int64_t)rects[i].top + rects[i].bottom) / 2;
        primary = dx ? (nx - cx) * dx : (ny - cy) * dy;
        if (primary <= 0) continue;
        secondary = dx ? ny - cy : nx - cx;
        if (secondary < 0) secondary = -secondary;
        /* 优先选择同一方向且横向偏离较小的控件，避免向右却跳到右下角。 */
        score = primary * primary + secondary * secondary * 4;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

uint32_t PAL3MenuNav_NextCategory(const uint8_t *enabled, uint32_t count,
    uint32_t current, int32_t direction)
{
    uint32_t next;

    if (!enabled || count == 0 || current >= count || direction == 0)
        return count;
    if (direction > 0) {
        for (next = current + 1; next < count; ++next)
            if (enabled[next]) return next;
    } else {
        next = current;
        while (next > 0) {
            --next;
            if (enabled[next]) return next;
        }
    }
    return count;
}

static int is_state_ai_choice(struct UIWnd *window);

static int is_plain_button(struct UIWnd *window)
{
    struct UIChatRest *rest = &g_gamefrm.m_ChatRest;
    /* 这三个字段在头文件中写作 UIButton，现场虚表实际是 UIStatic。 */
    if (window && window->vfptr == (struct UIWndVtbl *)
            ((uintptr_t)GetModuleHandleA(NULL) +
                PAL3_MENU_NAV_CATEGORY_BUTTON_VTABLE_RVA) &&
        (window == &rest->m_Chat.baseclass || window == &rest->m_Rest.baseclass ||
         window == &rest->m_Exit.baseclass))
        return 1;
    return window && window->vfptr == (struct UIWndVtbl *)
        ((uintptr_t)GetModuleHandleA(NULL) + PAL3_MENU_NAV_BUTTON_VTABLE_RVA);
}

static void collect_buttons(struct UIWnd *window, struct UIWnd *active_frame,
    int ancestors_available, struct menu_button *buttons, uint32_t *count,
    uint32_t depth)
{
    struct PtrArray *children;
    int available;
    int i;

    if (!window || depth > PAL3_MENU_NAV_MAX_DEPTH ||
        *count >= PAL3_MENU_NAV_MAX_BUTTONS) return;
    available = ancestors_available && window->m_bcreateok &&
        window->m_bvisible && window->m_benable;
    if (!available) return;

    if (window != active_frame &&
        (is_plain_button(window) || is_state_ai_choice(window) ||
         (active_cover_subpage() == active_frame &&
          (window->vfptr == (struct UIWndVtbl *)((uintptr_t)GetModuleHandleA(NULL) + 0x0016AC70u) ||
           window->vfptr == (struct UIWndVtbl *)((uintptr_t)GetModuleHandleA(NULL) + 0x0016AC90u))))) {
        if (window->m_pfather && window->m_rect.right > window->m_rect.left &&
            window->m_rect.bottom > window->m_rect.top) {
            struct menu_button *candidate = &buttons[*count];
            candidate->button = (struct UIButton *)window;
            candidate->rect.left = window->m_rect.left;
            candidate->rect.top = window->m_rect.top;
            candidate->rect.right = window->m_rect.right;
            candidate->rect.bottom = window->m_rect.bottom;
            candidate->center_x = (window->m_rect.left + window->m_rect.right) / 2;
            candidate->center_y = (window->m_rect.top + window->m_rect.bottom) / 2;
            ++*count;
        }
        return;
    }

    children = &window->m_childs;
    if (!children->m_pData || children->m_nSize <= 0 ||
        children->m_nSize > PAL3_MENU_NAV_MAX_CHILDREN) return;
    for (i = 0; i < children->m_nSize && *count < PAL3_MENU_NAV_MAX_BUTTONS; ++i) {
        struct UIWnd *child = (struct UIWnd *)children->m_pData[i];
        if (child && child->m_pfather == window)
            collect_buttons(child, active_frame, available, buttons, count, depth + 1);
    }
}

static struct UIWnd *current_state_page_root(void);

static struct UIWnd *active_chat_rest_root(void)
{
    struct UIChatRest *rest = &g_gamefrm.m_ChatRest;
    struct UIWnd *root = &rest->baseclass;

    /* 客栈服务挂在场景中；住宿确认仍交给前面的原版弹窗处理。 */
    if (PAL3_s_gamestate != GAME_SCENE || !rest->m_bActive ||
        !root->m_bcreateok || !root->m_bvisible || !root->m_benable ||
        rest->m_answerDlg.m_isDoModel || rest->m_noenoughmsg.m_isDoModel ||
        rest->m_enough.m_isDoModel || rest->m_restover.m_isDoModel)
        return NULL;
    return root;
}

static uint32_t collect_active_buttons(struct menu_button *buttons)
{
    struct UIWnd *active_frame;
    uint32_t count = 0;

    active_frame = active_chat_rest_root();
    if (!active_frame) {
        if (PAL3_s_gamestate != GAME_UI || !g_gamefrm.m_curfrm) return 0;
        active_frame = &g_gamefrm.m_curfrm->baseclass;
        if (active_cover_subpage()) active_frame = active_cover_subpage();
        if (g_gamefrm.m_curfrmid == FRM_S_STATE)
            active_frame = current_state_page_root();
    }
    if (!active_frame) return 0;
    if (!active_frame->m_bcreateok || !active_frame->m_bvisible ||
        !active_frame->m_benable) return 0;
    if (active_frame == &g_gamefrm.m_ChatRest.baseclass) {
        /* 未打开的住宿弹窗也保留着控件，只收集服务菜单的三个入口。 */
        collect_buttons(&g_gamefrm.m_ChatRest.m_Chat.baseclass,
            active_frame, 1, buttons, &count, 0);
        collect_buttons(&g_gamefrm.m_ChatRest.m_Rest.baseclass,
            active_frame, 1, buttons, &count, 0);
        collect_buttons(&g_gamefrm.m_ChatRest.m_Exit.baseclass,
            active_frame, 1, buttons, &count, 0);
    } else collect_buttons(active_frame, active_frame, 1, buttons, &count, 0);
    return count;
}

static uint32_t cover_subpage_state(void)
{
    uint32_t state = 0;
    if (PAL3_s_gamestate == GAME_UI && g_gamefrm.m_curfrmid == FRM_COVER && g_gamefrm.m_curfrm)
        memcpy(&state, (const uint8_t *)g_gamefrm.m_curfrm +
            PAL3_MENU_NAV_COVER_STATE_OFFSET, sizeof(state));
    return state;
}

static struct UIWnd *active_cover_subpage(void)
{
    const uint8_t *cover;
    struct UIWnd *page;
    uint32_t state;

    if (PAL3_s_gamestate != GAME_UI || g_gamefrm.m_curfrmid != FRM_COVER ||
        !g_gamefrm.m_curfrm)
        return NULL;

    cover = (const uint8_t *)g_gamefrm.m_curfrm;
    memcpy(&state, cover + PAL3_MENU_NAV_COVER_STATE_OFFSET, sizeof(state));
    if (state != 1 && state != 3 && state != 4) return NULL;
    /* 现场状态值：1 为前尘忆梦，3 为小游戏，4 为读档。 */
    memcpy(&page, cover + (state == 1 ? 0x3620u :
        state == 3 ? 0x3624u : 0x3628u), sizeof(page));

    /* 读档子页独立于标题按钮树；仅在原版活动页确实可见时让输入通过。 */
    return page && page->m_bcreateok && page->m_bvisible && page->m_benable ? page : NULL;
}

static void collect_category_buttons(struct UIWnd *window,
    struct UIWnd *required_root, int ancestors_visible,
    struct category_button *buttons, uint32_t *count, uint32_t depth)
{
    struct PtrArray *children;
    int available;
    int i;

    if (!window || depth > PAL3_MENU_NAV_MAX_DEPTH ||
        *count >= PAL3_MENU_NAV_MAX_CATEGORIES)
        return;
    available = ancestors_visible && window->m_bcreateok && window->m_bvisible;
    if (!available) return;

    if (window != required_root && window->vfptr == (struct UIWndVtbl *)
            ((uintptr_t)GetModuleHandleA(NULL) +
                PAL3_MENU_NAV_CATEGORY_BUTTON_VTABLE_RVA)) {
        uint32_t category_id = window->m_id;
        /* 用已确认的 ID 识别分类，横向次序取真实控件位置。 */
        if (window->m_pfather == required_root &&
            category_id >= PAL3_MENU_NAV_CATEGORY_ID_FIRST &&
            category_id < PAL3_MENU_NAV_CATEGORY_ID_FIRST +
                PAL3_MENU_NAV_CATEGORY_COUNT &&
            *count < PAL3_MENU_NAV_CATEGORY_COUNT) {
            struct category_button *candidate = &buttons[*count];
            candidate->window = window;
            candidate->index = PAL3MenuNav_CategoryPageIndex(category_id);
            candidate->center_x = window->m_rect.left +
                (window->m_rect.right - window->m_rect.left) / 2;
            /* 状态页0由原版打开流程直接SetPage(0)，固定保留为返回入口。 */
            candidate->enabled = (uint8_t)(!!window->m_benable ||
                candidate->index == 0);
            ++*count;
        }
        return;
    }

    children = &window->m_childs;
    if (!children->m_pData || children->m_nSize <= 0 ||
        children->m_nSize > PAL3_MENU_NAV_MAX_CHILDREN)
        return;
    for (i = 0; i < children->m_nSize &&
            *count < PAL3_MENU_NAV_MAX_CATEGORIES; ++i) {
        struct UIWnd *child = (struct UIWnd *)children->m_pData[i];
        if (child && child->m_pfather == window)
            collect_category_buttons(child, required_root, available,
                buttons, count, depth + 1);
    }
}

static uint32_t collect_active_categories(struct category_button *buttons)
{
    struct UIWnd *root;
    uint32_t count = 0;
    uint32_t i;

    if (PAL3_s_gamestate != GAME_UI || g_gamefrm.m_curfrmid != FRM_S_STATE ||
        !g_gamefrm.m_curfrm)
        return 0;
    root = &g_gamefrm.m_curfrm->baseclass;
    if (!root->m_bcreateok || !root->m_bvisible || !root->m_benable) return 0;
    collect_category_buttons(root, root, 1, buttons, &count, 0);
    for (i = 1; i < count; ++i) {
        struct category_button value = buttons[i];
        uint32_t j = i;
        while (j > 0 && buttons[j - 1].center_x > value.center_x) {
            buttons[j] = buttons[j - 1];
            --j;
        }
        buttons[j] = value;
    }
    return count == PAL3_MENU_NAV_CATEGORY_COUNT ? count : 0;
}

static int find_current_category(const struct category_button *buttons,
    uint32_t count)
{
    uint32_t category_index;
    uint32_t page_indexes[PAL3_MENU_NAV_MAX_CATEGORIES];
    uint32_t i;
    if (!buttons || count != PAL3_MENU_NAV_CATEGORY_COUNT ||
        !g_gamefrm.m_curfrm ||
        (void *)g_gamefrm.m_sysfrm != (void *)g_gamefrm.m_curfrm)
        return -1;
    /* 25B80 是标签高亮索引；SetPage 的 2AFD4 才记录实际显示的页面。 */
    memcpy(&category_index, (const uint8_t *)g_gamefrm.m_sysfrm +
        PAL3_MENU_NAV_STATE_PAGE_INDEX_OFFSET, sizeof(category_index));
    if (category_index >= PAL3_MENU_NAV_CATEGORY_COUNT) return -1;
    for (i = 0; i < count; ++i) page_indexes[i] = buttons[i].index;
    return PAL3MenuNav_FindCategoryForPage(page_indexes, count,
        category_index);
}

static int read_current_state_page(uint32_t *page_index)
{
    if (!page_index || PAL3_s_gamestate != GAME_UI ||
        g_gamefrm.m_curfrmid != FRM_S_STATE || !g_gamefrm.m_curfrm ||
        (void *)g_gamefrm.m_sysfrm != (void *)g_gamefrm.m_curfrm)
        return 0;
    memcpy(page_index, (const uint8_t *)g_gamefrm.m_sysfrm +
        PAL3_MENU_NAV_STATE_PAGE_INDEX_OFFSET, sizeof(*page_index));
    return *page_index < PAL3_MENU_NAV_CATEGORY_COUNT;
}

static int activate_dialog_choice(struct MUIDialog *dialog,
    struct UIWnd *required_root, int physical_a);

static struct UIWnd *current_state_page_root(void)
{
    /* 原版 SetPage 分别通知各页；2AFC4 只指向道具页，不是当前页指针。 */
    static const uint32_t page_offsets[PAL3_MENU_NAV_CATEGORY_COUNT] = {
        0, 0x2AFC4u, 0x2AFC0u, 0x2AFBCu,
        0x2AFB8u, 0x2AFC8u, 0x2AFCCu, 0x2AFD0u
    };
    struct UIWnd *page_root = NULL;
    uint32_t page_index;

    if (!read_current_state_page(&page_index)) return NULL;
    if (page_index == 0) page_root = (struct UIWnd *)g_gamefrm.m_sysfrm;
    else memcpy(&page_root, (const uint8_t *)g_gamefrm.m_sysfrm +
        page_offsets[page_index], sizeof(page_root));
    if (!page_root || !page_root->m_bcreateok ||
        !page_root->m_bvisible || !page_root->m_benable)
        return NULL;
    return page_root;
}

static int is_state_ai_choice(struct UIWnd *window)
{
    uint32_t page;
    return window && read_current_state_page(&page) && page == 4 &&
        window->m_pfather == current_state_page_root() &&
        window->m_id >= 0xA00609u && window->m_id <= 0xA00633u &&
        window->vfptr == (struct UIWndVtbl *)
            ((uintptr_t)GetModuleHandleA(NULL) + 0x0016AD88u);
}

static struct UIWnd *state_lineup_root(void)
{
    uint32_t page;
    return read_current_state_page(&page) && page == 4 ?
        current_state_page_root() : NULL;
}

static void *state_lineup_actor(struct UIWnd *root, int actor)
{
    return actor >= 0 && actor < 5 ?
        (uint8_t *)root + 0x27048u + actor * 0x98u : NULL;
}

static struct UIWnd *state_lineup_queue(struct UIWnd *root, int actor)
{
    return actor >= 0 && actor < 5 ?
        (struct UIWnd *)((uint8_t *)root + 0x25CE0u + actor * 0x158u) : NULL;
}

static int state_lineup_queue_actor(struct UIWnd *root, void *object)
{
    int actor;
    for (actor = 0; actor < 5; ++actor)
        if (object == state_lineup_queue(root, actor)) return actor;
    return -1;
}

static void *state_lineup_source(struct UIWnd *root)
{
    int actor;
    if (!root || !*((uint8_t *)root + 0x26F58u)) return NULL;
    memcpy(&actor, (uint8_t *)root + 0x26F5Cu, sizeof(actor));
    return state_lineup_actor(root, actor);
}

static MAKE_THISCALL(unsigned char, state_lineup_clicked, void *object)
{
    if (s_state_lineup_override && state_lineup_root() == s_state_lineup) {
        const uint8_t *state = (const uint8_t *)object;
        /* 仅替换原版两处点击判断；可见、动画锁定和后续换位仍遵守原版。 */
        if (object != s_state_lineup_click || !state[0x50] || state[0x79])
            return 0;
        s_state_lineup_click = NULL;
        return 1;
    }
    return THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) + 0x0007DA60u,
        unsigned char, void *), object);
}

static uintptr_t original_list_render;

static MAKE_THISCALL(void, render_dialog_choice_list, struct UIWnd *window)
{
    struct UIListBox *list = &g_gamefrm.m_seldlg.m_list;
    DWORD color = list->m_seltexcolor.Color;
    int replace = window == &list->baseclass && !s_mouse_mode;
    /* 只隐藏剧情选项原来的细指示图，行底色、文字和其他页面列表照常绘制。 */
    if (replace) list->m_seltexcolor.Alpha = 0;
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(original_list_render, void, struct UIWnd *), window);
    if (replace) list->m_seltexcolor.Color = color;
}

void PAL3MenuNav_Initialize(void)
{
    static const uint32_t calls[] = {0x0008536Eu, 0x0008574Du};
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uint32_t i;
    memcpy(&s_system_update, (void *)(base + 0x16F344u), 4);
    make_pointer((unsigned)(base + 0x16F344u), system_page_update);
    if (!PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) return;
    for (i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
        int32_t relative;
        memcpy(&relative, (void *)(base + calls[i] + 1), sizeof(relative));
        if (*(uint8_t *)(base + calls[i]) != 0xE8u ||
            base + calls[i] + 5 + relative != base + 0x0007DA60u)
            return;
    }
    for (i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i)
        make_call((unsigned)(base + calls[i]), state_lineup_clicked);
    original_list_render = *(uintptr_t *)(base + PAL3_MENU_NAV_LISTBOX_VTABLE_RVA + 4);
    make_pointer((unsigned)(base + PAL3_MENU_NAV_LISTBOX_VTABLE_RVA + 4), render_dialog_choice_list);
}

static int state_lineup_point(struct UIWnd *root, void *object,
    float *screen_x, float *screen_y)
{
    struct ObjectCamera *control;
    struct gbCamera *camera;
    struct gbVec3D world, eye;
    const struct gbMatrix4 *m;
    float x = 100000.0f, y = 100000.0f;
    memcpy(&control, (uint8_t *)root + 0x26F50u, sizeof(control));
    if (!control || !(camera = control->m_pCamera)) return 0;
    memcpy(&world, (uint8_t *)object + 0x20u, sizeof(world));
    m = &camera->ViewMatrix;
    eye.x = world.x * m->xx + world.y * m->yx + world.z * m->zx + m->tx;
    eye.y = world.x * m->xy + world.y * m->yy + world.z * m->zy + m->ty;
    eye.z = world.x * m->xz + world.y * m->yz + world.z * m->zz + m->tz;
    if (eye.z >= -camera->zNear || eye.z <= -camera->zFar) return 0;
    /* 调用游戏投影函数，保留 PAL3patch 已生效的宽屏修正。 */
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA("GBENGINE.DLL") + 0x000220B0u,
        void, struct gbCamera *, struct gbVec3D *, float *, float *),
        camera, &eye, &x, &y);
    if (!(x > -2.0f && x < 2.0f && y > -2.0f && y < 2.0f)) return 0;
    *screen_x = camera->Viewport.x + (x + 1.0f) * camera->Viewport.width * 0.5f;
    *screen_y = camera->Viewport.y + (1.0f - y) * camera->Viewport.height * 0.5f;
    return 1;
}

struct state_list_search {
    struct UIListBox *list;
    uint32_t matches;
};

static void find_state_page_list_node(struct UIWnd *window,
    int ancestors_visible, uint32_t depth, struct state_list_search *search)
{
    struct PtrArray *children;
    int available;
    int i;

    if (!window || !search || depth > PAL3_MENU_NAV_MAX_DEPTH ||
        search->matches > 1)
        return;
    available = ancestors_visible && window->m_bcreateok &&
        window->m_bvisible && window->m_benable;
    if (!available) return;
    if (window->vfptr == (struct UIWndVtbl *)((uintptr_t)GetModuleHandleA(NULL) +
            PAL3_MENU_NAV_LISTBOX_VTABLE_RVA)) {
        struct UIListBox *list = (struct UIListBox *)window;
        search->list = list;
        ++search->matches;
        return;
    }

    children = &window->m_childs;
    if (!children->m_pData || children->m_nSize <= 0 ||
        children->m_nSize > PAL3_MENU_NAV_MAX_CHILDREN)
        return;
    for (i = 0; i < children->m_nSize && search->matches <= 1; ++i) {
        struct UIWnd *child = (struct UIWnd *)children->m_pData[i];
        if (child && child->m_pfather == window)
            find_state_page_list_node(child, available, depth + 1, search);
    }
}

static struct UIListBox *find_active_state_page_list(void)
{
    struct UIWnd *page_root = current_state_page_root();
    struct state_list_search search;

    if (!page_root) return NULL;
    memset(&search, 0, sizeof(search));
    find_state_page_list_node(page_root, 1, 0, &search);
    return search.matches == 1 ? search.list : NULL;
}

struct state_dialog_search {
    struct MUIDialog *dialog;
    struct UIWndVtbl *vtable;
    uint32_t matches;
};

static void find_state_page_dialog_node(struct UIWnd *window,
    int ancestors_visible, uint32_t depth, struct state_dialog_search *search)
{
    struct PtrArray *children;
    int available;
    int i;

    if (!window || !search || depth > PAL3_MENU_NAV_MAX_DEPTH ||
        search->matches > 1)
        return;
    available = ancestors_visible && window->m_bcreateok &&
        window->m_bvisible && window->m_benable;
    if (!available) return;
    if (window->vfptr == search->vtable) {
        struct MUIDialog *dialog = (struct MUIDialog *)window;
        /* 原版保留未打开的弹窗控件，只有正在执行模态交互时才能屏蔽底层按键。 */
        if (dialog->m_isDoModel &&
            (uint32_t)dialog->m_type <= PAL3_MENU_NAV_DIALOG_TYPE_MSGBOX) {
            search->dialog = dialog;
            ++search->matches;
        }
        return;
    }

    children = &window->m_childs;
    if (!children->m_pData || children->m_nSize <= 0 ||
        children->m_nSize > PAL3_MENU_NAV_MAX_CHILDREN)
        return;
    for (i = 0; i < children->m_nSize && search->matches <= 1; ++i) {
        struct UIWnd *child = (struct UIWnd *)children->m_pData[i];
        if (child && child->m_pfather == window)
            find_state_page_dialog_node(child, available, depth + 1, search);
    }
}

static uint32_t handle_state_page_dialog(const struct PAL3MenuNavInput *input)
{
    struct MUIDialog *known_dialog = &g_gamefrm.m_ChatRest.m_answerDlg;
    struct UIWnd *page_root = current_state_page_root();
    struct UIWnd *known_window = &known_dialog->baseclass_0.baseclass;
    struct state_dialog_search search;
    int physical_a = -1;
    uint32_t consumed;

    if (!input || !page_root || !known_window->vfptr) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] modal-scan skipped page_root=%p known_vtable=%p",
            page_root, known_window->vfptr);
        return 0;
    }
    memset(&search, 0, sizeof(search));
    search.vtable = known_window->vfptr;
    find_state_page_dialog_node(page_root, 1, 0, &search);
    PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] modal-scan matches=%lu dialog=%p page_root=%p type=%d create=%d visible=%d enable=%d isModel=%d isDoModel=%d",
        (unsigned long)search.matches, search.dialog, page_root,
        search.dialog ? search.dialog->m_type : -1,
        search.dialog ? search.dialog->baseclass_0.baseclass.m_bcreateok : 0,
        search.dialog ? search.dialog->baseclass_0.baseclass.m_bvisible : 0,
        search.dialog ? search.dialog->baseclass_0.baseclass.m_benable : 0,
        search.dialog ? search.dialog->m_IsModel : 0,
        search.dialog ? search.dialog->m_isDoModel : 0);
    if (!search.matches) return 0;

    if (input->pressed & XINPUT_B) physical_a = 1;
    else if (input->pressed & XINPUT_A) physical_a = 0;
    if (search.matches == 1 && physical_a >= 0 &&
        PAL3MenuNav_ShouldNavigate(
            (input->held & XINPUT_LEFT_SHOULDER) != 0, input->mouse_mode))
        activate_dialog_choice(search.dialog, page_root, physical_a);

    /* 弹窗显示期间屏蔽底层方向和按钮，避免分类切换或页面项收到输入。 */
    consumed = PAL3_MENU_NAV_MODAL | PAL3_MENU_NAV_UP |
        PAL3_MENU_NAV_DOWN | PAL3_MENU_NAV_LEFT | PAL3_MENU_NAV_RIGHT;
    if (physical_a == 1) consumed |= PAL3_MENU_NAV_CONFIRM;
    else if (physical_a == 0) consumed |= PAL3_MENU_NAV_CANCEL;
    return consumed;
}

static void commit_state_lineup(void)
{
    uint32_t page;
    struct UIWnd *root;
    if (!read_current_state_page(&page) || page != 4) return;
    root = current_state_page_root();
    if (!root) return;
    /* 原版点击阵列页标签前会提交队列、阵位和 AI；手柄切页也必须走这一步。 */
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) + 0x00085CD0u,
        void, struct UIWnd *), root);
}

static int set_state_page(uint32_t page_index)
{
    uint32_t current_page;
    uint32_t before_page = 0xFFFFFFFFu;
    int success;
    uintptr_t set_page_address = (uintptr_t)GetModuleHandleA(NULL) +
        PAL3_MENU_NAV_STATE_SET_PAGE_RVA;

    if (page_index >= PAL3_MENU_NAV_CATEGORY_COUNT ||
        !read_current_state_page(&current_page)) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] SetPage skipped request=%lu page_valid=%d",
            (unsigned long)page_index,
            page_index < PAL3_MENU_NAV_CATEGORY_COUNT);
        return 0;
    }
    before_page = current_page;
    if (page_index != current_page) commit_state_lineup();
    /* 原版打开StateUI时先调用4A4780，再由UIGameFrm执行SetPage(0)。 */
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(set_page_address, void, void *, int),
        g_gamefrm.m_sysfrm, (int)page_index);
    success = read_current_state_page(&current_page) && current_page == page_index;
    PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] SetPage request=%lu before=%lu after=%lu result=%d",
        (unsigned long)page_index, (unsigned long)before_page,
        success ? (unsigned long)current_page : 0xFFFFFFFFu, success);
    return success;
}

static int click_category(const struct category_button *buttons,
    uint32_t count, int current, int direction)
{
    uint8_t enabled[PAL3_MENU_NAV_MAX_CATEGORIES];
    struct UIWnd *window;
    uint32_t target;
    uint32_t i;

    if (count > PAL3_MENU_NAV_MAX_CATEGORIES || current < 0 ||
        (uint32_t)current >= count) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] category-click invalid count=%lu current=%d",
            (unsigned long)count, current);
        return 0;
    }
    for (i = 0; i < count; ++i) enabled[i] = buttons[i].enabled;
    target = PAL3MenuNav_NextCategory(enabled, count, (uint32_t)current,
        direction);
    if (target >= count || !buttons[target].window ||
        buttons[target].window->m_pfather != &g_gamefrm.m_curfrm->baseclass ||
        (void *)g_gamefrm.m_sysfrm != (void *)g_gamefrm.m_curfrm) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] category-click rejected target=%lu count=%lu window=%p parent_ok=%d sysfrm_ok=%d",
            (unsigned long)target, (unsigned long)count,
            target < count ? buttons[target].window : NULL,
            target < count && buttons[target].window &&
                buttons[target].window->m_pfather ==
                    &g_gamefrm.m_curfrm->baseclass,
            g_gamefrm.m_curfrm &&
                (void *)g_gamefrm.m_sysfrm == (void *)g_gamefrm.m_curfrm);
        return 0;
    }
    PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] category-click current=%d target=%lu id=%lu page=%lu direction=%d",
        current, (unsigned long)target,
        (unsigned long)buttons[target].window->m_id,
        (unsigned long)buttons[target].index, direction);

    window = &g_gamefrm.m_curfrm->baseclass;
    if (!window->vfptr || !window->vfptr->OnMessage) return 0;
    THISCALL_WRAPPER(window->vfptr->OnMessage, window,
        PAL3_MENU_NAV_CATEGORY_HOVER_MESSAGE,
        buttons[target].window->m_id, 0);
    if (buttons[target].index == 0)
        return set_state_page(buttons[target].index);
    if (buttons[target].index != buttons[current].index) commit_state_lineup();
    /* 复用 UIStatic_Update 的按下与释放事件；释放消息 1001 才会执行原版 SetPage。 */
    THISCALL_WRAPPER(window->vfptr->OnMessage, window,
        PAL3_MENU_NAV_CATEGORY_MOUSE_DOWN_MESSAGE,
        buttons[target].window->m_id, 0);
    THISCALL_WRAPPER(window->vfptr->OnMessage, window,
        PAL3_MENU_NAV_BUTTON_MESSAGE, buttons[target].window->m_id, 0);
    {
        uint32_t current_page;
        int success = read_current_state_page(&current_page) &&
            current_page == buttons[target].index;
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] category messages=0x%03x,0x%03x page_now=%lu result=%d",
            PAL3_MENU_NAV_CATEGORY_MOUSE_DOWN_MESSAGE,
            PAL3_MENU_NAV_BUTTON_MESSAGE,
            success ? (unsigned long)current_page : 0xFFFFFFFFu,
            success);
        return success;
    }
}

static int find_button(const struct menu_button *buttons, uint32_t count,
    struct UIButton *button)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
        if (buttons[i].button == button) return (int)i;
    return -1;
}

static uint32_t initial_button(const struct menu_button *buttons, uint32_t count)
{
    uint32_t best = 0;
    uint32_t i;
    for (i = 0; i < count; ++i)
        if (buttons[i].button->baseclass.m_bfocus) return i;
    for (i = 1; i < count; ++i) {
        if (buttons[i].center_y < buttons[best].center_y ||
            (buttons[i].center_y == buttons[best].center_y &&
                buttons[i].center_x < buttons[best].center_x))
            best = i;
    }
    return best;
}

static int select_direction(int32_t dx, int32_t dy,
    const struct menu_button *buttons, uint32_t count)
{
    struct PAL3MenuNavRect rects[PAL3_MENU_NAV_MAX_BUTTONS];
    int current;
    uint32_t next;
    uint32_t i;

    if (!count) return 0;
    current = find_button(buttons, count, s_selected);
    if (current < 0) current = (int)initial_button(buttons, count);
    for (i = 0; i < count; ++i) rects[i] = buttons[i].rect;
    next = PAL3MenuNav_SelectNeighbor(rects, count, (uint32_t)current, dx, dy);
    if (next == (uint32_t)current) return 0;
    s_selected = buttons[next].button;
    s_selection_engaged = 1;
    return 1;
}

static int has_direction_neighbor(int32_t dx, int32_t dy,
    const struct menu_button *buttons, uint32_t count)
{
    struct PAL3MenuNavRect rects[PAL3_MENU_NAV_MAX_BUTTONS];
    int current = find_button(buttons, count, s_selected);
    uint32_t i;

    if (!count || current < 0) return 0;
    for (i = 0; i < count; ++i) rects[i] = buttons[i].rect;
    return PAL3MenuNav_SelectNeighbor(rects, count, (uint32_t)current, dx, dy) !=
        (uint32_t)current;
}

static int activate_selected(const struct menu_button *buttons, uint32_t count)
{
    struct UIWnd *parent;
    int selected = find_button(buttons, count, s_selected);
    if (!count) return 0;
    if (selected < 0) selected = (int)initial_button(buttons, count);
    s_selected = buttons[selected].button;
    s_selection_engaged = 1;
    parent = s_selected->baseclass.m_pfather;
    if (!parent || !parent->vfptr || !parent->vfptr->OnMessage) return 0;

    if (is_state_ai_choice(&s_selected->baseclass)) {
        uint8_t *state = (uint8_t *)s_selected;
        int checked = !state[0x44];
        /* 阵列页还检查 0x45 的点击标记，否则只改显示，角色设置不会写入。 */
        state[0x45] = 1;
        state[0x44] = (uint8_t)checked;
        memcpy(state + 0x50, &checked, sizeof(checked));
        THISCALL_WRAPPER(parent->vfptr->OnMessage, parent,
            PAL3_MENU_NAV_BUTTON_MESSAGE, s_selected->baseclass.m_id, checked);
        state[0x45] = 0;
        state[0x46] = 0;
        return 1;
    }

    /* 复用 UIButton_Update 发给父窗体的原生确认消息。 */
    THISCALL_WRAPPER(parent->vfptr->OnMessage, parent,
        PAL3_MENU_NAV_BUTTON_MESSAGE, s_selected->baseclass.m_id, 0);
    return 1;
}

static int direction_event(uint8_t held, uint8_t index, uint8_t bit, uint32_t now_ms);

static uint32_t handle_state_lineup(const struct PAL3MenuNavInput *input,
    uint8_t directions)
{
    struct UIWnd *root = state_lineup_root();
    struct menu_button buttons[PAL3_MENU_NAV_MAX_BUTTONS];
    struct PAL3MenuNavRect rects[PAL3_MENU_NAV_MAX_BUTTONS + 10];
    void *targets[PAL3_MENU_NAV_MAX_BUTTONS + 10];
    struct UIButton *choices[PAL3_MENU_NAV_MAX_BUTTONS + 10] = {0};
    void *source;
    uint32_t count = 0, button_count, i;
    int selected = -1, initialized = 0;
    double scale = (game_frect_43.right - game_frect_43.left) / 800.0;

    if (root != s_state_lineup) {
        s_state_lineup = root;
        s_state_lineup_focus = s_state_lineup_click = NULL;
    }
    if (!root || scale <= 0.0) return 0;
    source = state_lineup_source(root);
    if (!input->controller_active || input->mouse_mode ||
        (input->held & XINPUT_LEFT_SHOULDER)) {
        /* 两次点击之间切鼠标或失焦时，点回原角色只取消选择，不更改阵位。 */
        s_state_lineup_click = source;
        s_state_lineup_override = source != NULL;
        s_state_lineup_focus = NULL;
        s_selected = NULL;
        s_mouse_mode = 1;
        s_previous_directions = 0;
        return directions | ((input->pressed & XINPUT_B) ? PAL3_MENU_NAV_CONFIRM : 0);
    }
    s_state_lineup_override = 1;
    s_mouse_mode = 0;
    if (input->pressed & XINPUT_A) {
        if (!source) return 0;
        s_state_lineup_click = source;
        s_state_lineup_focus = source;
        return directions | PAL3_MENU_NAV_CANCEL;
    }
    for (i = 0; i < (source ? 6u : 5u); ++i) {
        void *object;
        float x, y;
        int32_t left, top;
        if (source) {
            int actor;
            object = (uint8_t *)root + 0x27360u + i * 0x98u;
            /* 原版 27348 表在队友互换后不更新，按当前角色阵位找占用者。 */
            for (actor = 0; actor < 5; ++actor)
                if (*((uint8_t *)root + 0x169u + actor) &&
                    *((uint8_t *)root + 0x25C32u + actor) == i) {
                    object = state_lineup_actor(root, actor);
                    break;
                }
        } else object = state_lineup_actor(root, (int)i);
        if (!object || !*((uint8_t *)object + 0x50u) ||
            !state_lineup_point(root, object, &x, &y)) continue;
        left = (int32_t)((x - game_frect_43.left) / scale);
        top = (int32_t)((y - game_frect_43.top) / scale);
        rects[count] = (struct PAL3MenuNavRect){left - 16, top - 16, left + 16, top + 16};
        targets[count++] = object;
    }
    if (!source) {
        for (i = 0; i < 5; ++i) {
            struct UIWnd *portrait = state_lineup_queue(root, (int)i);
            if (!*((uint8_t *)root + 0x169u + i) || !portrait->m_benable)
                continue;
            /* 当前领队的普通头像隐藏，由另一张高亮图显示，仍保留该角色的焦点。 */
            targets[count] = portrait;
            rects[count++] = (struct PAL3MenuNavRect){
                portrait->m_rect.left, portrait->m_rect.top,
                portrait->m_rect.right, portrait->m_rect.bottom};
        }
        button_count = collect_active_buttons(buttons);
        for (i = 0; i < button_count; ++i) {
            targets[count] = buttons[i].button;
            choices[count] = buttons[i].button;
            rects[count++] = buttons[i].rect;
        }
    }
    for (i = 0; i < count; ++i)
        if (targets[i] == s_state_lineup_focus) selected = (int)i;
    if (!count) return directions;
    if (selected < 0) { selected = 0; initialized = 1; }
    for (i = 0; i < 4; ++i) {
        static const int dx[] = {0, 0, -1, 1};
        static const int dy[] = {-1, 1, 0, 0};
        uint8_t bit = (uint8_t)(1u << i);
        if ((directions & bit) && direction_event(directions, i, bit, input->now_ms))
            selected = (int)PAL3MenuNav_SelectNeighbor(rects, count,
                (uint32_t)selected, dx[i], dy[i]);
    }
    s_previous_directions = directions;
    s_state_lineup_focus = targets[selected];
    s_selected = choices[selected];
    s_selection_engaged = s_selected != NULL;
    PAL3_MENU_EDGE_LOG("[PAL3MenuLineup] source=%p target=%p choice=%lu center=%ld,%ld",
        source, s_state_lineup_focus,
        s_selected ? (unsigned long)s_selected->baseclass.m_id : 0,
        (long)((rects[selected].left + rects[selected].right) / 2),
        (long)((rects[selected].top + rects[selected].bottom) / 2));
    if ((input->pressed & XINPUT_B) && !initialized) {
        int queue_actor = state_lineup_queue_actor(root, targets[selected]);
        if (queue_actor >= 0) {
            /* 原版消息负责交换队列、更新头像及切换场景领队，不能只改页内顺序。 */
            THISCALL_WRAPPER(root->vfptr->OnMessage, root,
                PAL3_MENU_NAV_BUTTON_MESSAGE, 0xA00002u + queue_actor, 0);
        } else if (s_selected) {
            struct menu_button choice = {0};
            choice.button = s_selected;
            activate_selected(&choice, 1);
        } else {
            s_state_lineup_click = targets[selected];
            if (source) s_state_lineup_focus = source;
        }
    }
    return directions | ((input->pressed & XINPUT_B) ? PAL3_MENU_NAV_CONFIRM : 0);
}

static struct UIWnd *find_state_close_button(struct UIWnd *window,
    struct UIWnd *required_root, int ancestors_visible, uint32_t depth)
{
    struct PtrArray *children;
    int available;
    int i;

    if (!window || depth > PAL3_MENU_NAV_MAX_DEPTH) return NULL;
    available = ancestors_visible && window->m_bcreateok &&
        window->m_bvisible && window->m_benable;
    if (!available) return NULL;
    if (window != required_root &&
        window->m_id == PAL3_MENU_NAV_STATE_CLOSE_BUTTON_ID &&
        window->vfptr == (struct UIWndVtbl *)((uintptr_t)GetModuleHandleA(NULL) +
            PAL3_MENU_NAV_CATEGORY_BUTTON_VTABLE_RVA) &&
        window->m_pfather && window->m_rect.right > window->m_rect.left &&
        window->m_rect.bottom > window->m_rect.top)
        return window;

    children = &window->m_childs;
    if (!children->m_pData || children->m_nSize <= 0 ||
        children->m_nSize > PAL3_MENU_NAV_MAX_CHILDREN)
        return NULL;
    for (i = 0; i < children->m_nSize; ++i) {
        struct UIWnd *child = (struct UIWnd *)children->m_pData[i];
        struct UIWnd *button;
        if (!child || child->m_pfather != window) continue;
        button = find_state_close_button(child, required_root, available,
            depth + 1);
        if (button) return button;
    }
    return NULL;
}

static int activate_state_close_button(void)
{
    struct UIWnd *root;
    struct UIWnd *button;
    struct UIWnd *parent;

    if (PAL3_s_gamestate != GAME_UI || g_gamefrm.m_curfrmid != FRM_S_STATE ||
        !g_gamefrm.m_curfrm)
        return 0;
    root = &g_gamefrm.m_curfrm->baseclass;
    if (!root->m_bcreateok || !root->m_bvisible || !root->m_benable)
        return 0;
    button = find_state_close_button(root, root, 1, 0);
    if (!button) return 0;
    parent = button->m_pfather;
    if (!parent || !parent->vfptr || !parent->vfptr->OnMessage) return 0;

    /* X 控件按现场确认的 UIWnd 基类字段发原版消息，不读取按钮扩展字段。 */
    THISCALL_WRAPPER(parent->vfptr->OnMessage, parent,
        PAL3_MENU_NAV_BUTTON_MESSAGE, button->m_id, 0);
    return 1;
}

static int ancestors_available(struct UIWnd *window, struct UIWnd *required_root)
{
    while (window) {
        if (!window->m_bcreateok || !window->m_bvisible || !window->m_benable)
            return 0;
        if (window == required_root) return 1;
        window = window->m_pfather;
    }
    return 0;
}

static int activate_dialog_choice(struct MUIDialog *dialog,
    struct UIWnd *required_root, int physical_a)
{
    struct UIButton *button = NULL;
    struct UIWnd *window;
    enum PAL3MenuNavDialogChoice choice;

    if (!dialog || !required_root ||
        !ancestors_available(&dialog->baseclass_0.baseclass, required_root))
        return 0;
    if (dialog->m_type == DLG_MSGBOX) {
        int confirm = g_input.KeyInfo[7].PressTime;
        window = &dialog->baseclass_0.baseclass;
        if (!window->vfptr || !window->vfptr->Update) return 0;
        /* 无按钮提示由原版 Update 结束，保留其最短显示时间与结束通知。 */
        g_input.KeyInfo[7].PressTime = 1;
        THISCALL_WRAPPER(window->vfptr->Update, window, 0.0f, 1);
        g_input.KeyInfo[7].PressTime = confirm;
        return 1;
    }
    choice = PAL3MenuNav_SelectDialogChoice((uint32_t)dialog->m_type,
        physical_a,
        dialog->mb_Yes.baseclass.m_bcreateok &&
            dialog->mb_Yes.baseclass.m_bvisible &&
            dialog->mb_Yes.baseclass.m_benable &&
            ancestors_available(&dialog->mb_Yes.baseclass,
                &dialog->baseclass_0.baseclass),
        dialog->mb_No.baseclass.m_bcreateok &&
            dialog->mb_No.baseclass.m_bvisible &&
            dialog->mb_No.baseclass.m_benable &&
            ancestors_available(&dialog->mb_No.baseclass,
                &dialog->baseclass_0.baseclass));
    if (choice == PAL3_MENU_NAV_DIALOG_YES) button = &dialog->mb_Yes;
    else if (choice == PAL3_MENU_NAV_DIALOG_NO) button = &dialog->mb_No;
    else return 0;

    window = &button->baseclass;
    if (!window->m_bcreateok || !window->m_bvisible || !window->m_benable ||
        !ancestors_available(window, &dialog->baseclass_0.baseclass) ||
        !window->m_pfather || !window->m_pfather->vfptr ||
        !window->m_pfather->vfptr->OnMessage)
        return 0;

    /* UIButton 的原生更新路径以此消息把按钮 ID 交给父窗体。 */
    THISCALL_WRAPPER(window->m_pfather->vfptr->OnMessage, window->m_pfather,
        PAL3_MENU_NAV_BUTTON_MESSAGE, window->m_id, 0);
    return 1;
}

static int handle_unique_dialog_input(struct MUIDialog **dialogs,
    uint32_t count, struct UIWnd *required_root, int physical_a)
{
    struct MUIDialog *active_dialog = NULL;
    uint32_t active_count = 0;
    uint32_t i;

    if (!dialogs || !required_root) return 0;
    for (i = 0; i < count; ++i) {
        struct MUIDialog *dialog = dialogs[i];
        /* 住宿提示框未打开时仍可见，只有原版模态状态才表示正在处理输入。 */
        if (dialog && dialog->m_isDoModel &&
            ancestors_available(&dialog->baseclass_0.baseclass,
                required_root)) {
            active_dialog = dialog;
            ++active_count;
        }
    }
    if (!active_count) return 0;
    /* 两个候选同时显示时不猜；仍消费按键，避免穿透到下面的菜单。 */
    if (active_count == 1)
        activate_dialog_choice(active_dialog, required_root, physical_a);
    return 1;
}

static uint8_t direction_state(const struct PAL3MenuNavInput *input);
static uint32_t handle_archive_navigation(struct UIWnd *root, int system_page,
    const struct PAL3MenuNavInput *input, uint8_t directions);

static uint32_t handle_cover_subpage_input(struct UIWnd *page,
    const struct PAL3MenuNavInput *input)
{
    struct MUIDialog *dialogs[2];
    int physical_a = (input->pressed & XINPUT_B) != 0;
    int physical_b = (input->pressed & XINPUT_A) != 0;
    unsigned int i;

    if (input->mouse_mode) return 0;
    dialogs[0] = (struct MUIDialog *)((uint8_t *)page + 0x1FD3Cu);
    dialogs[1] = (struct MUIDialog *)((uint8_t *)page + 0x20294u);
    for (i = 0; i < 2; ++i) {
        /* 原版读档处理函数以 m_isDoModel 锁定输入，隐藏的弹窗不能吞掉按键。 */
        if (dialogs[i]->m_isDoModel) {
            if (physical_a || physical_b)
                activate_dialog_choice(dialogs[i],
                    &dialogs[i]->baseclass_0.baseclass, physical_a);
            return PAL3_MENU_NAV_MODAL | PAL3_MENU_NAV_UP | PAL3_MENU_NAV_DOWN |
                PAL3_MENU_NAV_LEFT | PAL3_MENU_NAV_RIGHT |
                (physical_a ? PAL3_MENU_NAV_CONFIRM : 0) |
                (physical_b ? PAL3_MENU_NAV_CANCEL : 0);
        }
    }
    if (physical_a) {
        int up = g_input.KeyInfo[4].PressTime;
        int down = g_input.KeyInfo[5].PressTime;
        int confirm = g_input.KeyInfo[7].PressTime;
        /* 复用原版选槽确认函数：有效性检查、提示和读取流程都交给游戏。 */
        g_input.KeyInfo[4].PressTime = g_input.KeyInfo[5].PressTime = 0;
        g_input.KeyInfo[7].PressTime = 1;
        THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
            (uintptr_t)GetModuleHandleA(NULL) + PAL3_MENU_NAV_LOAD_KEY_HANDLER_RVA,
            void, void *), page);
        g_input.KeyInfo[4].PressTime = up;
        g_input.KeyInfo[5].PressTime = down;
        g_input.KeyInfo[7].PressTime = confirm;
        return PAL3_MENU_NAV_CONFIRM;
    }
    if (physical_b && page->vfptr && page->vfptr->OnMessage) {
        THISCALL_WRAPPER(page->vfptr->OnMessage, page,
            PAL3_MENU_NAV_BUTTON_MESSAGE, 0xD1u, 0);
        return PAL3_MENU_NAV_CANCEL;
    }
    return handle_archive_navigation(page, 0, input, direction_state(input));
}

static uint32_t handle_scene_dialog(const struct PAL3MenuNavInput *input)
{
    struct MUIDialog *dialogs[] = {
        &g_gamefrm.m_ChatRest.m_answerDlg,
        &g_gamefrm.m_ChatRest.m_noenoughmsg,
        &g_gamefrm.m_ChatRest.m_enough,
        &g_gamefrm.m_ChatRest.m_restover
    };
    struct UIWnd *rest_root = &g_gamefrm.m_ChatRest.baseclass;
    uint32_t action;
    int physical_a;

    if (input->pressed & XINPUT_B) physical_a = 1;
    else if (input->pressed & XINPUT_A) physical_a = 0;
    else return 0;

    action = physical_a ?
        PAL3_MENU_NAV_CONFIRM : PAL3_MENU_NAV_CANCEL;
    if (!PAL3MenuNav_ShouldNavigate(
            (input->held & XINPUT_LEFT_SHOULDER) != 0, input->mouse_mode))
        return action;

    if (PAL3_s_gamestate == GAME_SCENE) {
        if (handle_unique_dialog_input(dialogs,
                (uint32_t)(sizeof(dialogs) / sizeof(dialogs[0])),
                rest_root, physical_a))
            return action;
    }

    if (PAL3_s_gamestate == GAME_SUBGAME_SKEE &&
        g_gamefrm.m_curfrmid == FRM_SKEE && g_gamefrm.m_curfrm) {
        struct UISkee *skee = (struct UISkee *)g_gamefrm.m_curfrm;
        struct UIWnd *skee_root = &skee->uiframewnd0.baseclass;
        struct MUIDialog *skee_dialogs[] = {
            &skee->result_dlg, &skee->help_dlg
        };
        if (handle_unique_dialog_input(skee_dialogs, 2, skee_root,
                physical_a))
            return action;
    }
    return 0;
}

static uint8_t direction_state(const struct PAL3MenuNavInput *input)
{
    uint8_t dirs = 0;
    int up = (input->held & XINPUT_DPAD_UP) || input->stick_y > PAL3_MENU_NAV_DEADZONE;
    int down = (input->held & XINPUT_DPAD_DOWN) || input->stick_y < -PAL3_MENU_NAV_DEADZONE;
    int left = (input->held & XINPUT_DPAD_LEFT) || input->stick_x < -PAL3_MENU_NAV_DEADZONE;
    int right = (input->held & XINPUT_DPAD_RIGHT) || input->stick_x > PAL3_MENU_NAV_DEADZONE;

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

int PAL3MenuNav_ShouldSwitchCategoryOnLRelease(int was_held, int is_held,
    int used_for_mouse, uint8_t directions_while_held)
{
    return was_held && !is_held && !used_for_mouse &&
        directions_while_held == 0;
}

int PAL3MenuNav_ShouldNavigate(int left_shoulder_held, int mouse_mode)
{
    return !left_shoulder_held && !mouse_mode;
}

static int direction_event(uint8_t held, uint8_t index, uint8_t bit,
    uint32_t now_ms)
{
    if (!(held & bit)) return 0;
    if (!(s_previous_directions & bit)) {
        s_repeat_at[index] = now_ms + PAL3_MENU_NAV_INITIAL_REPEAT_MS;
        return 1;
    }
    if (time_reached(now_ms, s_repeat_at[index])) {
        s_repeat_at[index] = now_ms + PAL3_MENU_NAV_REPEAT_MS;
        return 1;
    }
    return 0;
}

static uint32_t handle_archive_navigation(struct UIWnd *root, int system_page,
    const struct PAL3MenuNavInput *input, uint8_t directions)
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    int up, down, i;
    int saved[5];
    static const int keys[] = {4, 5, 7, 9, 10};
    int scroll = input->right_stick_y > PAL3_MENU_NAV_DEADZONE ? -1 :
        input->right_stick_y < -PAL3_MENU_NAV_DEADZONE ? 1 : 0;
    int fresh = s_archive_scroll_root != root || s_archive_scroll_direction != scroll;
    uint32_t action = directions & (PAL3_MENU_NAV_UP | PAL3_MENU_NAV_DOWN);
    if (!input->controller_active || input->mouse_mode) return 0;
    if (system_page && s_system_root != root) {
        s_system_root = root;
        s_system_focus = 0;
    }
    /* Switch 的 Y 对应 XInput X；只在没有确认弹窗的系统页打开，整帧交给覆盖页。 */
    if (PAL3Mod_Enabled(PAL3_MOD_CHEATS) && system_page && (input->pressed & XINPUT_X)) {
        s_cheat_open = 1;
        s_cheat_row = 0;
        s_previous_directions = 0;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
        g_input.KeyInfo[0].PressTime = g_input.KeyInfo[0].Data = 0;
        return PAL3_MENU_NAV_MODAL | PAL3_MENU_NAV_CONFIRM | PAL3_MENU_NAV_CANCEL |
            PAL3_MENU_NAV_CATEGORY | directions;
    }
    up = direction_event(directions, 0, PAL3_MENU_NAV_UP, input->now_ms);
    down = direction_event(directions, 1, PAL3_MENU_NAV_DOWN, input->now_ms);
    if (system_page) {
        /* 列表首行再按上可选择存读标签，A 只选标签；不会直接保存或读取。 */
        if (!s_system_focus && up && *(int *)((uint8_t *)root + 0x45E9Cu) == 0) {
            s_system_focus = 4;
            up = 0;
        } else if (s_system_focus == 4 && down) {
            s_system_focus = 0;
            down = 0;
        }
        if (s_system_focus == 4) {
            int mode = *(int *)((uint8_t *)root + 0x45EA4u);
            int wanted = mode;
            if (direction_event(directions, 2, PAL3_MENU_NAV_LEFT, input->now_ms)) wanted = 0;
            if (direction_event(directions, 3, PAL3_MENU_NAV_RIGHT, input->now_ms)) wanted = 1;
            if (wanted != mode) {
                struct UIWnd *tab = (struct UIWnd *)((uint8_t *)root +
                    (wanted ? 0x25DA4u : 0x25CACu));
                if (tab->m_benable)
                    THISCALL_WRAPPER(root->vfptr->OnMessage, root,
                        PAL3_MENU_NAV_BUTTON_MESSAGE, tab->m_id, 0);
            }
            if (input->pressed & XINPUT_B) s_system_focus = 0;
            up = down = 0;
            action |= directions | PAL3_MENU_NAV_CONFIRM;
        }
        /* 右侧保留音乐和音效；金手指由 Y 直接打开，不占方向焦点。 */
        else {
        if (direction_event(directions, 2, PAL3_MENU_NAV_LEFT, input->now_ms) && s_system_focus > 0)
            --s_system_focus;
        if (direction_event(directions, 3, PAL3_MENU_NAV_RIGHT, input->now_ms) && s_system_focus < 2)
            ++s_system_focus;
        }
        action |= directions & (PAL3_MENU_NAV_LEFT | PAL3_MENU_NAV_RIGHT);
        if (input->pressed & XINPUT_Y) {
            int mode;
            struct UIWnd *target;
            memcpy(&mode, (uint8_t *)root + 0x45EA4u, sizeof(mode));
            target = (struct UIWnd *)((uint8_t *)root +
                (mode ? 0x25CACu : 0x25DA4u));
            /* 剧情禁止保存时沿用原按钮的限制，手柄不能绕过禁用状态。 */
            if (target->m_benable)
                THISCALL_WRAPPER(root->vfptr->OnMessage, root,
                    PAL3_MENU_NAV_BUTTON_MESSAGE, target->m_id, 0);
            s_system_focus = 0;
        }
        if (s_system_focus == 1 || s_system_focus == 2) {
            struct UIWnd *slider = (struct UIWnd *)((uint8_t *)root +
                (s_system_focus == 1 ? 0x45B40u : 0x45C50u));
            float value, maximum;
            memcpy(&value, (uint8_t *)slider + 0xE8u, sizeof(value));
            memcpy(&maximum, (uint8_t *)slider + 0xE4u, sizeof(maximum));
            if ((up || down) && maximum > 0.0f) {
                value += (up ? 1.0f : -1.0f) * maximum / 20.0f;
                if (value < 0.0f) value = 0.0f;
                if (value > maximum) value = maximum;
                THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + 0x0003C550u,
                    void, struct UIWnd *, float), slider, value);
                /* 原版滑条使用 3FB 通知，3ED 不会应用到音频。 */
                THISCALL_WRAPPER(root->vfptr->OnMessage, root, 0x3FBu, slider->m_id, 0);
            }
            if (input->pressed & XINPUT_B) action |= PAL3_MENU_NAV_CONFIRM;
            up = down = 0;
        }
    }
    for (i = 0; i < 5; ++i) {
        saved[i] = g_input.KeyInfo[keys[i]].PressTime;
        g_input.KeyInfo[keys[i]].PressTime = 0;
    }
    g_input.KeyInfo[4].PressTime = up;
    g_input.KeyInfo[5].PressTime = down;
    if (up || down)
        THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base +
            (system_page ? 0x000B2D20u : PAL3_MENU_NAV_LOAD_KEY_HANDLER_RVA),
            void, struct UIWnd *), root);
    for (i = 0; i < 5; ++i) g_input.KeyInfo[keys[i]].PressTime = saved[i];
    s_archive_scroll_root = root;
    s_archive_scroll_direction = scroll;
    if (scroll && (fresh || time_reached(input->now_ms, s_archive_scroll_at))) {
        s_archive_scroll_at = input->now_ms + (fresh ?
            PAL3_MENU_NAV_INITIAL_REPEAT_MS : PAL3_MENU_NAV_REPEAT_MS);
        /* 存档列表不是 UIListBox，调用其自己的翻行入口才能同步缩略图和滚动条。 */
        THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(base + (system_page ?
            (scroll < 0 ? 0x000B1FB0u : 0x000B1EA0u) :
            (scroll < 0 ? 0x00055B30u : 0x00055A20u)),
            unsigned char, struct UIWnd *, int), root, 1);
    }
    s_previous_directions = directions;
    return action;
}

static int system_page_visible(void)
{
    uint32_t page;
    return read_current_state_page(&page) && page == 7 && current_state_page_root();
}

int PAL3MenuNav_ToggleCheat(void)
{
    struct PAL3MenuNavInput idle;
    if (!PAL3Mod_Enabled(PAL3_MOD_CHEATS) || !system_page_visible()) return 0;
    memset(&idle, 0, sizeof(idle));
    if (!s_cheat_open && handle_state_page_dialog(&idle)) return 0;
    s_cheat_open = !s_cheat_open;
    s_cheat_row = 0;
    s_cheat_close_frames = s_cheat_open ? 0 : 2;
    s_previous_directions = 0;
    memset(s_repeat_at, 0, sizeof(s_repeat_at));
    return 1;
}

static void change_cheat_rate(int row, int step)
{
    int value = PAL3Gamepad_GetExperienceRate(row) + step;
    if (value < 1) value = 1;
    if (value > 10) value = 10;
    PAL3Gamepad_SetExperienceRate(row, value);
}

int PAL3MenuNav_MouseMessage(unsigned message, long position)
{
    double scale = (game_frect_43.right - game_frect_43.left) / 800.0;
    int x, y, row;
    if (!PAL3Mod_Enabled(PAL3_MOD_CHEATS)) return 0;
    if (!system_page_visible() || scale <= 0) { s_cheat_open = 0; return 0; }
    if (message != WM_LBUTTONDOWN && message != WM_LBUTTONUP &&
        message != WM_LBUTTONDBLCLK) return 0;
    x = (int)(((short)(position & 0xFFFF) - game_frect_43.left) / scale);
    y = (int)(((short)((unsigned long)position >> 16) - game_frect_43.top) / scale);
    if (!s_cheat_open) return 0;
    if (message == WM_LBUTTONUP) {
        if (x >= 350 && x <= 450 && y >= 365 && y <= 398) {
            s_cheat_open = 0;
            s_cheat_close_frames = 2;
        }
        for (row = 0; row < 2; ++row) if (y >= 230 + row * 65 && y <= 267 + row * 65) {
            s_cheat_row = row;
            if (x >= 435 && x <= 470) change_cheat_rate(row, -1);
            if (x >= 550 && x <= 585) change_cheat_rate(row, 1);
        }
    }
    return 1;
}

static uint32_t handle_cheat_input(const struct PAL3MenuNavInput *input)
{
    uint8_t directions;
    if (!PAL3Mod_Enabled(PAL3_MOD_CHEATS)) { s_cheat_open = 0; return 0; }
    if (!s_cheat_open) return 0;
    if (!system_page_visible()) { s_cheat_open = 0; return 0; }
    directions = direction_state(input);
    /* 页面覆盖存档列表时清空底层鼠标键，避免同一次确认点到存档槽。 */
    g_input.KeyInfo[0].PressTime = g_input.KeyInfo[0].Data = 0;
    if (input->controller_active) {
        if (direction_event(directions, 0, PAL3_MENU_NAV_UP, input->now_ms)) s_cheat_row = 0;
        if (direction_event(directions, 1, PAL3_MENU_NAV_DOWN, input->now_ms)) s_cheat_row = 1;
        if (direction_event(directions, 2, PAL3_MENU_NAV_LEFT, input->now_ms)) change_cheat_rate(s_cheat_row, -1);
        if (direction_event(directions, 3, PAL3_MENU_NAV_RIGHT, input->now_ms)) change_cheat_rate(s_cheat_row, 1);
        if (input->pressed & XINPUT_A) { s_cheat_open = 0; s_cheat_close_frames = 2; }
    }
    s_previous_directions = directions;
    return PAL3_MENU_NAV_MODAL | PAL3_MENU_NAV_CONFIRM | PAL3_MENU_NAV_CANCEL |
        PAL3_MENU_NAV_CATEGORY | directions;
}

uint32_t PAL3MenuNav_CheatTick(const struct PAL3MenuNavInput *input)
{
    if (!input || !PAL3Mod_Enabled(PAL3_MOD_CHEATS)) return 0;
    if ((input->pressed & XINPUT_X) && PAL3MenuNav_ToggleCheat())
        return PAL3_MENU_NAV_MODAL | PAL3_MENU_NAV_CONFIRM | PAL3_MENU_NAV_CANCEL;
    return handle_cheat_input(input);
}

static void scroll_state_list(struct UIListBox *list,
    const struct PAL3MenuNavInput *input)
{
    int direction = input->right_stick_y > PAL3_MENU_NAV_DEADZONE ? -1 :
        input->right_stick_y < -PAL3_MENU_NAV_DEADZONE ? 1 : 0;
    int fresh = s_scroll_list != list || s_scroll_direction != direction;
    s_scroll_list = list;
    s_scroll_direction = direction;
    if (!direction || !input->controller_active) return;
    if (!fresh && !time_reached(input->now_ms, s_scroll_repeat_at)) return;
    s_scroll_repeat_at = input->now_ms + (fresh ?
        PAL3_MENU_NAV_INITIAL_REPEAT_MS : PAL3_MENU_NAV_REPEAT_MS);
    /* 复用原版滚动条的上下按钮，范围、首行和父页面通知由游戏同步更新。 */
    THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
        (uintptr_t)GetModuleHandleA(NULL) +
            (direction < 0 ? 0x00041D50u : 0x00041DC0u),
        void, struct UIScrollBar *), &list->m_scrollbar);
}

static uint32_t handle_category_input(const struct PAL3MenuNavInput *input,
    int active, uint8_t directions)
{
    struct category_button buttons[PAL3_MENU_NAV_MAX_CATEGORIES];
    uint32_t count;
    int current;
    int left_held = (input->held & XINPUT_LEFT_SHOULDER) != 0;
    int right_held = (input->held & XINPUT_RIGHT_SHOULDER) != 0;
    int category_direction = 0;
    int pressed_action = input->pressed & (XINPUT_A | XINPUT_B);

    if (!active || PAL3_s_gamestate != GAME_UI ||
        g_gamefrm.m_curfrmid != FRM_S_STATE) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] category skipped active=%d state=%d frame=%d",
            active, PAL3_s_gamestate, g_gamefrm.m_curfrmid);
        s_category_frame = NULL;
        s_category_previous_left = 0;
        s_category_left_used = 0;
        s_category_left_directions = 0;
        return 0;
    }

    if (s_category_frame != g_gamefrm.m_curfrm) {
        s_category_frame = g_gamefrm.m_curfrm;
        s_category_previous_left = (uint8_t)left_held;
        s_category_left_used = (uint8_t)(left_held || input->mouse_combo_used);
        s_category_left_directions = 0;
    }
    if (left_held) {
        if (input->mouse_combo_used || directions || pressed_action || right_held)
            s_category_left_used = 1;
        s_category_left_directions |= directions;
    }

    /* R 按下选右侧下一类；L 单按释放选左侧上一类。 */
    if ((input->pressed & XINPUT_RIGHT_SHOULDER) && !left_held &&
        !input->mouse_mode)
        category_direction = 1;
    else if (PAL3MenuNav_ShouldSwitchCategoryOnLRelease(
            s_category_previous_left, left_held, s_category_left_used,
            s_category_left_directions))
        category_direction = -1;

    s_category_previous_left = (uint8_t)left_held;
    if (category_direction == 0) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] category no-action held=0x%04x pressed=0x%04x mouse=%d left_prev=%d left_held=%d left_used=%d dirs=0x%02x",
            input->held, input->pressed, input->mouse_mode,
            s_category_previous_left, left_held, s_category_left_used,
            s_category_left_directions);
        if (!left_held) {
            s_category_left_used = 0;
            s_category_left_directions = 0;
        }
        return 0;
    }

    count = collect_active_categories(buttons);
    current = find_current_category(buttons, count);
    PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] category action=%d count=%lu current=%d page_root=%p",
        category_direction, (unsigned long)count, current,
        current_state_page_root());
    if (current < 0 || !click_category(buttons, count, current,
            category_direction)) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] category failed count=%lu current=%d",
            (unsigned long)count, current);
        return 0;
    }
    s_category_left_used = 0;
    s_category_left_directions = 0;
    return PAL3_MENU_NAV_CATEGORY;
}

static uint32_t handle_state_subcategory_input(uint8_t directions,
    const struct PAL3MenuNavInput *input)
{
    static const uint32_t item_ids[] = {
        0xA00403u, 0xA00404u, 0xA00405u, 0xA00406u, 0xA00407u, 0xA00408u
    };
    static const uint32_t equip_ids[] = {
        0xA00327u, 0xA00328u, 0xA00329u, 0xA00330u, 0xA00331u
    };
    static const uint32_t magic_ids[] = {
        0xA00225u, 0xA00226u, 0xA00227u, 0xA00228u,
        0xA00229u, 0xA00230u, 0xA00231u, 0xA00232u
    };
    const uint32_t *ids;
    uint32_t page, count, i, j, target;
    struct UIWnd *root = current_state_page_root();
    struct UIWnd *tabs[8] = {0};
    uint8_t enabled[8];
    uint8_t horizontal = directions & (PAL3_MENU_NAV_LEFT | PAL3_MENU_NAV_RIGHT);
    int current = -1;
    int direction = (horizontal & PAL3_MENU_NAV_LEFT) ? -1 : 1;

    if (!horizontal || !root || !read_current_state_page(&page) ||
        !PAL3MenuNav_ShouldNavigate(
            (input->held & XINPUT_LEFT_SHOULDER) != 0, input->mouse_mode)) return 0;
    if (page == 1) { ids = item_ids; count = 6; }
    else if (page == 2) { ids = equip_ids; count = 5; }
    else if (page == 3) { ids = magic_ids; count = 8; }
    else return 0;
    if (!root->m_childs.m_pData || root->m_childs.m_nSize <= 0 ||
        root->m_childs.m_nSize > PAL3_MENU_NAV_MAX_CHILDREN) return 0;
    for (i = 0; i < (uint32_t)root->m_childs.m_nSize; ++i) {
        struct UIWnd *child = (struct UIWnd *)root->m_childs.m_pData[i];
        if (!child || child->m_pfather != root || !child->m_bcreateok ||
            !child->m_bvisible || child->vfptr != (struct UIWndVtbl *)
                ((uintptr_t)GetModuleHandleA(NULL) +
                    PAL3_MENU_NAV_CATEGORY_BUTTON_VTABLE_RVA)) continue;
        for (j = 0; j < count; ++j)
            if (child->m_id == ids[j]) tabs[j] = child;
    }
    /* 原版用禁用当前标签表示选中；有缺失或多个禁用标签时不猜选中项。 */
    for (i = 0; i < count; ++i) {
        if (!tabs[i]) return 0;
        enabled[i] = !!tabs[i]->m_benable;
        if (!enabled[i]) {
            if (current >= 0) return 0;
            current = (int)i;
        }
    }
    if (current < 0 || !root->vfptr || !root->vfptr->OnMessage) return 0;
    if (!direction_event(directions, direction < 0 ? 2 : 3,
            direction < 0 ? PAL3_MENU_NAV_LEFT : PAL3_MENU_NAV_RIGHT,
            input->now_ms)) return horizontal;
    target = PAL3MenuNav_NextCategory(enabled, count, (uint32_t)current, direction);
    if (target < count && target != (uint32_t)current) {
        THISCALL_WRAPPER(root->vfptr->OnMessage, root,
            PAL3_MENU_NAV_CATEGORY_HOVER_MESSAGE, ids[target], 0);
        THISCALL_WRAPPER(root->vfptr->OnMessage, root,
            PAL3_MENU_NAV_CATEGORY_MOUSE_DOWN_MESSAGE, ids[target], 0);
        THISCALL_WRAPPER(root->vfptr->OnMessage, root,
            PAL3_MENU_NAV_BUTTON_MESSAGE, ids[target], 0);
    }
    return horizontal;
}

static void handle_state_role_input(const struct PAL3MenuNavInput *input)
{
    struct UIWnd *root = current_state_page_root();
    struct UIWnd *portraits[5] = {0};
    uint32_t page, id_base, selected_offset, i;
    int current, target, direction;

    if (!(input->pressed & (XINPUT_X | XINPUT_Y)) || !root ||
        !read_current_state_page(&page) || !PAL3MenuNav_ShouldNavigate(
            (input->held & XINPUT_LEFT_SHOULDER) != 0, input->mouse_mode)) return;
    if (page == 1) {
        /* 进入使用对象选择后，方向和 A/B 交给单独的处理，不能重新浏览角色。 */
        if (*((uint8_t *)root + 0x28Du)) return;
        id_base = 0xAB00AB00u; selected_offset = 0x274u;
    } else if (page == 2) {
        id_base = 0xAB0CAB00u; selected_offset = 0x3489Cu;
    } else if (page == 3) {
        if (*((uint8_t *)root + 0x38Du)) return;
        id_base = 0xAC0CAB00u; selected_offset = 0x323D8u;
    } else return;
    memcpy(&current, (uint8_t *)root + selected_offset, sizeof(current));
    if (current < 0 || current >= 5 || !root->vfptr ||
        !root->vfptr->OnMessage || !root->m_childs.m_pData ||
        root->m_childs.m_nSize <= 0 ||
        root->m_childs.m_nSize > PAL3_MENU_NAV_MAX_CHILDREN) return;
    for (i = 0; i < (uint32_t)root->m_childs.m_nSize; ++i) {
        struct UIWnd *child = root->m_childs.m_pData[i];
        if (child && child->m_pfather == root && child->m_bcreateok &&
            child->m_bvisible && child->m_benable &&
            child->m_id >= id_base && child->m_id < id_base + 5 &&
            child->vfptr == (struct UIWndVtbl *)((uintptr_t)GetModuleHandleA(NULL) +
                PAL3_MENU_NAV_CATEGORY_BUTTON_VTABLE_RVA))
            portraits[child->m_id - id_base] = child;
    }
    /* Switch 实体 X/Y 分别对应 XInput Y/X，按一次只切换一个在场角色。 */
    direction = (input->pressed & XINPUT_Y) ? 1 : -1;
    for (i = 1; i < 5; ++i) {
        target = (current + direction * (int)i + 5) % 5;
        if (!portraits[target]) continue;
        THISCALL_WRAPPER(root->vfptr->OnMessage, root,
            PAL3_MENU_NAV_BUTTON_MESSAGE, portraits[target]->m_id, 0);
        s_selected = NULL;
        s_selection_engaged = 0;
        return;
    }
}

static uint32_t handle_state_target_input(const struct PAL3MenuNavInput *input,
    uint8_t directions)
{
    struct UIWnd *root = current_state_page_root();
    uint32_t page, id_base, selected_offset, present_offset, marker_offset, i;
    int current;

    if (!root || !read_current_state_page(&page)) {
        s_state_target_root = NULL;
        return 0;
    }
    if (page == 1 && *((uint8_t *)root + 0x28Du)) {
        id_base = 0xAB00AB00u; selected_offset = 0x274u; present_offset = 0x26Du;
        marker_offset = 0x2746Cu;
    } else if (page == 3 && *((uint8_t *)root + 0x38Du) &&
            *((uint8_t *)root + 0x38Eu)) {
        id_base = 0xAC0CAB00u; selected_offset = 0x323D8u; present_offset = 0x36Du;
        marker_offset = 0x350ECu;
    } else {
        s_state_target_root = NULL;
        return 0;
    }
    if (!input->controller_active || !PAL3MenuNav_ShouldNavigate(
            (input->held & XINPUT_LEFT_SHOULDER) != 0, input->mouse_mode)) return 0;
    memcpy(&current, (uint8_t *)root + selected_offset, sizeof(current));
    if (current < 0 || current >= 5 || !root->vfptr || !root->vfptr->OnMessage)
        return 0;
    if (s_state_target_root != root) {
        struct UIWnd *marker = (struct UIWnd *)((uint8_t *)root +
            marker_offset + current * 0x131Cu);
        s_state_target_root = root;
        /* 初次打开时原版可能只标出可用队友，角色字段却还停在满精的旧角色。 */
        if (!marker->m_bvisible) {
            for (i = 0; i < 5; ++i) {
                marker = (struct UIWnd *)((uint8_t *)root + marker_offset + i * 0x131Cu);
                if (!*((uint8_t *)root + present_offset + i) || !marker->m_bvisible)
                    continue;
                current = (int)i;
                memcpy((uint8_t *)root + selected_offset, &current, sizeof(current));
                THISCALL_WRAPPER(root->vfptr->OnMessage, root,
                    PAL3_MENU_NAV_BUTTON_MESSAGE, id_base + current, 0);
                break;
            }
        }
    }
    s_selected = NULL;
    s_selection_engaged = 0;
    if (input->pressed & XINPUT_A) {
        /* 原版取消消息会恢复列表并移除目标标记，下一次 B 才返回状态页。 */
        THISCALL_WRAPPER(root->vfptr->OnMessage, root, 0x3F2u, 0, 0);
    } else {
        for (i = 0; i < 4; ++i) {
            uint8_t bit = (uint8_t)(1u << i);
            int step, direction = (i == 0 || i == 2) ? -1 : 1;
            if (!(directions & bit) || !direction_event(directions, i, bit, input->now_ms))
                continue;
            for (step = 1; step < 5; ++step) {
                int target = (current + direction * step + 5) % 5;
                if (!*((uint8_t *)root + present_offset + target)) continue;
                current = target;
                /* 与原版角色方向键一致，只更新输入目标，再由原版检验并显示标记。 */
                memcpy((uint8_t *)root + selected_offset, &current, sizeof(current));
                THISCALL_WRAPPER(root->vfptr->OnMessage, root,
                    PAL3_MENU_NAV_BUTTON_MESSAGE, id_base + current, 0);
                break;
            }
            break;
        }
        /* 3E9 只选中并显示目标，3FC 才执行原版用药或仙术，不能混用。 */
        if (input->pressed & XINPUT_B)
            THISCALL_WRAPPER(root->vfptr->OnMessage, root,
                PAL3_MENU_NAV_CATEGORY_MOUSE_DOWN_MESSAGE, id_base + current, 0);
    }
    s_previous_directions = directions;
    return PAL3_MENU_NAV_MODAL | directions | PAL3_MENU_NAV_CONFIRM | PAL3_MENU_NAV_CANCEL;
}

uint32_t PAL3MenuNav_Tick(const struct PAL3MenuNavInput *input)
{
    struct UIWnd *cover_page;
    struct menu_button buttons[PAL3_MENU_NAV_MAX_BUTTONS];
    uint32_t count;
    uint32_t consumed = 0;
    uint8_t directions;
    int active;
    int just_initialized = 0;
    int shoulder_held;
    uint32_t scene_dialog_action;

    if (!input) return 0;
    scene_dialog_action = handle_cheat_input(input);
    if (scene_dialog_action) return scene_dialog_action;
#if PAL3_MENU_TREE_DIAG
    s_trace_menu_edge = (input->pressed &
        (XINPUT_LEFT_SHOULDER | XINPUT_RIGHT_SHOULDER | XINPUT_A | XINPUT_B |
         XINPUT_DPAD_UP | XINPUT_DPAD_DOWN | XINPUT_DPAD_LEFT | XINPUT_DPAD_RIGHT)) != 0 ||
        (s_category_previous_left && !(input->held & XINPUT_LEFT_SHOULDER));
    if (s_trace_menu_edge)
        plog("[PAL3MenuEdge] input state=%d frame=%d curfrm=%p sysfrm=%p held=0x%04x pressed=0x%04x mouse=%d stick=%d,%d",
            PAL3_s_gamestate, g_gamefrm.m_curfrmid, g_gamefrm.m_curfrm,
            g_gamefrm.m_sysfrm, input->held, input->pressed,
            input->mouse_mode, input->stick_x, input->stick_y);
#endif
    scene_dialog_action = handle_scene_dialog(input);
    if (scene_dialog_action) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] early scene-dialog action=0x%08lx",
            (unsigned long)scene_dialog_action);
        return scene_dialog_action;
    }
    active = g_gamefrm.m_curfrm != NULL &&
        (PAL3_s_gamestate == GAME_UI || active_chat_rest_root() != NULL);
    if (!active || s_frame != g_gamefrm.m_curfrm) {
        s_frame = active ? g_gamefrm.m_curfrm : NULL;
        s_selected = NULL;
        s_selection_engaged = 0;
        s_previous_directions = 0;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
    }
    if (!active) {
        s_category_frame = NULL;
        s_category_previous_left = 0;
        s_category_left_used = 0;
        s_category_left_directions = 0;
        s_mouse_mode = input->mouse_mode;
        return 0;
    }

    cover_page = active_cover_subpage();
    if (cover_page && cover_subpage_state() == PAL3_MENU_NAV_COVER_SUBPAGE_STATE) {
        s_frame = g_gamefrm.m_curfrm;
        s_selected = NULL;
        s_selection_engaged = 0;
        s_category_frame = NULL;
        s_category_previous_left = 0;
        s_category_left_used = 0;
        s_category_left_directions = 0;
        s_mouse_mode = input->mouse_mode;
        return handle_cover_subpage_input(cover_page, input);
    }

    if (cover_page && (input->pressed & XINPUT_A) && !input->mouse_mode) {
        /* 使用各页实际的关闭按钮消息，让原版恢复标题状态与音乐。 */
        THISCALL_WRAPPER(cover_page->vfptr->OnMessage, cover_page,
            PAL3_MENU_NAV_BUTTON_MESSAGE, cover_subpage_state() == 1 ? 0xB1u : 0xC1u, 0);
        s_selected = NULL;
        s_selection_engaged = 0;
        return PAL3_MENU_NAV_CANCEL;
    }

    scene_dialog_action = handle_state_page_dialog(input);
    if (scene_dialog_action) {
        PAL3_MENU_EDGE_LOG("[PAL3MenuEdge] early state-dialog action=0x%08lx",
            (unsigned long)scene_dialog_action);
        return scene_dialog_action;
    }
    directions = direction_state(input);
    scene_dialog_action = handle_state_target_input(input, directions);
    if (scene_dialog_action) return scene_dialog_action;
    consumed |= handle_category_input(input, active, directions);
    if (consumed & PAL3_MENU_NAV_CATEGORY) {
        s_previous_directions = 0;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
        s_mouse_mode = input->mouse_mode;
        return consumed;
    }

    if (state_lineup_root()) {
        uint32_t lineup_action = handle_state_lineup(input, directions);
        if (!(input->pressed & XINPUT_A) || (lineup_action & PAL3_MENU_NAV_CANCEL) ||
            input->mouse_mode || (input->held & XINPUT_LEFT_SHOULDER))
            return consumed | lineup_action;
    } else {
        s_state_lineup = NULL;
        s_state_lineup_focus = s_state_lineup_click = NULL;
        s_state_lineup_override = 0;
    }

    /* 状态页的 B 先退回页 0，页 0 才关闭；避免列表页直接退出整层。 */
    if (PAL3_s_gamestate == GAME_UI && g_gamefrm.m_curfrmid == FRM_S_STATE &&
        (input->pressed & XINPUT_A) &&
        PAL3MenuNav_ShouldNavigate(
            (input->held & XINPUT_LEFT_SHOULDER) != 0, input->mouse_mode)) {
        uint32_t page_index;
        if (read_current_state_page(&page_index)) {
            if (PAL3MenuNav_ShouldReturnToStateRoot(page_index)) {
                set_state_page(0);
                s_category_frame = NULL;
                s_category_previous_left = 0;
                s_category_left_used = 0;
                s_category_left_directions = 0;
                return PAL3_MENU_NAV_CANCEL;
            }
            if (page_index == 0 && activate_state_close_button()) {
                s_category_frame = NULL;
                s_category_previous_left = 0;
                s_category_left_used = 0;
                s_category_left_directions = 0;
                return PAL3_MENU_NAV_CANCEL;
            }
        }
    }

    /* 临时鼠标模式保留给鼠标输入层处理，不再驱动菜单焦点或确认。 */
    if (input->mouse_mode) {
        consumed |= directions;
        if (input->pressed & XINPUT_B) consumed |= PAL3_MENU_NAV_CONFIRM;
        s_previous_directions = 0;
        s_mouse_mode = 1;
        memset(s_repeat_at, 0, sizeof(s_repeat_at));
        return consumed;
    }
    shoulder_held = !PAL3MenuNav_ShouldNavigate(
        (input->held & XINPUT_LEFT_SHOULDER) != 0, input->mouse_mode);

    if (active_chat_rest_root()) {
        /* 服务菜单中的方向不能同时推动场景人物；B 复用“退出”按钮。 */
        consumed |= directions;
        if (!shoulder_held && (input->pressed & XINPUT_A)) {
            struct UIWnd *button = &g_gamefrm.m_ChatRest.m_Exit.baseclass;
            struct UIWnd *root = active_chat_rest_root();
            if (ancestors_available(button, root) && root->vfptr &&
                root->vfptr->OnMessage)
                THISCALL_WRAPPER(root->vfptr->OnMessage, root,
                    PAL3_MENU_NAV_BUTTON_MESSAGE, button->m_id, 0);
            return consumed | PAL3_MENU_NAV_CANCEL;
        }
    }

    handle_state_role_input(input);
    consumed |= handle_state_subcategory_input(directions, input);
    if (!shoulder_held) {
        uint32_t page_index;
        struct UIWnd *root = current_state_page_root();
        if (root && read_current_state_page(&page_index) && page_index == 7) {
            uint32_t archive = handle_archive_navigation(root, 1, input, directions);
            if (!(input->pressed & XINPUT_B) || s_system_focus ||
                (archive & PAL3_MENU_NAV_CONFIRM))
                return consumed | archive;
        }
    }
    if (!shoulder_held && (input->pressed & XINPUT_B)) {
        uint32_t page_index;
        struct UIWnd *page_root = current_state_page_root();
        if (page_root && read_current_state_page(&page_index) && page_index == 7) {
            int up = g_input.KeyInfo[4].PressTime;
            int down = g_input.KeyInfo[5].PressTime;
            int confirm = g_input.KeyInfo[7].PressTime;
            int load = g_input.KeyInfo[9].PressTime;
            int save = g_input.KeyInfo[10].PressTime;
            /* 原版系统页确认会检查当前存档和读写模式，再打开确认弹窗。 */
            g_input.KeyInfo[4].PressTime = g_input.KeyInfo[5].PressTime = 0;
            g_input.KeyInfo[9].PressTime = g_input.KeyInfo[10].PressTime = 0;
            g_input.KeyInfo[7].PressTime = 1;
            THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
                (uintptr_t)GetModuleHandleA(NULL) + 0x000B2D20u,
                void, struct UIWnd *), page_root);
            g_input.KeyInfo[4].PressTime = up;
            g_input.KeyInfo[5].PressTime = down;
            g_input.KeyInfo[7].PressTime = confirm;
            g_input.KeyInfo[9].PressTime = load;
            g_input.KeyInfo[10].PressTime = save;
            return consumed | PAL3_MENU_NAV_CONFIRM;
        }
    }
    if (!shoulder_held && PAL3_s_gamestate == GAME_UI &&
        g_gamefrm.m_curfrmid == FRM_S_STATE &&
        find_active_state_page_list()) {
        struct UIListBox *list = find_active_state_page_list();
        int up_event = direction_event(directions, 0, PAL3_MENU_NAV_UP, input->now_ms);
        int down_event = direction_event(directions, 1, PAL3_MENU_NAV_DOWN, input->now_ms);
        scroll_state_list(list, input);
        /* 长推摇杆按统一间隔调用原版列表，不再依赖键盘首次按下那一帧。 */
        if (list->m_hasKeyCtrl && (up_event || down_event || (input->pressed & XINPUT_B))) {
            int up = g_input.KeyInfo[4].PressTime;
            int down = g_input.KeyInfo[5].PressTime;
            int confirm = g_input.KeyInfo[7].PressTime;
            g_input.KeyInfo[4].PressTime = up_event;
            g_input.KeyInfo[5].PressTime = down_event;
            g_input.KeyInfo[7].PressTime = (input->pressed & XINPUT_B) != 0;
            THISCALL_WRAPPER(MAKE_THISCALL_FUNCPTR(
                (uintptr_t)GetModuleHandleA(NULL) + PAL3_MENU_NAV_LIST_KEY_HANDLER_RVA,
                void, struct UIListBox *), list);
            g_input.KeyInfo[4].PressTime = up;
            g_input.KeyInfo[5].PressTime = down;
            g_input.KeyInfo[7].PressTime = confirm;
            if (input->pressed & XINPUT_B) consumed |= PAL3_MENU_NAV_CONFIRM;
        }
        if (list->m_hasKeyCtrl)
            consumed |= directions & (PAL3_MENU_NAV_UP | PAL3_MENU_NAV_DOWN);
        s_selected = NULL;
        s_selection_engaged = 0;
        s_previous_directions = directions;
        s_mouse_mode = input->mouse_mode;
        return consumed;
    }

    s_scroll_list = NULL;
    s_scroll_direction = 0;

    count = collect_active_buttons(buttons);
    if (!count) {
        s_selected = NULL;
        s_selection_engaged = 0;
        s_previous_directions = 0;
        s_mouse_mode = input->mouse_mode;
        return consumed; /* 没有已识别按钮时，让原版列表继续接收未消费的方向键。 */
    }
    if (find_button(buttons, count, s_selected) < 0) {
        s_selected = NULL;
        s_selection_engaged = 0;
    }

    /* 页面首次出现时先给出可见焦点；同一帧的 A 只建立焦点，不直接误点。 */
    if (!s_selected) {
        s_selected = buttons[initial_button(buttons, count)].button;
        s_selection_engaged = 1;
        just_initialized = 1;
    }

    /* L+十字左右调灵敏度时，方向不再同时移动菜单焦点。 */
    if (shoulder_held &&
        (input->held & (XINPUT_DPAD_LEFT | XINPUT_DPAD_RIGHT))) {
        uint8_t speed_dirs = directions & (PAL3_MENU_NAV_LEFT | PAL3_MENU_NAV_RIGHT);
        directions &= (uint8_t)~(PAL3_MENU_NAV_LEFT | PAL3_MENU_NAV_RIGHT);
        consumed |= speed_dirs;
    }
    if (shoulder_held) {
        consumed |= directions;
        if (input->pressed & XINPUT_B) consumed |= PAL3_MENU_NAV_CONFIRM;
    }
    /* 重复间隔内也拦住原版注入，避免同一输入同时驱动两套焦点。 */
    if (!shoulder_held && (directions & PAL3_MENU_NAV_UP)) {
        if (has_direction_neighbor(0, -1, buttons, count)) {
            consumed |= PAL3_MENU_NAV_UP;
            if (direction_event(directions, 0, PAL3_MENU_NAV_UP, input->now_ms))
                select_direction(0, -1, buttons, count);
        }
    }
    if (!shoulder_held && (directions & PAL3_MENU_NAV_DOWN)) {
        if (has_direction_neighbor(0, 1, buttons, count)) {
            consumed |= PAL3_MENU_NAV_DOWN;
            if (direction_event(directions, 1, PAL3_MENU_NAV_DOWN, input->now_ms))
                select_direction(0, 1, buttons, count);
        }
    }
    if (!shoulder_held && (directions & PAL3_MENU_NAV_LEFT)) {
        if (has_direction_neighbor(-1, 0, buttons, count)) {
            consumed |= PAL3_MENU_NAV_LEFT;
            if (direction_event(directions, 2, PAL3_MENU_NAV_LEFT, input->now_ms))
                select_direction(-1, 0, buttons, count);
        }
    }
    if (!shoulder_held && (directions & PAL3_MENU_NAV_RIGHT)) {
        if (has_direction_neighbor(1, 0, buttons, count)) {
            consumed |= PAL3_MENU_NAV_RIGHT;
            if (direction_event(directions, 3, PAL3_MENU_NAV_RIGHT, input->now_ms))
                select_direction(1, 0, buttons, count);
        }
    }
    s_previous_directions = shoulder_held ? 0 : directions;

    /* 确认只吃按下沿；取消保留给上层 Escape 边沿路径。 */
    if ((input->pressed & XINPUT_B) && !shoulder_held && !just_initialized) {
        if (activate_selected(buttons, count)) consumed |= PAL3_MENU_NAV_CONFIRM;
    } else if ((input->pressed & XINPUT_B) && !shoulder_held && just_initialized)
        consumed |= PAL3_MENU_NAV_CONFIRM;
    s_mouse_mode = input->mouse_mode;
    return consumed;
}

void PAL3MenuNav_RenderChoice(struct UIWnd *window)
{
    const RECT *r;
    const uint32_t color = 0xFFFFFF00u;
    void (__cdecl *draw)(int, int, int, int, const uint32_t *);
    if (!s_selection_engaged || s_mouse_mode || !s_selected ||
        window != &s_selected->baseclass || !is_state_ai_choice(window) ||
        !window->m_bvisible || !window->m_benable)
        return;
    r = &window->m_rect;
    draw = (void (__cdecl *)(int, int, int, int, const uint32_t *))
        ((uintptr_t)GetModuleHandleA(NULL) + 0x00140070u);
    /* 在原控件 Render 中绘制，沿用游戏当前缩放，不能套用 UIButton 状态字段。 */
    draw(r->left, r->top, r->right, r->top + 2, &color);
    draw(r->left, r->bottom - 2, r->right, r->bottom, &color);
    draw(r->left, r->top, r->left + 2, r->bottom, &color);
    draw(r->right - 2, r->top, r->right, r->bottom, &color);
}

static void draw_gold_pointer(float x, float y, float scale)
{
    struct pointer_vertex { float x, y, z, rhw; DWORD color; } vertices[48];
    const float edge[4][2] = {{0,-8}, {18,0}, {0,8}, {3,0}};
    const float fringe[4][2] = {{-1.1f,-9.5f}, {20,0}, {-1.1f,9.5f}, {1.5f,0}};
    const float fill[4][2] = {{1.6f,-5.5f}, {14.5f,0}, {1.6f,5.5f}, {4.3f,0}};
    IDirect3DDevice9 *device = GB_GfxMgr->m_pd3dDevice;
    IDirect3DStateBlock9 *saved = NULL;
    D3DVIEWPORT9 viewport = {0, 0, PAL3_s_drvinfo.width, PAL3_s_drvinfo.height, 0.0f, 1.0f};
    int count = 0, i;
#define PUT_POINTER(px, py, c) \
    vertices[count++] = (struct pointer_vertex){x + (px) * scale - 0.5f, \
        y + (py) * scale - 0.5f, 0.0f, 1.0f, (c)}
    if (!device || FAILED(IDirect3DDevice9_CreateStateBlock(device, D3DSBT_ALL, &saved))) return;
    /* 半透明边缘消除台阶，浅金上面、暖金下面组成小箭头；不再画大块荧光黄色。 */
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        PUT_POINTER(edge[i][0], edge[i][1], 0xFF34281Eu);
        PUT_POINTER(fringe[i][0], fringe[i][1], 0x0034281Eu);
        PUT_POINTER(edge[j][0], edge[j][1], 0xFF34281Eu);
        PUT_POINTER(fringe[i][0], fringe[i][1], 0x0034281Eu);
        PUT_POINTER(fringe[j][0], fringe[j][1], 0x0034281Eu);
        PUT_POINTER(edge[j][0], edge[j][1], 0xFF34281Eu);
    }
    for (i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        PUT_POINTER(7, 0, 0xFF34281Eu);
        PUT_POINTER(edge[i][0], edge[i][1], 0xFF34281Eu);
        PUT_POINTER(edge[j][0], edge[j][1], 0xFF34281Eu);
        PUT_POINTER(7, 0, 0xFFF2CB72u);
        PUT_POINTER(fill[i][0], fill[i][1], i < 2 ? 0xFFFFE9ADu : 0xFFC98D36u);
        PUT_POINTER(fill[j][0], fill[j][1], j < 2 ? 0xFFFFE9ADu : 0xFFC98D36u);
    }
#undef PUT_POINTER
    /* 屏幕坐标绘制并恢复全部状态，避免原 UI 矩阵、纹理或着色器影响箭头和下一帧。 */
    IDirect3DDevice9_SetVertexShader(device, NULL);
    IDirect3DDevice9_SetPixelShader(device, NULL);
    IDirect3DDevice9_SetTexture(device, 0, NULL);
    IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    /* 阵法模型可能留下局部视口和阴影蒙版，屏幕箭头须使用完整画面。 */
    IDirect3DDevice9_SetViewport(device, &viewport);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_COLORWRITEENABLE, 0xFu);
    IDirect3DDevice9_SetRenderState(device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    {
        HRESULT result = IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLELIST,
            count / 3, vertices, sizeof(vertices[0]));
#if PAL3_MENU_TREE_DIAG
        static int logged;
        if (!logged) {
            logged = 1;
            plog("[PAL3GoldPointer] x=%.1f y=%.1f scale=%.2f viewport=%lux%lu result=%08lx",
                x, y, scale, viewport.Width, viewport.Height, (unsigned long)result);
        }
#else
        (void)result;
#endif
    }
    IDirect3DStateBlock9_Apply(saved);
    IDirect3DStateBlock9_Release(saved);
}

static void cheat_box(int x0, int y0, int x1, int y1, uint32_t color)
{
    double scale = (game_frect_43.right - game_frect_43.left) / 800.0;
    D3DRECT rect = {(LONG)(game_frect_43.left + x0 * scale),
        (LONG)(game_frect_43.top + y0 * scale),
        (LONG)(game_frect_43.left + x1 * scale),
        (LONG)(game_frect_43.top + y1 * scale)};
    IDirect3DDevice9_Clear(GB_GfxMgr->m_pd3dDevice, 1, &rect,
        D3DCLEAR_TARGET, color, 1.0f, 0);
}

static void cheat_text(const wchar_t *text, int x, int y, uint32_t color)
{
    double scale = (game_frect_43.right - game_frect_43.left) / 800.0;
    print_wstring(print_wstring_getfontid(PRINTWSTR_U16, scale), text,
        (int)(game_frect_43.left + x * scale), (int)(game_frect_43.top + y * scale), color);
}

static void cheat_button(int x0, int y0, int x1, int y1, int selected)
{
    uint32_t edge = selected ? 0xFF476C86u : 0xFF647B9Du;
    uint32_t fill = selected ? 0xFFE1EBC4u : 0xFFC0DED7u;
    /* 沿用原版的蓝边、浅玉色与切角，不在分类栏上另贴纯色方块。 */
    cheat_box(x0 + 3, y0, x1 - 3, y1, edge);
    cheat_box(x0, y0 + 3, x1, y1 - 3, edge);
    cheat_box(x0 + 3, y0 + 2, x1 - 3, y1 - 2, fill);
    cheat_box(x0 + 2, y0 + 4, x1 - 2, y1 - 4, fill);
    cheat_box(x0 + 4, y0 + 2, x1 - 4, y0 + 3, 0xFFF0F3E1u);
}

static void render_cheat_page(void)
{
    int row;
    wchar_t value[20];
    const wchar_t *hint;
    if (!system_page_visible()) return;
    if (s_cheat_open) {
        cheat_box(215, 165, 625, 415, 0xFF526A92u);
        cheat_box(217, 167, 623, 413, 0xFF9BBEC5u);
        cheat_box(220, 170, 620, 410, 0xFFBAD8CEu);
        cheat_box(222, 172, 618, 209, 0xFF719CA6u);
        cheat_box(240, 211, 600, 212, 0xFF829DA9u);
        for (row = 0; row < 2; ++row) {
            if (s_cheat_row == row) cheat_box(240, 225 + row * 65, 600, 272 + row * 65, 0xFFDCE8C7u);
            cheat_button(435, 230 + row * 65, 470, 267 + row * 65, s_cheat_row == row);
            cheat_button(550, 230 + row * 65, 585, 267 + row * 65, s_cheat_row == row);
        }
        cheat_button(350, 365, 450, 398, 0);
    }
    print_wstring_begin();
    if (s_cheat_open) {
        cheat_text(L"金手指", 385, 182, 0xFFF3F1D8u);
        for (row = 0; row < 2; ++row) {
            swprintf(value, sizeof(value) / sizeof(value[0]), L"%d 倍", PAL3Gamepad_GetExperienceRate(row));
            cheat_text(row ? L"技能熟练度" : L"人物经验", 265, 240 + row * 65, 0xFF203A42u);
            cheat_text(L"－", 444, 240 + row * 65, 0xFF203A59u);
            cheat_text(value, 488, 240 + row * 65, 0xFF304D71u);
            cheat_text(L"＋", 559, 240 + row * 65, 0xFF203A59u);
        }
        cheat_text(L"返回", 383, 374, 0xFF203A59u);
        cheat_text(L"上下选择　左右调整　B 返回", 280, 342, 0xFF426168u);
    } else {
        hint = PAL3Mod_SystemHint();
        cheat_text(hint, 61, 513, 0xFF18354Du);
        cheat_text(hint, 60, 512, 0xFFE1EBDCu);
    }
    print_wstring_end();
}

void PAL3MenuNav_Render(void)
{
    struct menu_button buttons[PAL3_MENU_NAV_MAX_BUTTONS];
    uint32_t count;
    int selected;
    struct UIButton *button;
    enum UIButton_UIBUTTON_STATE previous;

    render_cheat_page();
    if (s_cheat_open || !PAL3Mod_Enabled(PAL3_MOD_CONTROLLER)) return;

    if (PAL3_s_gamestate == GAME_SCENE &&
        g_gamefrm.m_seldlg.baseclass.baseclass.m_bvisible && !s_mouse_mode) {
        struct UIListBox *list = &g_gamefrm.m_seldlg.m_list;
        int row = list->m_onsel - list->m_firstshow;
        double scale = (game_frect_43.right - game_frect_43.left) / 800.0;
        if (row >= 0 && list->m_itemheight > 0 && list->baseclass.m_bvisible) {
            float x = (float)(game_frect_43.left + (list->baseclass.m_rect.left +
                list->m_itemoffset.x - 18) * scale);
            float y = (float)(game_frect_43.top + (list->baseclass.m_rect.top +
                list->m_itemoffset.y + 8 + (row + 0.5f) * list->m_itemheight) * scale);
            if (y < game_frect_43.top + list->baseclass.m_rect.bottom * scale)
                draw_gold_pointer(x, y, (float)scale);
        }
        return;
    }

    if (s_system_focus == 4 && s_system_root && current_state_page_root() == s_system_root) {
        int mode = *(int *)((uint8_t *)s_system_root + 0x45EA4u);
        struct UIWnd *tab = (struct UIWnd *)((uint8_t *)s_system_root +
            (mode ? 0x25DA4u : 0x25CACu));
        const RECT *r = &tab->m_rect;
        cheat_box(r->left, r->top, r->right, r->top + 2, 0xFFFFDE93u);
        cheat_box(r->left, r->bottom - 2, r->right, r->bottom, 0xFFFFDE93u);
        return;
    }
    if ((s_system_focus == 1 || s_system_focus == 2) && s_system_root && current_state_page_root() == s_system_root &&
        !s_mouse_mode) {
        struct UIWnd *slider = (struct UIWnd *)((uint8_t *)s_system_root +
            (s_system_focus == 1 ? 0x45B40u : 0x45C50u));
        double scale = (game_frect_43.right - game_frect_43.left) / 800.0;
        const uint32_t color = 0xFFFFFF00u;
        int left = (int)(game_frect_43.left + (slider->m_rect.left - 4) * scale);
        int right = (int)(game_frect_43.left + (slider->m_rect.right + 4) * scale);
        int top = (int)(game_frect_43.top + (slider->m_rect.top - 4) * scale);
        int bottom = (int)(game_frect_43.top + (slider->m_rect.bottom + 4) * scale);
        int width = (int)(3 * scale);
        D3DRECT edges[] = {
            {left, top, right, top + width}, {left, bottom - width, right, bottom},
            {left, top, left + width, bottom}, {right - width, top, right, bottom}
        };
        /* 此时游戏的 UI 绘制已结束；直接画屏幕矩形，避免残留矩阵把焦点移出画面。 */
        IDirect3DDevice9_Clear(GB_GfxMgr->m_pd3dDevice, 4, edges,
            D3DCLEAR_TARGET, color, 1.0f, 0);
        return;
    }

    if (s_state_lineup && state_lineup_root() == s_state_lineup &&
        s_state_lineup_focus && !s_selected && !s_mouse_mode) {
        float x, y;
        int has_point;
        double scale = (game_frect_43.right - game_frect_43.left) / 800.0;
        if (state_lineup_queue_actor(s_state_lineup, s_state_lineup_focus) >= 0) {
            const RECT *r = &((struct UIWnd *)s_state_lineup_focus)->m_rect;
            x = (float)(game_frect_43.left + r->left * scale);
            y = (float)(game_frect_43.top + (r->top + r->bottom) * 0.5 * scale);
            has_point = 1;
        } else has_point = state_lineup_point(s_state_lineup, s_state_lineup_focus, &x, &y);
        if (has_point) {
            draw_gold_pointer((float)(x - 30 * scale), y, (float)scale);
        }
        return;
    }

    if (active_cover_subpage()) {
        struct UIWnd *page = active_cover_subpage();
        struct UIWnd *window = s_selected ? &s_selected->baseclass : NULL;
        /* 子页不再重画标题 UIButton；不同种类的原版按钮统一用外框表示焦点。 */
        if (cover_subpage_state() != 4 && s_selection_engaged && !s_mouse_mode &&
            window && window->m_pfather == page && window->m_bvisible && window->m_benable) {
            const RECT *r = &window->m_rect;
            const uint32_t color = 0xFF40FFFFu;
            void (__cdecl *draw)(int, int, int, int, const uint32_t *) =
                (void *)((uintptr_t)GetModuleHandleA(NULL) + 0x00140070u);
            draw(r->left, r->top, r->right, r->top + 3, &color);
            draw(r->left, r->bottom - 3, r->right, r->bottom, &color);
            draw(r->left, r->top, r->left + 3, r->bottom, &color);
            draw(r->right - 3, r->top, r->right, r->bottom, &color);
        }
        return;
    }
    if (!s_selection_engaged || s_mouse_mode || !s_frame ||
        s_frame != g_gamefrm.m_curfrm) return;
    count = collect_active_buttons(buttons);
    selected = find_button(buttons, count, s_selected);
    if (selected < 0) return;
    button = buttons[selected].button;
    if (is_state_ai_choice(&button->baseclass)) return;
    if (active_chat_rest_root()) {
        double scale = (game_frect_43.right - game_frect_43.left) / 800.0;
        float x = (float)(game_frect_43.left + (button->baseclass.m_rect.left - 25) * scale);
        float y = (float)(game_frect_43.top + (button->baseclass.m_rect.top + button->baseclass.m_rect.bottom) * 0.5 * scale);
        draw_gold_pointer(x, y, (float)scale);
        return;
    }
    previous = button->m_state;
    button->m_state = UIBUTTON_MOUSEON;
    if (button->baseclass.vfptr && button->baseclass.vfptr->Render)
        THISCALL_WRAPPER(button->baseclass.vfptr->Render, &button->baseclass);
    button->m_state = previous;
}

void PAL3MenuNav_Reset(void)
{
    s_system_root = NULL;
    s_system_focus = 0;
    s_cheat_open = 0;
    s_cheat_close_frames = 0;
    s_archive_scroll_root = NULL;
    s_archive_scroll_direction = 0;
    s_state_target_root = NULL;
    if (s_state_lineup && state_lineup_root() == s_state_lineup) {
        s_state_lineup_click = state_lineup_source(s_state_lineup);
        s_state_lineup_override = s_state_lineup_click != NULL;
    } else {
        s_state_lineup = NULL;
        s_state_lineup_click = NULL;
        s_state_lineup_override = 0;
    }
    s_state_lineup_focus = NULL;
    s_frame = NULL;
    s_selected = NULL;
    s_selection_engaged = 0;
    s_previous_directions = 0;
    s_mouse_mode = 0;
    s_scroll_list = NULL;
    s_scroll_direction = 0;
    memset(s_repeat_at, 0, sizeof(s_repeat_at));
    s_category_frame = NULL;
    s_category_previous_left = 0;
    s_category_left_used = 0;
    s_category_left_directions = 0;
#if PAL3_MENU_TREE_DIAG
    s_diag_tab_held = 0;
    s_diag_pending = 0;
    s_diag_pending_reasons = 0;
    s_diag_snapshot_id = 0;
    s_diag_node_count = 0;
    s_diag_was_in_ui = 0;
    s_diag_signature_valid = 0;
    s_diag_frame = NULL;
    s_diag_frame_id = 0;
    s_diag_signature = 0;
    s_diag_list_in_tree = NULL;
#endif
}

#include "手柄场景输入.h"

#include <string.h>

#if defined(PAL3_SCENE_INPUT_NATIVE)
#define USE_PAL3_DEFINITIONS
#include "PAL3patch.h"
#endif

#define CAMERA_DEADZONE 0.24f
#define RUN_DOUBLE_CLICK_WINDOW_MS 500u
#define WHEEL_DELTAS_PER_SECOND 720.0f
#define MAX_FRAME_DELTA_MS 100u

enum direction_phase {
    DIRECTION_IDLE,
    DIRECTION_RELEASE_FRAME,
    DIRECTION_WAIT_SECOND_PRESS,
    DIRECTION_RUNNING
};

struct direction_state {
    uint32_t first_press_at;
    enum direction_phase phase;
};

static struct direction_state direction_states[4];
static uint32_t previous_tick_ms;
static uint8_t have_previous_tick;
static float wheel_accumulator;
static int8_t previous_wheel_direction;
#if defined(PAL3_SCENE_INPUT_NATIVE)
static int scene_face_diag_written;
#endif

static const uint8_t direction_bits[4] = {
    PAL3_SCENE_INPUT_UP,
    PAL3_SCENE_INPUT_DOWN,
    PAL3_SCENE_INPUT_LEFT,
    PAL3_SCENE_INPUT_RIGHT
};

int PAL3SceneInput_IsFreeExploration(
    const struct PAL3SceneInputSceneState *scene)
{
    if (!scene) return 0;
    return scene->game_scene && scene->frame_is_scene &&
        scene->scene_frame_exists && scene->current_frame_is_scene_frame &&
        scene->scene_update_enabled && scene->scene_frame_visible &&
        scene->scene_frame_enabled &&
        !scene->role_dialog_visible && !scene->head_dialog_visible &&
        !scene->select_dialog_visible;
}

void PAL3SceneInput_Reset(void)
{
    memset(direction_states, 0, sizeof(direction_states));
    previous_tick_ms = 0;
    have_previous_tick = 0;
    wheel_accumulator = 0.0f;
    previous_wheel_direction = 0;
}

static uint32_t elapsed_since_previous_tick(uint32_t now_ms)
{
    uint32_t elapsed;
    if (!have_previous_tick) {
        previous_tick_ms = now_ms;
        have_previous_tick = 1;
        return 0;
    }

    elapsed = now_ms - previous_tick_ms;
    previous_tick_ms = now_ms;
    if (elapsed > MAX_FRAME_DELTA_MS) elapsed = MAX_FRAME_DELTA_MS;
    return elapsed;
}

static uint8_t update_directions(uint8_t held, uint32_t now_ms)
{
    uint8_t output = 0;
    unsigned int i;
    for (i = 0; i < 4; ++i) {
        struct direction_state *state = &direction_states[i];
        int wanted = (held & direction_bits[i]) != 0;
        if (!wanted) {
            state->phase = DIRECTION_IDLE;
            continue;
        }

        if (state->phase == DIRECTION_IDLE) {
            // 首按只持续一个采样；下一采样留空，之后再保持按下。
            state->phase = DIRECTION_RELEASE_FRAME;
            state->first_press_at = now_ms;
            output |= direction_bits[i];
            continue;
        }

        if (state->phase == DIRECTION_RELEASE_FRAME) {
            state->phase = DIRECTION_WAIT_SECOND_PRESS;
            continue;
        }

        if (state->phase == DIRECTION_WAIT_SECOND_PRESS) {
            if (now_ms - state->first_press_at > RUN_DOUBLE_CLICK_WINDOW_MS) {
                // 采样太慢导致错过双击窗口时，从新的首按重新开始。
                state->first_press_at = now_ms;
                state->phase = DIRECTION_RELEASE_FRAME;
                output |= direction_bits[i];
                continue;
            }
            state->phase = DIRECTION_RUNNING;
        }

        output |= direction_bits[i];
    }
    return output;
}

static float clamp_axis(float value)
{
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

static int8_t wheel_direction(float value)
{
    if (value > CAMERA_DEADZONE) return 1;
    if (value < -CAMERA_DEADZONE) return -1;
    return 0;
}

void PAL3SceneInput_Tick(const struct PAL3SceneInput *input,
    struct PAL3SceneInputOutput *output)
{
    uint32_t elapsed;
    float y;
    int8_t direction;

    if (!output) return;
    memset(output, 0, sizeof(*output));
    if (!input || !input->free_exploration || !input->focused ||
        !input->connected || input->mouse_mode) {
        PAL3SceneInput_Reset();
        return;
    }

    elapsed = elapsed_since_previous_tick(input->now_ms);
    output->directions = update_directions(input->directions, input->now_ms);

    if (input->right_x > CAMERA_DEADZONE)
        output->camera_keys = PAL3_SCENE_INPUT_CAMERA_A;
    else if (input->right_x < -CAMERA_DEADZONE)
        output->camera_keys = PAL3_SCENE_INPUT_CAMERA_D;

    y = clamp_axis(input->right_y);
    direction = wheel_direction(y);
    if (direction != previous_wheel_direction) {
        wheel_accumulator = 0.0f;
        previous_wheel_direction = direction;
    }
    if (direction) {
        float magnitude = y < 0.0f ? -y : y;
        wheel_accumulator += magnitude * WHEEL_DELTAS_PER_SECOND *
            (float)elapsed / 1000.0f;
        if (wheel_accumulator >= 120.0f) {
            output->wheel_delta = (int16_t)(direction * 120);
            wheel_accumulator -= 120.0f;
        }
    } else {
        wheel_accumulator = 0.0f;
    }
}

#if defined(PAL3_SCENE_INPUT_NATIVE)
static void dump_scene_face_child(const char *name, const struct UIWnd *child)
{
    if (!child) return;
    plog("[PAL3SceneFaceDiag] %s id=%lu parent_id=%lu rect=(%ld,%ld,%ld,%ld) create=%d visible=%d enable=%d focus=%d",
        name, (unsigned long)child->m_id,
        (unsigned long)(child->m_pfather ? child->m_pfather->m_id : 0),
        child->m_rect.left, child->m_rect.top,
        child->m_rect.right, child->m_rect.bottom,
        child->m_bcreateok, child->m_bvisible, child->m_benable,
        child->m_bfocus);
}

static void dump_scene_face_once(const struct UISceneFace *face)
{
    int i;
    char name[24];

    if (!face || scene_face_diag_written) return;
    scene_face_diag_written = 1;
    plog("[PAL3SceneFaceDiag] clicktime=%.3f onclick=%d",
        face->m_clicktime, face->m_onclick);
    dump_scene_face_child("root", &face->baseclass);
    dump_scene_face_child("hp", &face->m_hp.baseclass);
    dump_scene_face_child("mp", &face->m_mp.baseclass);
    for (i = 0; i < 3; ++i) {
        wsprintfA(name, "face%d", i);
        dump_scene_face_child(name, &face->m_face[i].baseclass);
    }
    dump_scene_face_child("bk", &face->m_bk.baseclass);
}

static const struct UIStatic *select_menu_face(const struct UISceneFace *face)
{
    const struct UIStatic *selected = NULL;
    int i;

    if (!face) return NULL;
    for (i = 0; i < 3; ++i) {
        const struct UIStatic *candidate = &face->m_face[i];
        const struct UIWnd *window = &candidate->baseclass;
        if ((window->m_id != 2 && window->m_id != 3) ||
            !window->m_bcreateok || !window->m_bvisible ||
            !window->m_benable || window->m_rect.right <= window->m_rect.left ||
            window->m_rect.bottom <= window->m_rect.top)
            continue;
        if (!selected || window->m_rect.right > selected->baseclass.m_rect.right)
            selected = candidate;
    }
    return selected;
}

int PAL3SceneInput_OpenOriginalMenu(struct UISceneFace *face)
{
    struct UIWnd *window;
    const struct UIStatic *target;
    if (!face) return 0;
    window = &face->baseclass;
    if (!window->m_bcreateok || !window->m_bvisible ||
        !window->m_benable || !window->vfptr ||
        !window->vfptr->OnMessage)
        return 0;

    /* 原版处理器用 wParam 选择队员；lParam 不参与该消息处理。 */
    dump_scene_face_once(face);
    target = select_menu_face(face);
    if (!target) return 0;
    return THISCALL_WRAPPER(window->vfptr->OnMessage,
        window, 1009, target->baseclass.m_id, 0);
}
#endif

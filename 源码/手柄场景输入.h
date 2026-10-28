#ifndef PAL3_SCENE_INPUT_H
#define PAL3_SCENE_INPUT_H

#include <stdint.h>

enum PAL3SceneInputDirection {
    PAL3_SCENE_INPUT_UP    = 1u << 0,
    PAL3_SCENE_INPUT_DOWN  = 1u << 1,
    PAL3_SCENE_INPUT_LEFT  = 1u << 2,
    PAL3_SCENE_INPUT_RIGHT = 1u << 3
};

enum PAL3SceneInputCameraKey {
    PAL3_SCENE_INPUT_CAMERA_A = 1u << 0,
    PAL3_SCENE_INPUT_CAMERA_D = 1u << 1
};

struct PAL3SceneInputSceneState {
    uint8_t game_scene;
    uint8_t frame_is_scene;
    uint8_t scene_frame_exists;
    uint8_t current_frame_is_scene_frame;
    uint8_t scene_update_enabled;
    uint8_t scene_frame_visible;
    uint8_t scene_frame_enabled;
    uint8_t role_dialog_visible;
    uint8_t head_dialog_visible;
    uint8_t select_dialog_visible;
};

struct PAL3SceneInput {
    uint8_t free_exploration;
    uint8_t focused;
    uint8_t connected;
    uint8_t mouse_mode;
    uint8_t directions;
    float right_x;
    float right_y;
    uint32_t now_ms;
};

struct PAL3SceneInputOutput {
    uint8_t directions;
    uint8_t camera_keys;
    int16_t wheel_delta;
};

/* 活动窗口必须确实是可见的场景框，且没有覆盖场景的对话模态。 */
int PAL3SceneInput_IsFreeExploration(
    const struct PAL3SceneInputSceneState *scene);

/* 每次真实输入采样调用；持续方向只在一个采样帧内松开，第二按需落在500ms窗口内。 */
void PAL3SceneInput_Tick(const struct PAL3SceneInput *input,
    struct PAL3SceneInputOutput *output);
void PAL3SceneInput_Reset(void);

#if defined(PAL3_SCENE_INPUT_NATIVE)
struct UISceneFace;

/* 用当前可见队员头像的控件 ID 触发原版 scene.face 菜单入口。 */
int PAL3SceneInput_OpenOriginalMenu(struct UISceneFace *face);
#endif

#endif

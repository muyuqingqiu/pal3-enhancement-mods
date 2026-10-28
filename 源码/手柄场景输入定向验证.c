#include "手柄场景输入.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct PAL3SceneInput make_input(uint32_t now_ms)
{
    struct PAL3SceneInput input;
    memset(&input, 0, sizeof(input));
    input.free_exploration = 1;
    input.focused = 1;
    input.connected = 1;
    input.now_ms = now_ms;
    return input;
}

static void test_scene_gate(void)
{
    struct PAL3SceneInputSceneState scene;
    memset(&scene, 0, sizeof(scene));
    scene.game_scene = 1;
    scene.frame_is_scene = 1;
    scene.scene_frame_exists = 1;
    scene.current_frame_is_scene_frame = 1;
    scene.scene_update_enabled = 1;
    scene.scene_frame_visible = 1;
    scene.scene_frame_enabled = 1;
    assert(PAL3SceneInput_IsFreeExploration(&scene));

    scene.scene_update_enabled = 0;
    assert(!PAL3SceneInput_IsFreeExploration(&scene));
    scene.scene_update_enabled = 1;

    scene.frame_is_scene = 0;
    assert(!PAL3SceneInput_IsFreeExploration(&scene));
    scene.frame_is_scene = 1;
    scene.current_frame_is_scene_frame = 0;
    assert(!PAL3SceneInput_IsFreeExploration(&scene));
    scene.current_frame_is_scene_frame = 1;
    scene.role_dialog_visible = 1;
    assert(!PAL3SceneInput_IsFreeExploration(&scene));
    scene.role_dialog_visible = 0;
    scene.head_dialog_visible = 1;
    assert(!PAL3SceneInput_IsFreeExploration(&scene));
    scene.head_dialog_visible = 0;
    scene.select_dialog_visible = 1;
    assert(!PAL3SceneInput_IsFreeExploration(&scene));
    assert(!PAL3SceneInput_IsFreeExploration(NULL));
}

static void test_double_press_and_turn(void)
{
    struct PAL3SceneInput input;
    struct PAL3SceneInputOutput output;

    PAL3SceneInput_Reset();
    input = make_input(0);
    input.directions = PAL3_SCENE_INPUT_UP;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == PAL3_SCENE_INPUT_UP);

    input.now_ms = 8; /* 120 FPS：一个采样帧完整松开。 */
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == 0);

    input.now_ms = 17;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == PAL3_SCENE_INPUT_UP);

    input.now_ms = 25;
    input.directions |= PAL3_SCENE_INPUT_RIGHT;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == (PAL3_SCENE_INPUT_UP | PAL3_SCENE_INPUT_RIGHT));
    input.now_ms = 33;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == PAL3_SCENE_INPUT_UP);
    input.now_ms = 42;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == (PAL3_SCENE_INPUT_UP | PAL3_SCENE_INPUT_RIGHT));

    input.now_ms = 50;
    input.directions = 0;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == 0);
    input.now_ms = 58;
    input.directions = PAL3_SCENE_INPUT_UP;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == PAL3_SCENE_INPUT_UP);

    PAL3SceneInput_Reset();
    input = make_input(1000);
    input.directions = PAL3_SCENE_INPUT_DOWN | PAL3_SCENE_INPUT_LEFT;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == (PAL3_SCENE_INPUT_DOWN | PAL3_SCENE_INPUT_LEFT));
    input.now_ms = 1050; /* 20 FPS：仍只空过一个更新帧。 */
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == 0);
    input.now_ms = 1100;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == (PAL3_SCENE_INPUT_DOWN | PAL3_SCENE_INPUT_LEFT));

    PAL3SceneInput_Reset();
    input = make_input(0);
    input.directions = PAL3_SCENE_INPUT_RIGHT;
    PAL3SceneInput_Tick(&input, &output);
    input.now_ms = 500;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == 0);
    input.now_ms = 1000; /* 超过正常双击窗口，重新首按而不是冒充跑步。 */
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == PAL3_SCENE_INPUT_RIGHT);
}

static void test_camera_and_reset_boundaries(void)
{
    struct PAL3SceneInput input;
    struct PAL3SceneInputOutput output;

    PAL3SceneInput_Reset();
    input = make_input(0);
    input.right_x = 1.0f;
    input.right_y = 1.0f;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.camera_keys == PAL3_SCENE_INPUT_CAMERA_A);
    assert(output.wheel_delta == 0);
    input.now_ms = 100;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.wheel_delta == 0);
    input.now_ms = 200;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.wheel_delta == 120);

    input.now_ms = 216;
    input.right_x = -1.0f;
    input.right_y = -1.0f;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.camera_keys == PAL3_SCENE_INPUT_CAMERA_D);
    assert(output.wheel_delta == 0);
    input.now_ms = 316;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.wheel_delta == 0);
    input.now_ms = 416;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.wheel_delta == -120);

    input.now_ms = 432;
    input.directions = PAL3_SCENE_INPUT_LEFT;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == PAL3_SCENE_INPUT_LEFT);
    input.mouse_mode = 1;
    input.now_ms = 448;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == 0 && output.camera_keys == 0 &&
        output.wheel_delta == 0);

    input.mouse_mode = 0;
    input.now_ms = 464;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == PAL3_SCENE_INPUT_LEFT);
    input.focused = 0;
    input.now_ms = 480;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == 0 && output.camera_keys == 0 &&
        output.wheel_delta == 0);

    input.focused = 1;
    input.now_ms = 496;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == PAL3_SCENE_INPUT_LEFT);
    input.free_exploration = 0;
    input.now_ms = 512;
    PAL3SceneInput_Tick(&input, &output);
    assert(output.directions == 0 && output.camera_keys == 0 &&
        output.wheel_delta == 0);
}

int main(void)
{
    test_scene_gate();
    test_double_press_and_turn();
    test_camera_and_reset_boundaries();
    puts("手柄场景输入定向验证通过");
    return 0;
}

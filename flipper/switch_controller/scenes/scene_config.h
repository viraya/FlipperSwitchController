#pragma once

/**
 * Scene ID enum for SceneManager.
 *
 * Each entry corresponds to an on_enter/on_event/on_exit handler triple
 * registered in the scene_handlers arrays in switch_controller.c.
 */

typedef enum {
    SceneMenu,
    SceneUsbDebug,
    SceneController,
    SceneConfirmExit,
    SceneCount,
} AppScene;

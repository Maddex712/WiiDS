// What App.swift calls in the C++ side (visionos_main.mm). The Swift build imports it as
// its bridging header.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The app's name, as gcn_add_game's LABEL gives it.
const char* gcn_vos_app_label(void);

// The disc image in the app's Documents folder, or "" if there is none.
const char* gcn_vos_find_image(void);

// Why the disc image cannot be played -- there is none, or it is not the disc this build
// was recompiled from -- or "" if it can.
const char* gcn_vos_check_image(void);

// Runs the game into the immersive space's layer (a retained cp_layer_renderer_t) on the
// calling thread. Does not return: the process ends with the game.
void gcn_vos_run(void* layer_renderer);

#ifdef __cplusplus
}
#endif

#include "gx/render.h"
#include <cstdint>
void wiids_mkdd_guest_frame(uint32_t frame);
namespace {
struct Install {
    Install() { gx::set_guest_frame_hook(wiids_mkdd_guest_frame); }
} install;
}

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace wiids::mkdd {

struct ControllerPort {
    int device = -1;
    uint16_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    float lt_key = 0, rt_key = 0, hx = 0, hy = 0;
};

class ControllerPorts {
public:
    std::array<ControllerPort, 4> ports{};

    // Keep surviving devices in their original ports when another controller is removed.
    void set_devices(std::span<const int> devices) {
        for (auto& port : ports) {
            if (std::find(devices.begin(), devices.end(), port.device) == devices.end())
                port = {};
        }
        for (int device : devices) {
            if (device < 0 || find(device)) continue;
            auto open = std::find_if(ports.begin(), ports.end(),
                    [](const ControllerPort& port) { return port.device < 0; });
            if (open == ports.end()) break;
            open->device = device;
        }
    }

    ControllerPort* for_event(int device, bool from_gamepad) {
        if (auto* port = find(device)) return port;
        // The platform can send virtual button events and keyboard navigation to P1.
        return device < 0 || !from_gamepad ? &ports.front() : nullptr;
    }

    void clear_inputs() {
        for (auto& port : ports) port = ControllerPort{port.device};
    }

private:
    ControllerPort* find(int device) {
        if (device < 0) return nullptr;
        auto port = std::find_if(ports.begin(), ports.end(),
                [device](const ControllerPort& p) { return p.device == device; });
        return port != ports.end() ? &*port : nullptr;
    }
};

} // namespace wiids::mkdd

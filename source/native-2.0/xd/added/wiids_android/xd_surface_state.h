#pragma once
#include <mutex>
// UI surface changes and loader publication share one ordered callback stream.
class XdSurfaceState {
    std::mutex lock;
    void* game=nullptr;
    bool ready=false;
    void (*forward)(void*,bool);
public:
    explicit XdSurfaceState(void(*callback)(void*,bool)):forward(callback){}
    void setGame(void* value){std::lock_guard<std::mutex> guard(lock);game=value;if(game)forward(game,ready);}
    void setReady(bool value){std::lock_guard<std::mutex> guard(lock);ready=value;if(game)forward(game,ready);}
};

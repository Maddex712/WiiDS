// GameCube adapters in Wii U mode (Nintendo WUP-028, Mayflash and other
// copies; USB 057e:0337), read by the port itself (gc_adapter.cpp). pad.cpp
// mixes the first plugged port into controller 1.
#pragma once

// The latest report of the first port with a controller: the button bytes as
// the adapter sends them, the sticks minus where they rested when plugged in,
// and the raw trigger travel.
struct GcAdapterInput {
	int b1, b2;         // A B X Y Left Right Down Up / Start Z R L (low bits first)
	int lx, ly, cx, cy; // about -100..100
	int lt, rt;         // 0..255
};

// Starts the thread that finds and reads the adapter (not with SMS_NO_GC_ADAPTER=1).
void gc_adapter_start();
// False while no controller is plugged into the adapter.
bool gc_adapter_read(GcAdapterInput* in);
// Runs or stops the motor of the controller that feeds controller 1.
void gc_adapter_rumble(bool on);
// At exit: gives the thread time to stop the motor.
void gc_adapter_flush_rumble();

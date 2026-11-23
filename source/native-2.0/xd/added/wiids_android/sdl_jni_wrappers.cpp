#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <jni.h>
#include <cstdarg>
#include "stack_bridge.h"
extern "C" bool __real_SDL_AddEventWatch(SDL_EventFilter a0, void * a1);
extern "C" bool __wrap_SDL_AddEventWatch(SDL_EventFilter a0, void * a1) { return XdStack_Invoke("SDL_AddEventWatch", [&]() { return __real_SDL_AddEventWatch(a0, a1); }); }
extern "C" bool __real_SDL_ClearAudioStream(SDL_AudioStream * a0);
extern "C" bool __wrap_SDL_ClearAudioStream(SDL_AudioStream * a0) { return XdStack_Invoke("SDL_ClearAudioStream", [&]() { return __real_SDL_ClearAudioStream(a0); }); }
extern "C" bool __real_SDL_ClearComposition(SDL_Window * a0);
extern "C" bool __wrap_SDL_ClearComposition(SDL_Window * a0) { return XdStack_Invoke("SDL_ClearComposition", [&]() { return __real_SDL_ClearComposition(a0); }); }
extern "C" bool __real_SDL_ClearError();
extern "C" bool __wrap_SDL_ClearError() { return XdStack_Invoke("SDL_ClearError", [&]() { return __real_SDL_ClearError(); }); }
extern "C" void __real_SDL_CloseGamepad(SDL_Gamepad * a0);
extern "C" void __wrap_SDL_CloseGamepad(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_CloseGamepad", [&]() { return __real_SDL_CloseGamepad(a0); }); }
extern "C" void __real_SDL_CloseHaptic(SDL_Haptic * a0);
extern "C" void __wrap_SDL_CloseHaptic(SDL_Haptic * a0) { return XdStack_Invoke("SDL_CloseHaptic", [&]() { return __real_SDL_CloseHaptic(a0); }); }
extern "C" bool __real_SDL_CloseIO(SDL_IOStream * a0);
extern "C" bool __wrap_SDL_CloseIO(SDL_IOStream * a0) { return XdStack_Invoke("SDL_CloseIO", [&]() { return __real_SDL_CloseIO(a0); }); }
extern "C" void __real_SDL_CloseSensor(SDL_Sensor * a0);
extern "C" void __wrap_SDL_CloseSensor(SDL_Sensor * a0) { return XdStack_Invoke("SDL_CloseSensor", [&]() { return __real_SDL_CloseSensor(a0); }); }
extern "C" bool __real_SDL_ConvertEventToRenderCoordinates(SDL_Renderer * a0, SDL_Event * a1);
extern "C" bool __wrap_SDL_ConvertEventToRenderCoordinates(SDL_Renderer * a0, SDL_Event * a1) { return XdStack_Invoke("SDL_ConvertEventToRenderCoordinates", [&]() { return __real_SDL_ConvertEventToRenderCoordinates(a0, a1); }); }
extern "C" bool __real_SDL_CreateDirectory(const char * a0);
extern "C" bool __wrap_SDL_CreateDirectory(const char * a0) { return XdStack_Invoke("SDL_CreateDirectory", [&]() { return __real_SDL_CreateDirectory(a0); }); }
extern "C" SDL_PropertiesID __real_SDL_CreateProperties();
extern "C" SDL_PropertiesID __wrap_SDL_CreateProperties() { return XdStack_Invoke("SDL_CreateProperties", [&]() { return __real_SDL_CreateProperties(); }); }
extern "C" SDL_Renderer * __real_SDL_CreateRendererWithProperties(SDL_PropertiesID a0);
extern "C" SDL_Renderer * __wrap_SDL_CreateRendererWithProperties(SDL_PropertiesID a0) { return XdStack_Invoke("SDL_CreateRendererWithProperties", [&]() { return __real_SDL_CreateRendererWithProperties(a0); }); }
extern "C" SDL_Surface * __real_SDL_CreateSurfaceFrom(int a0, int a1, SDL_PixelFormat a2, void * a3, int a4);
extern "C" SDL_Surface * __wrap_SDL_CreateSurfaceFrom(int a0, int a1, SDL_PixelFormat a2, void * a3, int a4) { return XdStack_Invoke("SDL_CreateSurfaceFrom", [&]() { return __real_SDL_CreateSurfaceFrom(a0, a1, a2, a3, a4); }); }
extern "C" SDL_Cursor * __real_SDL_CreateSystemCursor(SDL_SystemCursor a0);
extern "C" SDL_Cursor * __wrap_SDL_CreateSystemCursor(SDL_SystemCursor a0) { return XdStack_Invoke("SDL_CreateSystemCursor", [&]() { return __real_SDL_CreateSystemCursor(a0); }); }
extern "C" SDL_Texture * __real_SDL_CreateTexture(SDL_Renderer * a0, SDL_PixelFormat a1, SDL_TextureAccess a2, int a3, int a4);
extern "C" SDL_Texture * __wrap_SDL_CreateTexture(SDL_Renderer * a0, SDL_PixelFormat a1, SDL_TextureAccess a2, int a3, int a4) { return XdStack_Invoke("SDL_CreateTexture", [&]() { return __real_SDL_CreateTexture(a0, a1, a2, a3, a4); }); }
extern "C" SDL_Window * __real_SDL_CreateWindow(const char * a0, int a1, int a2, SDL_WindowFlags a3);
extern "C" SDL_Window * __wrap_SDL_CreateWindow(const char * a0, int a1, int a2, SDL_WindowFlags a3) { return XdStack_Invoke("SDL_CreateWindow", [&]() { return __real_SDL_CreateWindow(a0, a1, a2, a3); }); }
extern "C" SDL_Window * __real_SDL_CreateWindowWithProperties(SDL_PropertiesID a0);
extern "C" SDL_Window * __wrap_SDL_CreateWindowWithProperties(SDL_PropertiesID a0) { return XdStack_Invoke("SDL_CreateWindowWithProperties", [&]() { return __real_SDL_CreateWindowWithProperties(a0); }); }
extern "C" void __real_SDL_Delay(Uint32 a0);
extern "C" void __wrap_SDL_Delay(Uint32 a0) { return XdStack_Invoke("SDL_Delay", [&]() { return __real_SDL_Delay(a0); }); }
extern "C" void __real_SDL_DestroyCursor(SDL_Cursor * a0);
extern "C" void __wrap_SDL_DestroyCursor(SDL_Cursor * a0) { return XdStack_Invoke("SDL_DestroyCursor", [&]() { return __real_SDL_DestroyCursor(a0); }); }
extern "C" void __real_SDL_DestroyProperties(SDL_PropertiesID a0);
extern "C" void __wrap_SDL_DestroyProperties(SDL_PropertiesID a0) { return XdStack_Invoke("SDL_DestroyProperties", [&]() { return __real_SDL_DestroyProperties(a0); }); }
extern "C" void __real_SDL_DestroyRenderer(SDL_Renderer * a0);
extern "C" void __wrap_SDL_DestroyRenderer(SDL_Renderer * a0) { return XdStack_Invoke("SDL_DestroyRenderer", [&]() { return __real_SDL_DestroyRenderer(a0); }); }
extern "C" void __real_SDL_DestroySurface(SDL_Surface * a0);
extern "C" void __wrap_SDL_DestroySurface(SDL_Surface * a0) { return XdStack_Invoke("SDL_DestroySurface", [&]() { return __real_SDL_DestroySurface(a0); }); }
extern "C" void __real_SDL_DestroyTexture(SDL_Texture * a0);
extern "C" void __wrap_SDL_DestroyTexture(SDL_Texture * a0) { return XdStack_Invoke("SDL_DestroyTexture", [&]() { return __real_SDL_DestroyTexture(a0); }); }
extern "C" void __real_SDL_DestroyWindow(SDL_Window * a0);
extern "C" void __wrap_SDL_DestroyWindow(SDL_Window * a0) { return XdStack_Invoke("SDL_DestroyWindow", [&]() { return __real_SDL_DestroyWindow(a0); }); }
extern "C" bool __real_SDL_DisableScreenSaver();
extern "C" bool __wrap_SDL_DisableScreenSaver() { return XdStack_Invoke("SDL_DisableScreenSaver", [&]() { return __real_SDL_DisableScreenSaver(); }); }
extern "C" bool __real_SDL_EnableScreenSaver();
extern "C" bool __wrap_SDL_EnableScreenSaver() { return XdStack_Invoke("SDL_EnableScreenSaver", [&]() { return __real_SDL_EnableScreenSaver(); }); }
extern "C" bool __real_SDL_FlushIO(SDL_IOStream * a0);
extern "C" bool __wrap_SDL_FlushIO(SDL_IOStream * a0) { return XdStack_Invoke("SDL_FlushIO", [&]() { return __real_SDL_FlushIO(a0); }); }
extern "C" SDL_GLContext __real_SDL_GL_CreateContext(SDL_Window * a0);
extern "C" SDL_GLContext __wrap_SDL_GL_CreateContext(SDL_Window * a0) { return XdStack_Invoke("SDL_GL_CreateContext", [&]() { return __real_SDL_GL_CreateContext(a0); }); }
extern "C" bool __real_SDL_GL_DestroyContext(SDL_GLContext a0);
extern "C" bool __wrap_SDL_GL_DestroyContext(SDL_GLContext a0) { return XdStack_Invoke("SDL_GL_DestroyContext", [&]() { return __real_SDL_GL_DestroyContext(a0); }); }
extern "C" SDL_GLContext __real_SDL_GL_GetCurrentContext();
extern "C" SDL_GLContext __wrap_SDL_GL_GetCurrentContext() { return XdStack_Invoke("SDL_GL_GetCurrentContext", [&]() { return __real_SDL_GL_GetCurrentContext(); }); }
extern "C" bool __real_SDL_GL_MakeCurrent(SDL_Window * a0, SDL_GLContext a1);
extern "C" bool __wrap_SDL_GL_MakeCurrent(SDL_Window * a0, SDL_GLContext a1) { return XdStack_Invoke("SDL_GL_MakeCurrent", [&]() { return __real_SDL_GL_MakeCurrent(a0, a1); }); }
extern "C" bool __real_SDL_GL_SetAttribute(SDL_GLAttr a0, int a1);
extern "C" bool __wrap_SDL_GL_SetAttribute(SDL_GLAttr a0, int a1) { return XdStack_Invoke("SDL_GL_SetAttribute", [&]() { return __real_SDL_GL_SetAttribute(a0, a1); }); }
extern "C" bool __real_SDL_GL_SetSwapInterval(int a0);
extern "C" bool __wrap_SDL_GL_SetSwapInterval(int a0) { return XdStack_Invoke("SDL_GL_SetSwapInterval", [&]() { return __real_SDL_GL_SetSwapInterval(a0); }); }
extern "C" bool __real_SDL_GL_SwapWindow(SDL_Window * a0);
extern "C" bool __wrap_SDL_GL_SwapWindow(SDL_Window * a0) { return XdStack_Invoke("SDL_GL_SwapWindow", [&]() { return __real_SDL_GL_SwapWindow(a0); }); }
extern "C" void __real_SDL_GUIDToString(SDL_GUID a0, char * a1, int a2);
extern "C" void __wrap_SDL_GUIDToString(SDL_GUID a0, char * a1, int a2) { return XdStack_Invoke("SDL_GUIDToString", [&]() { return __real_SDL_GUIDToString(a0, a1, a2); }); }
extern "C" bool __real_SDL_GamepadHasSensor(SDL_Gamepad * a0, SDL_SensorType a1);
extern "C" bool __wrap_SDL_GamepadHasSensor(SDL_Gamepad * a0, SDL_SensorType a1) { return XdStack_Invoke("SDL_GamepadHasSensor", [&]() { return __real_SDL_GamepadHasSensor(a0, a1); }); }
extern "C" void * __real_SDL_GetAndroidActivity();
extern "C" void * __wrap_SDL_GetAndroidActivity() { return XdStack_Invoke("SDL_GetAndroidActivity", [&]() { return __real_SDL_GetAndroidActivity(); }); }
extern "C" void * __real_SDL_GetAndroidJNIEnv();
extern void* XdStack_JniEnv();
extern "C" void* __wrap_SDL_GetAndroidJNIEnv() { return XdStack_JniEnv(); }
extern "C" int __real_SDL_GetAudioStreamQueued(SDL_AudioStream * a0);
extern "C" int __wrap_SDL_GetAudioStreamQueued(SDL_AudioStream * a0) { return XdStack_Invoke("SDL_GetAudioStreamQueued", [&]() { return __real_SDL_GetAudioStreamQueued(a0); }); }
extern "C" const char * __real_SDL_GetBasePath();
extern "C" const char * __wrap_SDL_GetBasePath() { return XdStack_Invoke("SDL_GetBasePath", [&]() { return __real_SDL_GetBasePath(); }); }
extern "C" bool __real_SDL_GetBooleanProperty(SDL_PropertiesID a0, const char * a1, bool a2);
extern "C" bool __wrap_SDL_GetBooleanProperty(SDL_PropertiesID a0, const char * a1, bool a2) { return XdStack_Invoke("SDL_GetBooleanProperty", [&]() { return __real_SDL_GetBooleanProperty(a0, a1, a2); }); }
extern "C" char * __real_SDL_GetClipboardText();
extern "C" char * __wrap_SDL_GetClipboardText() { return XdStack_Invoke("SDL_GetClipboardText", [&]() { return __real_SDL_GetClipboardText(); }); }
extern "C" const SDL_DisplayMode * __real_SDL_GetCurrentDisplayMode(SDL_DisplayID a0);
extern "C" const SDL_DisplayMode * __wrap_SDL_GetCurrentDisplayMode(SDL_DisplayID a0) { return XdStack_Invoke("SDL_GetCurrentDisplayMode", [&]() { return __real_SDL_GetCurrentDisplayMode(a0); }); }
extern "C" SDL_DisplayOrientation __real_SDL_GetCurrentDisplayOrientation(SDL_DisplayID a0);
extern "C" SDL_DisplayOrientation __wrap_SDL_GetCurrentDisplayOrientation(SDL_DisplayID a0) { return XdStack_Invoke("SDL_GetCurrentDisplayOrientation", [&]() { return __real_SDL_GetCurrentDisplayOrientation(a0); }); }
extern "C" bool __real_SDL_GetDisplayBounds(SDL_DisplayID a0, SDL_Rect * a1);
extern "C" bool __wrap_SDL_GetDisplayBounds(SDL_DisplayID a0, SDL_Rect * a1) { return XdStack_Invoke("SDL_GetDisplayBounds", [&]() { return __real_SDL_GetDisplayBounds(a0, a1); }); }
extern "C" float __real_SDL_GetDisplayContentScale(SDL_DisplayID a0);
extern "C" float __wrap_SDL_GetDisplayContentScale(SDL_DisplayID a0) { return XdStack_Invoke("SDL_GetDisplayContentScale", [&]() { return __real_SDL_GetDisplayContentScale(a0); }); }
extern "C" SDL_DisplayID __real_SDL_GetDisplayForWindow(SDL_Window * a0);
extern "C" SDL_DisplayID __wrap_SDL_GetDisplayForWindow(SDL_Window * a0) { return XdStack_Invoke("SDL_GetDisplayForWindow", [&]() { return __real_SDL_GetDisplayForWindow(a0); }); }
extern "C" const char * __real_SDL_GetDisplayName(SDL_DisplayID a0);
extern "C" const char * __wrap_SDL_GetDisplayName(SDL_DisplayID a0) { return XdStack_Invoke("SDL_GetDisplayName", [&]() { return __real_SDL_GetDisplayName(a0); }); }
extern "C" bool __real_SDL_GetDisplayUsableBounds(SDL_DisplayID a0, SDL_Rect * a1);
extern "C" bool __wrap_SDL_GetDisplayUsableBounds(SDL_DisplayID a0, SDL_Rect * a1) { return XdStack_Invoke("SDL_GetDisplayUsableBounds", [&]() { return __real_SDL_GetDisplayUsableBounds(a0, a1); }); }
extern "C" SDL_DisplayID * __real_SDL_GetDisplays(int * a0);
extern "C" SDL_DisplayID * __wrap_SDL_GetDisplays(int * a0) { return XdStack_Invoke("SDL_GetDisplays", [&]() { return __real_SDL_GetDisplays(a0); }); }
extern "C" const char * __real_SDL_GetError();
extern "C" const char * __wrap_SDL_GetError() { return XdStack_Invoke("SDL_GetError", [&]() { return __real_SDL_GetError(); }); }
extern "C" Sint16 __real_SDL_GetGamepadAxis(SDL_Gamepad * a0, SDL_GamepadAxis a1);
extern "C" Sint16 __wrap_SDL_GetGamepadAxis(SDL_Gamepad * a0, SDL_GamepadAxis a1) { return XdStack_Invoke("SDL_GetGamepadAxis", [&]() { return __real_SDL_GetGamepadAxis(a0, a1); }); }
extern "C" bool __real_SDL_GetGamepadButton(SDL_Gamepad * a0, SDL_GamepadButton a1);
extern "C" bool __wrap_SDL_GetGamepadButton(SDL_Gamepad * a0, SDL_GamepadButton a1) { return XdStack_Invoke("SDL_GetGamepadButton", [&]() { return __real_SDL_GetGamepadButton(a0, a1); }); }
extern "C" SDL_GamepadButtonLabel __real_SDL_GetGamepadButtonLabel(SDL_Gamepad * a0, SDL_GamepadButton a1);
extern "C" SDL_GamepadButtonLabel __wrap_SDL_GetGamepadButtonLabel(SDL_Gamepad * a0, SDL_GamepadButton a1) { return XdStack_Invoke("SDL_GetGamepadButtonLabel", [&]() { return __real_SDL_GetGamepadButtonLabel(a0, a1); }); }
extern "C" SDL_Gamepad * __real_SDL_GetGamepadFromID(SDL_JoystickID a0);
extern "C" SDL_Gamepad * __wrap_SDL_GetGamepadFromID(SDL_JoystickID a0) { return XdStack_Invoke("SDL_GetGamepadFromID", [&]() { return __real_SDL_GetGamepadFromID(a0); }); }
extern "C" SDL_GUID __real_SDL_GetGamepadGUIDForID(SDL_JoystickID a0);
extern "C" SDL_GUID __wrap_SDL_GetGamepadGUIDForID(SDL_JoystickID a0) { return XdStack_Invoke("SDL_GetGamepadGUIDForID", [&]() { return __real_SDL_GetGamepadGUIDForID(a0); }); }
extern "C" SDL_JoystickID __real_SDL_GetGamepadID(SDL_Gamepad * a0);
extern "C" SDL_JoystickID __wrap_SDL_GetGamepadID(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadID", [&]() { return __real_SDL_GetGamepadID(a0); }); }
extern "C" SDL_Joystick * __real_SDL_GetGamepadJoystick(SDL_Gamepad * a0);
extern "C" SDL_Joystick * __wrap_SDL_GetGamepadJoystick(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadJoystick", [&]() { return __real_SDL_GetGamepadJoystick(a0); }); }
extern "C" const char * __real_SDL_GetGamepadName(SDL_Gamepad * a0);
extern "C" const char * __wrap_SDL_GetGamepadName(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadName", [&]() { return __real_SDL_GetGamepadName(a0); }); }
extern "C" const char * __real_SDL_GetGamepadNameForID(SDL_JoystickID a0);
extern "C" const char * __wrap_SDL_GetGamepadNameForID(SDL_JoystickID a0) { return XdStack_Invoke("SDL_GetGamepadNameForID", [&]() { return __real_SDL_GetGamepadNameForID(a0); }); }
extern "C" int __real_SDL_GetGamepadPlayerIndex(SDL_Gamepad * a0);
extern "C" int __wrap_SDL_GetGamepadPlayerIndex(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadPlayerIndex", [&]() { return __real_SDL_GetGamepadPlayerIndex(a0); }); }
extern "C" SDL_PowerState __real_SDL_GetGamepadPowerInfo(SDL_Gamepad * a0, int * a1);
extern "C" SDL_PowerState __wrap_SDL_GetGamepadPowerInfo(SDL_Gamepad * a0, int * a1) { return XdStack_Invoke("SDL_GetGamepadPowerInfo", [&]() { return __real_SDL_GetGamepadPowerInfo(a0, a1); }); }
extern "C" Uint16 __real_SDL_GetGamepadProduct(SDL_Gamepad * a0);
extern "C" Uint16 __wrap_SDL_GetGamepadProduct(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadProduct", [&]() { return __real_SDL_GetGamepadProduct(a0); }); }
extern "C" SDL_PropertiesID __real_SDL_GetGamepadProperties(SDL_Gamepad * a0);
extern "C" SDL_PropertiesID __wrap_SDL_GetGamepadProperties(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadProperties", [&]() { return __real_SDL_GetGamepadProperties(a0); }); }
extern "C" const char * __real_SDL_GetGamepadSerial(SDL_Gamepad * a0);
extern "C" const char * __wrap_SDL_GetGamepadSerial(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadSerial", [&]() { return __real_SDL_GetGamepadSerial(a0); }); }
extern "C" const char * __real_SDL_GetGamepadStringForAxis(SDL_GamepadAxis a0);
extern "C" const char * __wrap_SDL_GetGamepadStringForAxis(SDL_GamepadAxis a0) { return XdStack_Invoke("SDL_GetGamepadStringForAxis", [&]() { return __real_SDL_GetGamepadStringForAxis(a0); }); }
extern "C" const char * __real_SDL_GetGamepadStringForButton(SDL_GamepadButton a0);
extern "C" const char * __wrap_SDL_GetGamepadStringForButton(SDL_GamepadButton a0) { return XdStack_Invoke("SDL_GetGamepadStringForButton", [&]() { return __real_SDL_GetGamepadStringForButton(a0); }); }
extern "C" SDL_GamepadType __real_SDL_GetGamepadType(SDL_Gamepad * a0);
extern "C" SDL_GamepadType __wrap_SDL_GetGamepadType(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadType", [&]() { return __real_SDL_GetGamepadType(a0); }); }
extern "C" Uint16 __real_SDL_GetGamepadVendor(SDL_Gamepad * a0);
extern "C" Uint16 __wrap_SDL_GetGamepadVendor(SDL_Gamepad * a0) { return XdStack_Invoke("SDL_GetGamepadVendor", [&]() { return __real_SDL_GetGamepadVendor(a0); }); }
extern "C" SDL_JoystickID * __real_SDL_GetGamepads(int * a0);
extern "C" SDL_JoystickID * __wrap_SDL_GetGamepads(int * a0) { return XdStack_Invoke("SDL_GetGamepads", [&]() { return __real_SDL_GetGamepads(a0); }); }
extern "C" SDL_MouseButtonFlags __real_SDL_GetGlobalMouseState(float * a0, float * a1);
extern "C" SDL_MouseButtonFlags __wrap_SDL_GetGlobalMouseState(float * a0, float * a1) { return XdStack_Invoke("SDL_GetGlobalMouseState", [&]() { return __real_SDL_GetGlobalMouseState(a0, a1); }); }
extern "C" const char * __real_SDL_GetHapticNameForID(SDL_HapticID a0);
extern "C" const char * __wrap_SDL_GetHapticNameForID(SDL_HapticID a0) { return XdStack_Invoke("SDL_GetHapticNameForID", [&]() { return __real_SDL_GetHapticNameForID(a0); }); }
extern "C" SDL_HapticID * __real_SDL_GetHaptics(int * a0);
extern "C" SDL_HapticID * __wrap_SDL_GetHaptics(int * a0) { return XdStack_Invoke("SDL_GetHaptics", [&]() { return __real_SDL_GetHaptics(a0); }); }
extern "C" Sint64 __real_SDL_GetIOSize(SDL_IOStream * a0);
extern "C" Sint64 __wrap_SDL_GetIOSize(SDL_IOStream * a0) { return XdStack_Invoke("SDL_GetIOSize", [&]() { return __real_SDL_GetIOSize(a0); }); }
extern "C" SDL_IOStatus __real_SDL_GetIOStatus(SDL_IOStream * a0);
extern "C" SDL_IOStatus __wrap_SDL_GetIOStatus(SDL_IOStream * a0) { return XdStack_Invoke("SDL_GetIOStatus", [&]() { return __real_SDL_GetIOStatus(a0); }); }
extern "C" void __real_SDL_GetJoystickGUIDInfo(SDL_GUID a0, Uint16 * a1, Uint16 * a2, Uint16 * a3, Uint16 * a4);
extern "C" void __wrap_SDL_GetJoystickGUIDInfo(SDL_GUID a0, Uint16 * a1, Uint16 * a2, Uint16 * a3, Uint16 * a4) { return XdStack_Invoke("SDL_GetJoystickGUIDInfo", [&]() { return __real_SDL_GetJoystickGUIDInfo(a0, a1, a2, a3, a4); }); }
extern "C" SDL_JoystickID __real_SDL_GetJoystickID(SDL_Joystick * a0);
extern "C" SDL_JoystickID __wrap_SDL_GetJoystickID(SDL_Joystick * a0) { return XdStack_Invoke("SDL_GetJoystickID", [&]() { return __real_SDL_GetJoystickID(a0); }); }
extern "C" SDL_PowerState __real_SDL_GetJoystickPowerInfo(SDL_Joystick * a0, int * a1);
extern "C" SDL_PowerState __wrap_SDL_GetJoystickPowerInfo(SDL_Joystick * a0, int * a1) { return XdStack_Invoke("SDL_GetJoystickPowerInfo", [&]() { return __real_SDL_GetJoystickPowerInfo(a0, a1); }); }
extern "C" SDL_Keycode __real_SDL_GetKeyFromScancode(SDL_Scancode a0, SDL_Keymod a1, bool a2);
extern "C" SDL_Keycode __wrap_SDL_GetKeyFromScancode(SDL_Scancode a0, SDL_Keymod a1, bool a2) { return XdStack_Invoke("SDL_GetKeyFromScancode", [&]() { return __real_SDL_GetKeyFromScancode(a0, a1, a2); }); }
extern "C" const bool * __real_SDL_GetKeyboardState(int * a0);
extern "C" const bool * __wrap_SDL_GetKeyboardState(int * a0) { return XdStack_Invoke("SDL_GetKeyboardState", [&]() { return __real_SDL_GetKeyboardState(a0); }); }
extern "C" SDL_Keymod __real_SDL_GetModState();
extern "C" SDL_Keymod __wrap_SDL_GetModState() { return XdStack_Invoke("SDL_GetModState", [&]() { return __real_SDL_GetModState(); }); }
extern "C" SDL_DisplayOrientation __real_SDL_GetNaturalDisplayOrientation(SDL_DisplayID a0);
extern "C" SDL_DisplayOrientation __wrap_SDL_GetNaturalDisplayOrientation(SDL_DisplayID a0) { return XdStack_Invoke("SDL_GetNaturalDisplayOrientation", [&]() { return __real_SDL_GetNaturalDisplayOrientation(a0); }); }
extern "C" bool __real_SDL_GetPathInfo(const char * a0, SDL_PathInfo * a1);
extern "C" bool __wrap_SDL_GetPathInfo(const char * a0, SDL_PathInfo * a1) { return XdStack_Invoke("SDL_GetPathInfo", [&]() { return __real_SDL_GetPathInfo(a0, a1); }); }
extern "C" Uint64 __real_SDL_GetPerformanceCounter();
extern "C" Uint64 __wrap_SDL_GetPerformanceCounter() { return XdStack_Invoke("SDL_GetPerformanceCounter", [&]() { return __real_SDL_GetPerformanceCounter(); }); }
extern "C" Uint64 __real_SDL_GetPerformanceFrequency();
extern "C" Uint64 __wrap_SDL_GetPerformanceFrequency() { return XdStack_Invoke("SDL_GetPerformanceFrequency", [&]() { return __real_SDL_GetPerformanceFrequency(); }); }
extern "C" SDL_PixelFormat __real_SDL_GetPixelFormatForMasks(int a0, Uint32 a1, Uint32 a2, Uint32 a3, Uint32 a4);
extern "C" SDL_PixelFormat __wrap_SDL_GetPixelFormatForMasks(int a0, Uint32 a1, Uint32 a2, Uint32 a3, Uint32 a4) { return XdStack_Invoke("SDL_GetPixelFormatForMasks", [&]() { return __real_SDL_GetPixelFormatForMasks(a0, a1, a2, a3, a4); }); }
extern "C" void * __real_SDL_GetPointerProperty(SDL_PropertiesID a0, const char * a1, void * a2);
extern "C" void * __wrap_SDL_GetPointerProperty(SDL_PropertiesID a0, const char * a1, void * a2) { return XdStack_Invoke("SDL_GetPointerProperty", [&]() { return __real_SDL_GetPointerProperty(a0, a1, a2); }); }
extern "C" char * __real_SDL_GetPrefPath(const char * a0, const char * a1);
extern "C" char * __wrap_SDL_GetPrefPath(const char * a0, const char * a1) { return XdStack_Invoke("SDL_GetPrefPath", [&]() { return __real_SDL_GetPrefPath(a0, a1); }); }
extern "C" SDL_DisplayID __real_SDL_GetPrimaryDisplay();
extern "C" SDL_DisplayID __wrap_SDL_GetPrimaryDisplay() { return XdStack_Invoke("SDL_GetPrimaryDisplay", [&]() { return __real_SDL_GetPrimaryDisplay(); }); }
extern "C" bool __real_SDL_GetRenderClipRect(SDL_Renderer * a0, SDL_Rect * a1);
extern "C" bool __wrap_SDL_GetRenderClipRect(SDL_Renderer * a0, SDL_Rect * a1) { return XdStack_Invoke("SDL_GetRenderClipRect", [&]() { return __real_SDL_GetRenderClipRect(a0, a1); }); }
extern "C" bool __real_SDL_GetRenderOutputSize(SDL_Renderer * a0, int * a1, int * a2);
extern "C" bool __wrap_SDL_GetRenderOutputSize(SDL_Renderer * a0, int * a1, int * a2) { return XdStack_Invoke("SDL_GetRenderOutputSize", [&]() { return __real_SDL_GetRenderOutputSize(a0, a1, a2); }); }
extern "C" bool __real_SDL_GetRenderScale(SDL_Renderer * a0, float * a1, float * a2);
extern "C" bool __wrap_SDL_GetRenderScale(SDL_Renderer * a0, float * a1, float * a2) { return XdStack_Invoke("SDL_GetRenderScale", [&]() { return __real_SDL_GetRenderScale(a0, a1, a2); }); }
extern "C" bool __real_SDL_GetRenderViewport(SDL_Renderer * a0, SDL_Rect * a1);
extern "C" bool __wrap_SDL_GetRenderViewport(SDL_Renderer * a0, SDL_Rect * a1) { return XdStack_Invoke("SDL_GetRenderViewport", [&]() { return __real_SDL_GetRenderViewport(a0, a1); }); }
extern "C" SDL_Scancode __real_SDL_GetScancodeFromName(const char * a0);
extern "C" SDL_Scancode __wrap_SDL_GetScancodeFromName(const char * a0) { return XdStack_Invoke("SDL_GetScancodeFromName", [&]() { return __real_SDL_GetScancodeFromName(a0); }); }
extern "C" const char * __real_SDL_GetScancodeName(SDL_Scancode a0);
extern "C" const char * __wrap_SDL_GetScancodeName(SDL_Scancode a0) { return XdStack_Invoke("SDL_GetScancodeName", [&]() { return __real_SDL_GetScancodeName(a0); }); }
extern "C" bool __real_SDL_GetSensorData(SDL_Sensor * a0, float * a1, int a2);
extern "C" bool __wrap_SDL_GetSensorData(SDL_Sensor * a0, float * a1, int a2) { return XdStack_Invoke("SDL_GetSensorData", [&]() { return __real_SDL_GetSensorData(a0, a1, a2); }); }
extern "C" SDL_Sensor * __real_SDL_GetSensorFromID(SDL_SensorID a0);
extern "C" SDL_Sensor * __wrap_SDL_GetSensorFromID(SDL_SensorID a0) { return XdStack_Invoke("SDL_GetSensorFromID", [&]() { return __real_SDL_GetSensorFromID(a0); }); }
extern "C" SDL_SensorType __real_SDL_GetSensorType(SDL_Sensor * a0);
extern "C" SDL_SensorType __wrap_SDL_GetSensorType(SDL_Sensor * a0) { return XdStack_Invoke("SDL_GetSensorType", [&]() { return __real_SDL_GetSensorType(a0); }); }
extern "C" SDL_SensorType __real_SDL_GetSensorTypeForID(SDL_SensorID a0);
extern "C" SDL_SensorType __wrap_SDL_GetSensorTypeForID(SDL_SensorID a0) { return XdStack_Invoke("SDL_GetSensorTypeForID", [&]() { return __real_SDL_GetSensorTypeForID(a0); }); }
extern "C" SDL_SensorID * __real_SDL_GetSensors(int * a0);
extern "C" SDL_SensorID * __wrap_SDL_GetSensors(int * a0) { return XdStack_Invoke("SDL_GetSensors", [&]() { return __real_SDL_GetSensors(a0); }); }
extern "C" Uint64 __real_SDL_GetTicks();
extern "C" Uint64 __wrap_SDL_GetTicks() { return XdStack_Invoke("SDL_GetTicks", [&]() { return __real_SDL_GetTicks(); }); }
extern "C" Uint64 __real_SDL_GetTicksNS();
extern "C" Uint64 __wrap_SDL_GetTicksNS() { return XdStack_Invoke("SDL_GetTicksNS", [&]() { return __real_SDL_GetTicksNS(); }); }
extern "C" const char * __real_SDL_GetUserFolder(SDL_Folder a0);
extern "C" const char * __wrap_SDL_GetUserFolder(SDL_Folder a0) { return XdStack_Invoke("SDL_GetUserFolder", [&]() { return __real_SDL_GetUserFolder(a0); }); }
extern "C" float __real_SDL_GetWindowDisplayScale(SDL_Window * a0);
extern "C" float __wrap_SDL_GetWindowDisplayScale(SDL_Window * a0) { return XdStack_Invoke("SDL_GetWindowDisplayScale", [&]() { return __real_SDL_GetWindowDisplayScale(a0); }); }
extern "C" SDL_WindowFlags __real_SDL_GetWindowFlags(SDL_Window * a0);
extern "C" SDL_WindowFlags __wrap_SDL_GetWindowFlags(SDL_Window * a0) { return XdStack_Invoke("SDL_GetWindowFlags", [&]() { return __real_SDL_GetWindowFlags(a0); }); }
extern "C" SDL_Window * __real_SDL_GetWindowFromEvent(const SDL_Event * a0);
extern "C" SDL_Window * __wrap_SDL_GetWindowFromEvent(const SDL_Event * a0) { return XdStack_Invoke("SDL_GetWindowFromEvent", [&]() { return __real_SDL_GetWindowFromEvent(a0); }); }
extern "C" SDL_Window * __real_SDL_GetWindowFromID(SDL_WindowID a0);
extern "C" SDL_Window * __wrap_SDL_GetWindowFromID(SDL_WindowID a0) { return XdStack_Invoke("SDL_GetWindowFromID", [&]() { return __real_SDL_GetWindowFromID(a0); }); }
extern "C" SDL_WindowID __real_SDL_GetWindowID(SDL_Window * a0);
extern "C" SDL_WindowID __wrap_SDL_GetWindowID(SDL_Window * a0) { return XdStack_Invoke("SDL_GetWindowID", [&]() { return __real_SDL_GetWindowID(a0); }); }
extern "C" bool __real_SDL_GetWindowPosition(SDL_Window * a0, int * a1, int * a2);
extern "C" bool __wrap_SDL_GetWindowPosition(SDL_Window * a0, int * a1, int * a2) { return XdStack_Invoke("SDL_GetWindowPosition", [&]() { return __real_SDL_GetWindowPosition(a0, a1, a2); }); }
extern "C" SDL_PropertiesID __real_SDL_GetWindowProperties(SDL_Window * a0);
extern "C" SDL_PropertiesID __wrap_SDL_GetWindowProperties(SDL_Window * a0) { return XdStack_Invoke("SDL_GetWindowProperties", [&]() { return __real_SDL_GetWindowProperties(a0); }); }
extern "C" bool __real_SDL_GetWindowRelativeMouseMode(SDL_Window * a0);
extern "C" bool __wrap_SDL_GetWindowRelativeMouseMode(SDL_Window * a0) { return XdStack_Invoke("SDL_GetWindowRelativeMouseMode", [&]() { return __real_SDL_GetWindowRelativeMouseMode(a0); }); }
extern "C" bool __real_SDL_GetWindowSize(SDL_Window * a0, int * a1, int * a2);
extern "C" bool __wrap_SDL_GetWindowSize(SDL_Window * a0, int * a1, int * a2) { return XdStack_Invoke("SDL_GetWindowSize", [&]() { return __real_SDL_GetWindowSize(a0, a1, a2); }); }
extern "C" bool __real_SDL_GetWindowSizeInPixels(SDL_Window * a0, int * a1, int * a2);
extern "C" bool __wrap_SDL_GetWindowSizeInPixels(SDL_Window * a0, int * a1, int * a2) { return XdStack_Invoke("SDL_GetWindowSizeInPixels", [&]() { return __real_SDL_GetWindowSizeInPixels(a0, a1, a2); }); }
extern "C" const char * __real_SDL_GetWindowTitle(SDL_Window * a0);
extern "C" const char * __wrap_SDL_GetWindowTitle(SDL_Window * a0) { return XdStack_Invoke("SDL_GetWindowTitle", [&]() { return __real_SDL_GetWindowTitle(a0); }); }
extern "C" bool __real_SDL_HapticRumbleSupported(SDL_Haptic * a0);
extern "C" bool __wrap_SDL_HapticRumbleSupported(SDL_Haptic * a0) { return XdStack_Invoke("SDL_HapticRumbleSupported", [&]() { return __real_SDL_HapticRumbleSupported(a0); }); }
extern "C" bool __real_SDL_HideCursor();
extern "C" bool __wrap_SDL_HideCursor() { return XdStack_Invoke("SDL_HideCursor", [&]() { return __real_SDL_HideCursor(); }); }
extern "C" SDL_IOStream * __real_SDL_IOFromFile(const char * a0, const char * a1);
extern "C" SDL_IOStream * __wrap_SDL_IOFromFile(const char * a0, const char * a1) { return XdStack_Invoke("SDL_IOFromFile", [&]() { return __real_SDL_IOFromFile(a0, a1); }); }
extern "C" bool __real_SDL_Init(SDL_InitFlags a0);
extern "C" bool __wrap_SDL_Init(SDL_InitFlags a0) { return XdStack_Invoke("SDL_Init", [&]() { return __real_SDL_Init(a0); }); }
extern "C" bool __real_SDL_InitHapticRumble(SDL_Haptic * a0);
extern "C" bool __wrap_SDL_InitHapticRumble(SDL_Haptic * a0) { return XdStack_Invoke("SDL_InitHapticRumble", [&]() { return __real_SDL_InitHapticRumble(a0); }); }
extern "C" bool __real_SDL_InitSubSystem(SDL_InitFlags a0);
extern "C" bool __wrap_SDL_InitSubSystem(SDL_InitFlags a0) { return XdStack_Invoke("SDL_InitSubSystem", [&]() { return __real_SDL_InitSubSystem(a0); }); }
extern "C" bool __real_SDL_IsMainThread();
extern "C" bool __wrap_SDL_IsMainThread() { return XdStack_Invoke("SDL_IsMainThread", [&]() { return __real_SDL_IsMainThread(); }); }
extern "C" void * __real_SDL_LoadFile_IO(SDL_IOStream * a0, size_t * a1, bool a2);
extern "C" void * __wrap_SDL_LoadFile_IO(SDL_IOStream * a0, size_t * a1, bool a2) { return XdStack_Invoke("SDL_LoadFile_IO", [&]() { return __real_SDL_LoadFile_IO(a0, a1, a2); }); }
extern "C" SDL_Surface * __real_SDL_LoadPNG(const char * a0);
extern "C" SDL_Surface * __wrap_SDL_LoadPNG(const char * a0) { return XdStack_Invoke("SDL_LoadPNG", [&]() { return __real_SDL_LoadPNG(a0); }); }
extern "C" SDL_AudioStream * __real_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID a0, const SDL_AudioSpec * a1, SDL_AudioStreamCallback a2, void * a3);
extern "C" SDL_AudioStream * __wrap_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID a0, const SDL_AudioSpec * a1, SDL_AudioStreamCallback a2, void * a3) { return XdStack_Invoke("SDL_OpenAudioDeviceStream", [&]() { return __real_SDL_OpenAudioDeviceStream(a0, a1, a2, a3); }); }
extern "C" SDL_Gamepad * __real_SDL_OpenGamepad(SDL_JoystickID a0);
extern "C" SDL_Gamepad * __wrap_SDL_OpenGamepad(SDL_JoystickID a0) { return XdStack_Invoke("SDL_OpenGamepad", [&]() { return __real_SDL_OpenGamepad(a0); }); }
extern "C" SDL_Haptic * __real_SDL_OpenHaptic(SDL_HapticID a0);
extern "C" SDL_Haptic * __wrap_SDL_OpenHaptic(SDL_HapticID a0) { return XdStack_Invoke("SDL_OpenHaptic", [&]() { return __real_SDL_OpenHaptic(a0); }); }
extern "C" SDL_IOStream * __real_SDL_OpenIO(const SDL_IOStreamInterface * a0, void * a1);
extern "C" SDL_IOStream * __wrap_SDL_OpenIO(const SDL_IOStreamInterface * a0, void * a1) { return XdStack_Invoke("SDL_OpenIO", [&]() { return __real_SDL_OpenIO(a0, a1); }); }
extern "C" SDL_Sensor * __real_SDL_OpenSensor(SDL_SensorID a0);
extern "C" SDL_Sensor * __wrap_SDL_OpenSensor(SDL_SensorID a0) { return XdStack_Invoke("SDL_OpenSensor", [&]() { return __real_SDL_OpenSensor(a0); }); }
extern "C" bool __real_SDL_OpenURL(const char * a0);
extern "C" bool __wrap_SDL_OpenURL(const char * a0) { return XdStack_Invoke("SDL_OpenURL", [&]() { return __real_SDL_OpenURL(a0); }); }
extern "C" bool __real_SDL_PauseAudioStreamDevice(SDL_AudioStream * a0);
extern "C" bool __wrap_SDL_PauseAudioStreamDevice(SDL_AudioStream * a0) { return XdStack_Invoke("SDL_PauseAudioStreamDevice", [&]() { return __real_SDL_PauseAudioStreamDevice(a0); }); }
extern "C" bool __real_SDL_PlayHapticRumble(SDL_Haptic * a0, float a1, Uint32 a2);
extern "C" bool __wrap_SDL_PlayHapticRumble(SDL_Haptic * a0, float a1, Uint32 a2) { return XdStack_Invoke("SDL_PlayHapticRumble", [&]() { return __real_SDL_PlayHapticRumble(a0, a1, a2); }); }
extern "C" bool __real_SDL_PollEvent(SDL_Event * a0);
extern "C" bool __wrap_SDL_PollEvent(SDL_Event * a0) { return XdStack_Invoke("SDL_PollEvent", [&]() { return __real_SDL_PollEvent(a0); }); }
extern "C" void __real_SDL_PumpEvents();
extern "C" void __wrap_SDL_PumpEvents() { return XdStack_Invoke("SDL_PumpEvents", [&]() { return __real_SDL_PumpEvents(); }); }
extern "C" bool __real_SDL_PushEvent(SDL_Event * a0);
extern "C" bool __wrap_SDL_PushEvent(SDL_Event * a0) { return XdStack_Invoke("SDL_PushEvent", [&]() { return __real_SDL_PushEvent(a0); }); }
extern "C" bool __real_SDL_PutAudioStreamData(SDL_AudioStream * a0, const void * a1, int a2);
extern "C" bool __wrap_SDL_PutAudioStreamData(SDL_AudioStream * a0, const void * a1, int a2) { return XdStack_Invoke("SDL_PutAudioStreamData", [&]() { return __real_SDL_PutAudioStreamData(a0, a1, a2); }); }
extern "C" void __real_SDL_QuitSubSystem(SDL_InitFlags a0);
extern "C" void __wrap_SDL_QuitSubSystem(SDL_InitFlags a0) { return XdStack_Invoke("SDL_QuitSubSystem", [&]() { return __real_SDL_QuitSubSystem(a0); }); }
extern "C" bool __real_SDL_RaiseWindow(SDL_Window * a0);
extern "C" bool __wrap_SDL_RaiseWindow(SDL_Window * a0) { return XdStack_Invoke("SDL_RaiseWindow", [&]() { return __real_SDL_RaiseWindow(a0); }); }
extern "C" size_t __real_SDL_ReadIO(SDL_IOStream * a0, void * a1, size_t a2);
extern "C" size_t __wrap_SDL_ReadIO(SDL_IOStream * a0, void * a1, size_t a2) { return XdStack_Invoke("SDL_ReadIO", [&]() { return __real_SDL_ReadIO(a0, a1, a2); }); }
extern "C" Uint32 __real_SDL_RegisterEvents(int a0);
extern "C" Uint32 __wrap_SDL_RegisterEvents(int a0) { return XdStack_Invoke("SDL_RegisterEvents", [&]() { return __real_SDL_RegisterEvents(a0); }); }
extern "C" void __real_SDL_RemoveEventWatch(SDL_EventFilter a0, void * a1);
extern "C" void __wrap_SDL_RemoveEventWatch(SDL_EventFilter a0, void * a1) { return XdStack_Invoke("SDL_RemoveEventWatch", [&]() { return __real_SDL_RemoveEventWatch(a0, a1); }); }
extern "C" bool __real_SDL_RemovePath(const char * a0);
extern "C" bool __wrap_SDL_RemovePath(const char * a0) { return XdStack_Invoke("SDL_RemovePath", [&]() { return __real_SDL_RemovePath(a0); }); }
extern "C" bool __real_SDL_RenamePath(const char * a0, const char * a1);
extern "C" bool __wrap_SDL_RenamePath(const char * a0, const char * a1) { return XdStack_Invoke("SDL_RenamePath", [&]() { return __real_SDL_RenamePath(a0, a1); }); }
extern "C" bool __real_SDL_RenderClear(SDL_Renderer * a0);
extern "C" bool __wrap_SDL_RenderClear(SDL_Renderer * a0) { return XdStack_Invoke("SDL_RenderClear", [&]() { return __real_SDL_RenderClear(a0); }); }
extern "C" bool __real_SDL_RenderClipEnabled(SDL_Renderer * a0);
extern "C" bool __wrap_SDL_RenderClipEnabled(SDL_Renderer * a0) { return XdStack_Invoke("SDL_RenderClipEnabled", [&]() { return __real_SDL_RenderClipEnabled(a0); }); }
extern "C" bool __real_SDL_RenderGeometryRaw(SDL_Renderer * a0, SDL_Texture * a1, const float * a2, int a3, const SDL_FColor * a4, int a5, const float * a6, int a7, int a8, const void * a9, int a10, int a11);
extern "C" bool __wrap_SDL_RenderGeometryRaw(SDL_Renderer * a0, SDL_Texture * a1, const float * a2, int a3, const SDL_FColor * a4, int a5, const float * a6, int a7, int a8, const void * a9, int a10, int a11) { return XdStack_Invoke("SDL_RenderGeometryRaw", [&]() { return __real_SDL_RenderGeometryRaw(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11); }); }
extern "C" bool __real_SDL_RenderPresent(SDL_Renderer * a0);
extern "C" bool __wrap_SDL_RenderPresent(SDL_Renderer * a0) { return XdStack_Invoke("SDL_RenderPresent", [&]() { return __real_SDL_RenderPresent(a0); }); }
extern "C" bool __real_SDL_RenderViewportSet(SDL_Renderer * a0);
extern "C" bool __wrap_SDL_RenderViewportSet(SDL_Renderer * a0) { return XdStack_Invoke("SDL_RenderViewportSet", [&]() { return __real_SDL_RenderViewportSet(a0); }); }
extern "C" bool __real_SDL_RestoreWindow(SDL_Window * a0);
extern "C" bool __wrap_SDL_RestoreWindow(SDL_Window * a0) { return XdStack_Invoke("SDL_RestoreWindow", [&]() { return __real_SDL_RestoreWindow(a0); }); }
extern "C" bool __real_SDL_ResumeAudioStreamDevice(SDL_AudioStream * a0);
extern "C" bool __wrap_SDL_ResumeAudioStreamDevice(SDL_AudioStream * a0) { return XdStack_Invoke("SDL_ResumeAudioStreamDevice", [&]() { return __real_SDL_ResumeAudioStreamDevice(a0); }); }
extern "C" bool __real_SDL_RumbleGamepad(SDL_Gamepad * a0, Uint16 a1, Uint16 a2, Uint32 a3);
extern "C" bool __wrap_SDL_RumbleGamepad(SDL_Gamepad * a0, Uint16 a1, Uint16 a2, Uint32 a3) { return XdStack_Invoke("SDL_RumbleGamepad", [&]() { return __real_SDL_RumbleGamepad(a0, a1, a2, a3); }); }
extern "C" bool __real_SDL_RunOnMainThread(SDL_MainThreadCallback a0, void * a1, bool a2);
extern "C" bool __wrap_SDL_RunOnMainThread(SDL_MainThreadCallback a0, void * a1, bool a2) { return XdStack_Invoke("SDL_RunOnMainThread", [&]() { return __real_SDL_RunOnMainThread(a0, a1, a2); }); }
extern "C" Sint64 __real_SDL_SeekIO(SDL_IOStream * a0, Sint64 a1, SDL_IOWhence a2);
extern "C" Sint64 __wrap_SDL_SeekIO(SDL_IOStream * a0, Sint64 a1, SDL_IOWhence a2) { return XdStack_Invoke("SDL_SeekIO", [&]() { return __real_SDL_SeekIO(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetAudioStreamFrequencyRatio(SDL_AudioStream * a0, float a1);
extern "C" bool __wrap_SDL_SetAudioStreamFrequencyRatio(SDL_AudioStream * a0, float a1) { return XdStack_Invoke("SDL_SetAudioStreamFrequencyRatio", [&]() { return __real_SDL_SetAudioStreamFrequencyRatio(a0, a1); }); }
extern "C" bool __real_SDL_SetAudioStreamGain(SDL_AudioStream * a0, float a1);
extern "C" bool __wrap_SDL_SetAudioStreamGain(SDL_AudioStream * a0, float a1) { return XdStack_Invoke("SDL_SetAudioStreamGain", [&]() { return __real_SDL_SetAudioStreamGain(a0, a1); }); }
extern "C" bool __real_SDL_SetAudioStreamGetCallback(SDL_AudioStream * a0, SDL_AudioStreamCallback a1, void * a2);
extern "C" bool __wrap_SDL_SetAudioStreamGetCallback(SDL_AudioStream * a0, SDL_AudioStreamCallback a1, void * a2) { return XdStack_Invoke("SDL_SetAudioStreamGetCallback", [&]() { return __real_SDL_SetAudioStreamGetCallback(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetBooleanProperty(SDL_PropertiesID a0, const char * a1, bool a2);
extern "C" bool __wrap_SDL_SetBooleanProperty(SDL_PropertiesID a0, const char * a1, bool a2) { return XdStack_Invoke("SDL_SetBooleanProperty", [&]() { return __real_SDL_SetBooleanProperty(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetClipboardText(const char * a0);
extern "C" bool __wrap_SDL_SetClipboardText(const char * a0) { return XdStack_Invoke("SDL_SetClipboardText", [&]() { return __real_SDL_SetClipboardText(a0); }); }
extern "C" bool __real_SDL_SetCurrentThreadPriority(SDL_ThreadPriority a0);
extern "C" bool __wrap_SDL_SetCurrentThreadPriority(SDL_ThreadPriority a0) { return XdStack_Invoke("SDL_SetCurrentThreadPriority", [&]() { return __real_SDL_SetCurrentThreadPriority(a0); }); }
extern "C" bool __real_SDL_SetCursor(SDL_Cursor * a0);
extern "C" bool __wrap_SDL_SetCursor(SDL_Cursor * a0) { return XdStack_Invoke("SDL_SetCursor", [&]() { return __real_SDL_SetCursor(a0); }); }
extern "C" bool __real_SDL_SetGamepadLED(SDL_Gamepad * a0, Uint8 a1, Uint8 a2, Uint8 a3);
extern "C" bool __wrap_SDL_SetGamepadLED(SDL_Gamepad * a0, Uint8 a1, Uint8 a2, Uint8 a3) { return XdStack_Invoke("SDL_SetGamepadLED", [&]() { return __real_SDL_SetGamepadLED(a0, a1, a2, a3); }); }
extern "C" bool __real_SDL_SetGamepadPlayerIndex(SDL_Gamepad * a0, int a1);
extern "C" bool __wrap_SDL_SetGamepadPlayerIndex(SDL_Gamepad * a0, int a1) { return XdStack_Invoke("SDL_SetGamepadPlayerIndex", [&]() { return __real_SDL_SetGamepadPlayerIndex(a0, a1); }); }
extern "C" bool __real_SDL_SetGamepadSensorEnabled(SDL_Gamepad * a0, SDL_SensorType a1, bool a2);
extern "C" bool __wrap_SDL_SetGamepadSensorEnabled(SDL_Gamepad * a0, SDL_SensorType a1, bool a2) { return XdStack_Invoke("SDL_SetGamepadSensorEnabled", [&]() { return __real_SDL_SetGamepadSensorEnabled(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetHint(const char * a0, const char * a1);
extern "C" bool __wrap_SDL_SetHint(const char * a0, const char * a1) { return XdStack_Invoke("SDL_SetHint", [&]() { return __real_SDL_SetHint(a0, a1); }); }
extern "C" bool __real_SDL_SetNumberProperty(SDL_PropertiesID a0, const char * a1, Sint64 a2);
extern "C" bool __wrap_SDL_SetNumberProperty(SDL_PropertiesID a0, const char * a1, Sint64 a2) { return XdStack_Invoke("SDL_SetNumberProperty", [&]() { return __real_SDL_SetNumberProperty(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetPointerProperty(SDL_PropertiesID a0, const char * a1, void * a2);
extern "C" bool __wrap_SDL_SetPointerProperty(SDL_PropertiesID a0, const char * a1, void * a2) { return XdStack_Invoke("SDL_SetPointerProperty", [&]() { return __real_SDL_SetPointerProperty(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetRenderClipRect(SDL_Renderer * a0, const SDL_Rect * a1);
extern "C" bool __wrap_SDL_SetRenderClipRect(SDL_Renderer * a0, const SDL_Rect * a1) { return XdStack_Invoke("SDL_SetRenderClipRect", [&]() { return __real_SDL_SetRenderClipRect(a0, a1); }); }
extern "C" bool __real_SDL_SetRenderLogicalPresentation(SDL_Renderer * a0, int a1, int a2, SDL_RendererLogicalPresentation a3);
extern "C" bool __wrap_SDL_SetRenderLogicalPresentation(SDL_Renderer * a0, int a1, int a2, SDL_RendererLogicalPresentation a3) { return XdStack_Invoke("SDL_SetRenderLogicalPresentation", [&]() { return __real_SDL_SetRenderLogicalPresentation(a0, a1, a2, a3); }); }
extern "C" bool __real_SDL_SetRenderScale(SDL_Renderer * a0, float a1, float a2);
extern "C" bool __wrap_SDL_SetRenderScale(SDL_Renderer * a0, float a1, float a2) { return XdStack_Invoke("SDL_SetRenderScale", [&]() { return __real_SDL_SetRenderScale(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetRenderViewport(SDL_Renderer * a0, const SDL_Rect * a1);
extern "C" bool __wrap_SDL_SetRenderViewport(SDL_Renderer * a0, const SDL_Rect * a1) { return XdStack_Invoke("SDL_SetRenderViewport", [&]() { return __real_SDL_SetRenderViewport(a0, a1); }); }
extern "C" bool __real_SDL_SetStringProperty(SDL_PropertiesID a0, const char * a1, const char * a2);
extern "C" bool __wrap_SDL_SetStringProperty(SDL_PropertiesID a0, const char * a1, const char * a2) { return XdStack_Invoke("SDL_SetStringProperty", [&]() { return __real_SDL_SetStringProperty(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetTextInputArea(SDL_Window * a0, const SDL_Rect * a1, int a2);
extern "C" bool __wrap_SDL_SetTextInputArea(SDL_Window * a0, const SDL_Rect * a1, int a2) { return XdStack_Invoke("SDL_SetTextInputArea", [&]() { return __real_SDL_SetTextInputArea(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetTextureBlendMode(SDL_Texture * a0, SDL_BlendMode a1);
extern "C" bool __wrap_SDL_SetTextureBlendMode(SDL_Texture * a0, SDL_BlendMode a1) { return XdStack_Invoke("SDL_SetTextureBlendMode", [&]() { return __real_SDL_SetTextureBlendMode(a0, a1); }); }
extern "C" bool __real_SDL_SetTextureScaleMode(SDL_Texture * a0, SDL_ScaleMode a1);
extern "C" bool __wrap_SDL_SetTextureScaleMode(SDL_Texture * a0, SDL_ScaleMode a1) { return XdStack_Invoke("SDL_SetTextureScaleMode", [&]() { return __real_SDL_SetTextureScaleMode(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowBordered(SDL_Window * a0, bool a1);
extern "C" bool __wrap_SDL_SetWindowBordered(SDL_Window * a0, bool a1) { return XdStack_Invoke("SDL_SetWindowBordered", [&]() { return __real_SDL_SetWindowBordered(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowFullscreen(SDL_Window * a0, bool a1);
extern "C" bool __wrap_SDL_SetWindowFullscreen(SDL_Window * a0, bool a1) { return XdStack_Invoke("SDL_SetWindowFullscreen", [&]() { return __real_SDL_SetWindowFullscreen(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowFullscreenMode(SDL_Window * a0, const SDL_DisplayMode * a1);
extern "C" bool __wrap_SDL_SetWindowFullscreenMode(SDL_Window * a0, const SDL_DisplayMode * a1) { return XdStack_Invoke("SDL_SetWindowFullscreenMode", [&]() { return __real_SDL_SetWindowFullscreenMode(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowIcon(SDL_Window * a0, SDL_Surface * a1);
extern "C" bool __wrap_SDL_SetWindowIcon(SDL_Window * a0, SDL_Surface * a1) { return XdStack_Invoke("SDL_SetWindowIcon", [&]() { return __real_SDL_SetWindowIcon(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowMinimumSize(SDL_Window * a0, int a1, int a2);
extern "C" bool __wrap_SDL_SetWindowMinimumSize(SDL_Window * a0, int a1, int a2) { return XdStack_Invoke("SDL_SetWindowMinimumSize", [&]() { return __real_SDL_SetWindowMinimumSize(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetWindowOpacity(SDL_Window * a0, float a1);
extern "C" bool __wrap_SDL_SetWindowOpacity(SDL_Window * a0, float a1) { return XdStack_Invoke("SDL_SetWindowOpacity", [&]() { return __real_SDL_SetWindowOpacity(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowParent(SDL_Window * a0, SDL_Window * a1);
extern "C" bool __wrap_SDL_SetWindowParent(SDL_Window * a0, SDL_Window * a1) { return XdStack_Invoke("SDL_SetWindowParent", [&]() { return __real_SDL_SetWindowParent(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowPosition(SDL_Window * a0, int a1, int a2);
extern "C" bool __wrap_SDL_SetWindowPosition(SDL_Window * a0, int a1, int a2) { return XdStack_Invoke("SDL_SetWindowPosition", [&]() { return __real_SDL_SetWindowPosition(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetWindowRelativeMouseMode(SDL_Window * a0, bool a1);
extern "C" bool __wrap_SDL_SetWindowRelativeMouseMode(SDL_Window * a0, bool a1) { return XdStack_Invoke("SDL_SetWindowRelativeMouseMode", [&]() { return __real_SDL_SetWindowRelativeMouseMode(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowResizable(SDL_Window * a0, bool a1);
extern "C" bool __wrap_SDL_SetWindowResizable(SDL_Window * a0, bool a1) { return XdStack_Invoke("SDL_SetWindowResizable", [&]() { return __real_SDL_SetWindowResizable(a0, a1); }); }
extern "C" bool __real_SDL_SetWindowSize(SDL_Window * a0, int a1, int a2);
extern "C" bool __wrap_SDL_SetWindowSize(SDL_Window * a0, int a1, int a2) { return XdStack_Invoke("SDL_SetWindowSize", [&]() { return __real_SDL_SetWindowSize(a0, a1, a2); }); }
extern "C" bool __real_SDL_SetWindowTitle(SDL_Window * a0, const char * a1);
extern "C" bool __wrap_SDL_SetWindowTitle(SDL_Window * a0, const char * a1) { return XdStack_Invoke("SDL_SetWindowTitle", [&]() { return __real_SDL_SetWindowTitle(a0, a1); }); }
extern "C" bool __real_SDL_ShowCursor();
extern "C" bool __wrap_SDL_ShowCursor() { return XdStack_Invoke("SDL_ShowCursor", [&]() { return __real_SDL_ShowCursor(); }); }
extern "C" bool __real_SDL_ShowMessageBox(const SDL_MessageBoxData * a0, int * a1);
extern "C" bool __wrap_SDL_ShowMessageBox(const SDL_MessageBoxData * a0, int * a1) { return XdStack_Invoke("SDL_ShowMessageBox", [&]() { return __real_SDL_ShowMessageBox(a0, a1); }); }
extern "C" void __real_SDL_ShowOpenFileDialog(SDL_DialogFileCallback a0, void * a1, SDL_Window * a2, const SDL_DialogFileFilter * a3, int a4, const char * a5, bool a6);
extern "C" void __wrap_SDL_ShowOpenFileDialog(SDL_DialogFileCallback a0, void * a1, SDL_Window * a2, const SDL_DialogFileFilter * a3, int a4, const char * a5, bool a6) { return XdStack_Invoke("SDL_ShowOpenFileDialog", [&]() { return __real_SDL_ShowOpenFileDialog(a0, a1, a2, a3, a4, a5, a6); }); }
extern "C" bool __real_SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags a0, const char * a1, const char * a2, SDL_Window * a3);
extern "C" bool __wrap_SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags a0, const char * a1, const char * a2, SDL_Window * a3) { return XdStack_Invoke("SDL_ShowSimpleMessageBox", [&]() { return __real_SDL_ShowSimpleMessageBox(a0, a1, a2, a3); }); }
extern "C" bool __real_SDL_ShowWindow(SDL_Window * a0);
extern "C" bool __wrap_SDL_ShowWindow(SDL_Window * a0) { return XdStack_Invoke("SDL_ShowWindow", [&]() { return __real_SDL_ShowWindow(a0); }); }
extern "C" bool __real_SDL_StartTextInput(SDL_Window * a0);
extern "C" bool __wrap_SDL_StartTextInput(SDL_Window * a0) { return XdStack_Invoke("SDL_StartTextInput", [&]() { return __real_SDL_StartTextInput(a0); }); }
extern "C" bool __real_SDL_StartTextInputWithProperties(SDL_Window * a0, SDL_PropertiesID a1);
extern "C" bool __wrap_SDL_StartTextInputWithProperties(SDL_Window * a0, SDL_PropertiesID a1) { return XdStack_Invoke("SDL_StartTextInputWithProperties", [&]() { return __real_SDL_StartTextInputWithProperties(a0, a1); }); }
extern "C" bool __real_SDL_StopHapticRumble(SDL_Haptic * a0);
extern "C" bool __wrap_SDL_StopHapticRumble(SDL_Haptic * a0) { return XdStack_Invoke("SDL_StopHapticRumble", [&]() { return __real_SDL_StopHapticRumble(a0); }); }
extern "C" bool __real_SDL_StopTextInput(SDL_Window * a0);
extern "C" bool __wrap_SDL_StopTextInput(SDL_Window * a0) { return XdStack_Invoke("SDL_StopTextInput", [&]() { return __real_SDL_StopTextInput(a0); }); }
extern "C" SDL_GUID __real_SDL_StringToGUID(const char * a0);
extern "C" SDL_GUID __wrap_SDL_StringToGUID(const char * a0) { return XdStack_Invoke("SDL_StringToGUID", [&]() { return __real_SDL_StringToGUID(a0); }); }
extern "C" bool __real_SDL_SyncWindow(SDL_Window * a0);
extern "C" bool __wrap_SDL_SyncWindow(SDL_Window * a0) { return XdStack_Invoke("SDL_SyncWindow", [&]() { return __real_SDL_SyncWindow(a0); }); }
extern "C" void __real_SDL_UpdateSensors();
extern "C" void __wrap_SDL_UpdateSensors() { return XdStack_Invoke("SDL_UpdateSensors", [&]() { return __real_SDL_UpdateSensors(); }); }
extern "C" bool __real_SDL_UpdateTexture(SDL_Texture * a0, const SDL_Rect * a1, const void * a2, int a3);
extern "C" bool __wrap_SDL_UpdateTexture(SDL_Texture * a0, const SDL_Rect * a1, const void * a2, int a3) { return XdStack_Invoke("SDL_UpdateTexture", [&]() { return __real_SDL_UpdateTexture(a0, a1, a2, a3); }); }
extern "C" bool __real_SDL_Vulkan_CreateSurface(SDL_Window * a0, VkInstance a1, const struct VkAllocationCallbacks * a2, VkSurfaceKHR * a3);
extern "C" bool __wrap_SDL_Vulkan_CreateSurface(SDL_Window * a0, VkInstance a1, const struct VkAllocationCallbacks * a2, VkSurfaceKHR * a3) { return XdStack_Invoke("SDL_Vulkan_CreateSurface", [&]() { return __real_SDL_Vulkan_CreateSurface(a0, a1, a2, a3); }); }
extern "C" bool __real_SDL_WaitEvent(SDL_Event * a0);
extern "C" bool __wrap_SDL_WaitEvent(SDL_Event * a0) { return XdStack_Invoke("SDL_WaitEvent", [&]() { return __real_SDL_WaitEvent(a0); }); }
extern "C" void __real_SDL_WarpMouseInWindow(SDL_Window * a0, float a1, float a2);
extern "C" void __wrap_SDL_WarpMouseInWindow(SDL_Window * a0, float a1, float a2) { return XdStack_Invoke("SDL_WarpMouseInWindow", [&]() { return __real_SDL_WarpMouseInWindow(a0, a1, a2); }); }
extern "C" SDL_InitFlags __real_SDL_WasInit(SDL_InitFlags a0);
extern "C" SDL_InitFlags __wrap_SDL_WasInit(SDL_InitFlags a0) { return XdStack_Invoke("SDL_WasInit", [&]() { return __real_SDL_WasInit(a0); }); }
extern "C" size_t __real_SDL_WriteIO(SDL_IOStream * a0, const void * a1, size_t a2);
extern "C" size_t __wrap_SDL_WriteIO(SDL_IOStream * a0, const void * a1, size_t a2) { return XdStack_Invoke("SDL_WriteIO", [&]() { return __real_SDL_WriteIO(a0, a1, a2); }); }
extern "C" void __real_SDL_free(void * a0);
extern "C" void __wrap_SDL_free(void * a0) { return XdStack_Invoke("SDL_free", [&]() { return __real_SDL_free(a0); }); }
extern "C" Uint32 __real_SDL_rand_bits();
extern "C" Uint32 __wrap_SDL_rand_bits() { return XdStack_Invoke("SDL_rand_bits", [&]() { return __real_SDL_rand_bits(); }); }
extern "C" char * __real_SDL_strdup(const char * a0);
extern "C" char * __wrap_SDL_strdup(const char * a0) { return XdStack_Invoke("SDL_strdup", [&]() { return __real_SDL_strdup(a0); }); }
extern "C" void __wrap_SDL_Log(const char* fmt, ...) { va_list ap; va_start(ap,fmt); XdStack_Invoke("SDL_Log", [&] { SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION,SDL_LOG_PRIORITY_INFO,fmt,ap); }); va_end(ap); }
extern "C" bool __wrap_SDL_SetError(const char* fmt, ...) { va_list ap; va_start(ap,fmt); bool r=XdStack_Invoke("SDL_SetError", [&] { return SDL_SetErrorV(fmt,ap); }); va_end(ap); return r; }
static thread_local JNIEnv* realEnv=nullptr;
static thread_local JNIEnv proxyEnv;
static thread_local JNINativeInterface proxyTable;
static jint proxy_GetVersion(JNIEnv * a0) { return XdStack_Invoke("JNI GetVersion", [&] { return realEnv->functions->GetVersion(realEnv); }); }
static jclass proxy_DefineClass(JNIEnv * a0, const char * a1, jobject a2, const jbyte * a3, jsize a4) { return XdStack_Invoke("JNI DefineClass", [&] { return realEnv->functions->DefineClass(realEnv, a1, a2, a3, a4); }); }
static jclass proxy_FindClass(JNIEnv * a0, const char * a1) { return XdStack_Invoke("JNI FindClass", [&] { return realEnv->functions->FindClass(realEnv, a1); }); }
static jmethodID proxy_FromReflectedMethod(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI FromReflectedMethod", [&] { return realEnv->functions->FromReflectedMethod(realEnv, a1); }); }
static jfieldID proxy_FromReflectedField(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI FromReflectedField", [&] { return realEnv->functions->FromReflectedField(realEnv, a1); }); }
static jobject proxy_ToReflectedMethod(JNIEnv * a0, jclass a1, jmethodID a2, jboolean a3) { return XdStack_Invoke("JNI ToReflectedMethod", [&] { return realEnv->functions->ToReflectedMethod(realEnv, a1, a2, a3); }); }
static jclass proxy_GetSuperclass(JNIEnv * a0, jclass a1) { return XdStack_Invoke("JNI GetSuperclass", [&] { return realEnv->functions->GetSuperclass(realEnv, a1); }); }
static jboolean proxy_IsAssignableFrom(JNIEnv * a0, jclass a1, jclass a2) { return XdStack_Invoke("JNI IsAssignableFrom", [&] { return realEnv->functions->IsAssignableFrom(realEnv, a1, a2); }); }
static jobject proxy_ToReflectedField(JNIEnv * a0, jclass a1, jfieldID a2, jboolean a3) { return XdStack_Invoke("JNI ToReflectedField", [&] { return realEnv->functions->ToReflectedField(realEnv, a1, a2, a3); }); }
static jint proxy_Throw(JNIEnv * a0, jthrowable a1) { return XdStack_Invoke("JNI Throw", [&] { return realEnv->functions->Throw(realEnv, a1); }); }
static jint proxy_ThrowNew(JNIEnv * a0, jclass a1, const char * a2) { return XdStack_Invoke("JNI ThrowNew", [&] { return realEnv->functions->ThrowNew(realEnv, a1, a2); }); }
static jthrowable proxy_ExceptionOccurred(JNIEnv * a0) { return XdStack_Invoke("JNI ExceptionOccurred", [&] { return realEnv->functions->ExceptionOccurred(realEnv); }); }
static void proxy_ExceptionDescribe(JNIEnv * a0) { return XdStack_Invoke("JNI ExceptionDescribe", [&] { return realEnv->functions->ExceptionDescribe(realEnv); }); }
static void proxy_ExceptionClear(JNIEnv * a0) { return XdStack_Invoke("JNI ExceptionClear", [&] { return realEnv->functions->ExceptionClear(realEnv); }); }
static void proxy_FatalError(JNIEnv * a0, const char * a1) { return XdStack_Invoke("JNI FatalError", [&] { return realEnv->functions->FatalError(realEnv, a1); }); }
static jint proxy_PushLocalFrame(JNIEnv * a0, jint a1) { return XdStack_Invoke("JNI PushLocalFrame", [&] { return realEnv->functions->PushLocalFrame(realEnv, a1); }); }
static jobject proxy_PopLocalFrame(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI PopLocalFrame", [&] { return realEnv->functions->PopLocalFrame(realEnv, a1); }); }
static jobject proxy_NewGlobalRef(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI NewGlobalRef", [&] { return realEnv->functions->NewGlobalRef(realEnv, a1); }); }
static void proxy_DeleteGlobalRef(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI DeleteGlobalRef", [&] { return realEnv->functions->DeleteGlobalRef(realEnv, a1); }); }
static void proxy_DeleteLocalRef(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI DeleteLocalRef", [&] { return realEnv->functions->DeleteLocalRef(realEnv, a1); }); }
static jboolean proxy_IsSameObject(JNIEnv * a0, jobject a1, jobject a2) { return XdStack_Invoke("JNI IsSameObject", [&] { return realEnv->functions->IsSameObject(realEnv, a1, a2); }); }
static jobject proxy_NewLocalRef(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI NewLocalRef", [&] { return realEnv->functions->NewLocalRef(realEnv, a1); }); }
static jint proxy_EnsureLocalCapacity(JNIEnv * a0, jint a1) { return XdStack_Invoke("JNI EnsureLocalCapacity", [&] { return realEnv->functions->EnsureLocalCapacity(realEnv, a1); }); }
static jobject proxy_AllocObject(JNIEnv * a0, jclass a1) { return XdStack_Invoke("JNI AllocObject", [&] { return realEnv->functions->AllocObject(realEnv, a1); }); }
static jobject proxy_NewObject(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI NewObject", [&] { return realEnv->functions->NewObjectV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jobject proxy_NewObjectV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI NewObjectV", [&] { return realEnv->functions->NewObjectV(realEnv, a1, a2, a3); }); }
static jobject proxy_NewObjectA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI NewObjectA", [&] { return realEnv->functions->NewObjectA(realEnv, a1, a2, a3); }); }
static jclass proxy_GetObjectClass(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI GetObjectClass", [&] { return realEnv->functions->GetObjectClass(realEnv, a1); }); }
static jboolean proxy_IsInstanceOf(JNIEnv * a0, jobject a1, jclass a2) { return XdStack_Invoke("JNI IsInstanceOf", [&] { return realEnv->functions->IsInstanceOf(realEnv, a1, a2); }); }
static jmethodID proxy_GetMethodID(JNIEnv * a0, jclass a1, const char * a2, const char * a3) { return XdStack_Invoke("JNI GetMethodID", [&] { return realEnv->functions->GetMethodID(realEnv, a1, a2, a3); }); }
static jobject proxy_CallObjectMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallObjectMethod", [&] { return realEnv->functions->CallObjectMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jobject proxy_CallObjectMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallObjectMethodV", [&] { return realEnv->functions->CallObjectMethodV(realEnv, a1, a2, a3); }); }
static jobject proxy_CallObjectMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallObjectMethodA", [&] { return realEnv->functions->CallObjectMethodA(realEnv, a1, a2, a3); }); }
static jboolean proxy_CallBooleanMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallBooleanMethod", [&] { return realEnv->functions->CallBooleanMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jboolean proxy_CallBooleanMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallBooleanMethodV", [&] { return realEnv->functions->CallBooleanMethodV(realEnv, a1, a2, a3); }); }
static jboolean proxy_CallBooleanMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallBooleanMethodA", [&] { return realEnv->functions->CallBooleanMethodA(realEnv, a1, a2, a3); }); }
static jbyte proxy_CallByteMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallByteMethod", [&] { return realEnv->functions->CallByteMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jbyte proxy_CallByteMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallByteMethodV", [&] { return realEnv->functions->CallByteMethodV(realEnv, a1, a2, a3); }); }
static jbyte proxy_CallByteMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallByteMethodA", [&] { return realEnv->functions->CallByteMethodA(realEnv, a1, a2, a3); }); }
static jchar proxy_CallCharMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallCharMethod", [&] { return realEnv->functions->CallCharMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jchar proxy_CallCharMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallCharMethodV", [&] { return realEnv->functions->CallCharMethodV(realEnv, a1, a2, a3); }); }
static jchar proxy_CallCharMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallCharMethodA", [&] { return realEnv->functions->CallCharMethodA(realEnv, a1, a2, a3); }); }
static jshort proxy_CallShortMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallShortMethod", [&] { return realEnv->functions->CallShortMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jshort proxy_CallShortMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallShortMethodV", [&] { return realEnv->functions->CallShortMethodV(realEnv, a1, a2, a3); }); }
static jshort proxy_CallShortMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallShortMethodA", [&] { return realEnv->functions->CallShortMethodA(realEnv, a1, a2, a3); }); }
static jint proxy_CallIntMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallIntMethod", [&] { return realEnv->functions->CallIntMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jint proxy_CallIntMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallIntMethodV", [&] { return realEnv->functions->CallIntMethodV(realEnv, a1, a2, a3); }); }
static jint proxy_CallIntMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallIntMethodA", [&] { return realEnv->functions->CallIntMethodA(realEnv, a1, a2, a3); }); }
static jlong proxy_CallLongMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallLongMethod", [&] { return realEnv->functions->CallLongMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jlong proxy_CallLongMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallLongMethodV", [&] { return realEnv->functions->CallLongMethodV(realEnv, a1, a2, a3); }); }
static jlong proxy_CallLongMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallLongMethodA", [&] { return realEnv->functions->CallLongMethodA(realEnv, a1, a2, a3); }); }
static jfloat proxy_CallFloatMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallFloatMethod", [&] { return realEnv->functions->CallFloatMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jfloat proxy_CallFloatMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallFloatMethodV", [&] { return realEnv->functions->CallFloatMethodV(realEnv, a1, a2, a3); }); }
static jfloat proxy_CallFloatMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallFloatMethodA", [&] { return realEnv->functions->CallFloatMethodA(realEnv, a1, a2, a3); }); }
static jdouble proxy_CallDoubleMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallDoubleMethod", [&] { return realEnv->functions->CallDoubleMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jdouble proxy_CallDoubleMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallDoubleMethodV", [&] { return realEnv->functions->CallDoubleMethodV(realEnv, a1, a2, a3); }); }
static jdouble proxy_CallDoubleMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallDoubleMethodA", [&] { return realEnv->functions->CallDoubleMethodA(realEnv, a1, a2, a3); }); }
static void proxy_CallVoidMethod(JNIEnv * a0, jobject a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);XdStack_Invoke("JNI CallVoidMethod", [&] { realEnv->functions->CallVoidMethodV(realEnv, a1, a2, ap); }); va_end(ap); }
static void proxy_CallVoidMethodV(JNIEnv * a0, jobject a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallVoidMethodV", [&] { return realEnv->functions->CallVoidMethodV(realEnv, a1, a2, a3); }); }
static void proxy_CallVoidMethodA(JNIEnv * a0, jobject a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallVoidMethodA", [&] { return realEnv->functions->CallVoidMethodA(realEnv, a1, a2, a3); }); }
static jobject proxy_CallNonvirtualObjectMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualObjectMethod", [&] { return realEnv->functions->CallNonvirtualObjectMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jobject proxy_CallNonvirtualObjectMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualObjectMethodV", [&] { return realEnv->functions->CallNonvirtualObjectMethodV(realEnv, a1, a2, a3, a4); }); }
static jobject proxy_CallNonvirtualObjectMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualObjectMethodA", [&] { return realEnv->functions->CallNonvirtualObjectMethodA(realEnv, a1, a2, a3, a4); }); }
static jboolean proxy_CallNonvirtualBooleanMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualBooleanMethod", [&] { return realEnv->functions->CallNonvirtualBooleanMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jboolean proxy_CallNonvirtualBooleanMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualBooleanMethodV", [&] { return realEnv->functions->CallNonvirtualBooleanMethodV(realEnv, a1, a2, a3, a4); }); }
static jboolean proxy_CallNonvirtualBooleanMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualBooleanMethodA", [&] { return realEnv->functions->CallNonvirtualBooleanMethodA(realEnv, a1, a2, a3, a4); }); }
static jbyte proxy_CallNonvirtualByteMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualByteMethod", [&] { return realEnv->functions->CallNonvirtualByteMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jbyte proxy_CallNonvirtualByteMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualByteMethodV", [&] { return realEnv->functions->CallNonvirtualByteMethodV(realEnv, a1, a2, a3, a4); }); }
static jbyte proxy_CallNonvirtualByteMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualByteMethodA", [&] { return realEnv->functions->CallNonvirtualByteMethodA(realEnv, a1, a2, a3, a4); }); }
static jchar proxy_CallNonvirtualCharMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualCharMethod", [&] { return realEnv->functions->CallNonvirtualCharMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jchar proxy_CallNonvirtualCharMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualCharMethodV", [&] { return realEnv->functions->CallNonvirtualCharMethodV(realEnv, a1, a2, a3, a4); }); }
static jchar proxy_CallNonvirtualCharMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualCharMethodA", [&] { return realEnv->functions->CallNonvirtualCharMethodA(realEnv, a1, a2, a3, a4); }); }
static jshort proxy_CallNonvirtualShortMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualShortMethod", [&] { return realEnv->functions->CallNonvirtualShortMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jshort proxy_CallNonvirtualShortMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualShortMethodV", [&] { return realEnv->functions->CallNonvirtualShortMethodV(realEnv, a1, a2, a3, a4); }); }
static jshort proxy_CallNonvirtualShortMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualShortMethodA", [&] { return realEnv->functions->CallNonvirtualShortMethodA(realEnv, a1, a2, a3, a4); }); }
static jint proxy_CallNonvirtualIntMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualIntMethod", [&] { return realEnv->functions->CallNonvirtualIntMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jint proxy_CallNonvirtualIntMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualIntMethodV", [&] { return realEnv->functions->CallNonvirtualIntMethodV(realEnv, a1, a2, a3, a4); }); }
static jint proxy_CallNonvirtualIntMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualIntMethodA", [&] { return realEnv->functions->CallNonvirtualIntMethodA(realEnv, a1, a2, a3, a4); }); }
static jlong proxy_CallNonvirtualLongMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualLongMethod", [&] { return realEnv->functions->CallNonvirtualLongMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jlong proxy_CallNonvirtualLongMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualLongMethodV", [&] { return realEnv->functions->CallNonvirtualLongMethodV(realEnv, a1, a2, a3, a4); }); }
static jlong proxy_CallNonvirtualLongMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualLongMethodA", [&] { return realEnv->functions->CallNonvirtualLongMethodA(realEnv, a1, a2, a3, a4); }); }
static jfloat proxy_CallNonvirtualFloatMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualFloatMethod", [&] { return realEnv->functions->CallNonvirtualFloatMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jfloat proxy_CallNonvirtualFloatMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualFloatMethodV", [&] { return realEnv->functions->CallNonvirtualFloatMethodV(realEnv, a1, a2, a3, a4); }); }
static jfloat proxy_CallNonvirtualFloatMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualFloatMethodA", [&] { return realEnv->functions->CallNonvirtualFloatMethodA(realEnv, a1, a2, a3, a4); }); }
static jdouble proxy_CallNonvirtualDoubleMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);auto result=XdStack_Invoke("JNI CallNonvirtualDoubleMethod", [&] { return realEnv->functions->CallNonvirtualDoubleMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); return result; }
static jdouble proxy_CallNonvirtualDoubleMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualDoubleMethodV", [&] { return realEnv->functions->CallNonvirtualDoubleMethodV(realEnv, a1, a2, a3, a4); }); }
static jdouble proxy_CallNonvirtualDoubleMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualDoubleMethodA", [&] { return realEnv->functions->CallNonvirtualDoubleMethodA(realEnv, a1, a2, a3, a4); }); }
static void proxy_CallNonvirtualVoidMethod(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, ...) { va_list ap; va_start(ap,a3);XdStack_Invoke("JNI CallNonvirtualVoidMethod", [&] { realEnv->functions->CallNonvirtualVoidMethodV(realEnv, a1, a2, a3, ap); }); va_end(ap); }
static void proxy_CallNonvirtualVoidMethodV(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, va_list a4) { return XdStack_Invoke("JNI CallNonvirtualVoidMethodV", [&] { return realEnv->functions->CallNonvirtualVoidMethodV(realEnv, a1, a2, a3, a4); }); }
static void proxy_CallNonvirtualVoidMethodA(JNIEnv * a0, jobject a1, jclass a2, jmethodID a3, const jvalue * a4) { return XdStack_Invoke("JNI CallNonvirtualVoidMethodA", [&] { return realEnv->functions->CallNonvirtualVoidMethodA(realEnv, a1, a2, a3, a4); }); }
static jfieldID proxy_GetFieldID(JNIEnv * a0, jclass a1, const char * a2, const char * a3) { return XdStack_Invoke("JNI GetFieldID", [&] { return realEnv->functions->GetFieldID(realEnv, a1, a2, a3); }); }
static jobject proxy_GetObjectField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetObjectField", [&] { return realEnv->functions->GetObjectField(realEnv, a1, a2); }); }
static jboolean proxy_GetBooleanField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetBooleanField", [&] { return realEnv->functions->GetBooleanField(realEnv, a1, a2); }); }
static jbyte proxy_GetByteField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetByteField", [&] { return realEnv->functions->GetByteField(realEnv, a1, a2); }); }
static jchar proxy_GetCharField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetCharField", [&] { return realEnv->functions->GetCharField(realEnv, a1, a2); }); }
static jshort proxy_GetShortField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetShortField", [&] { return realEnv->functions->GetShortField(realEnv, a1, a2); }); }
static jint proxy_GetIntField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetIntField", [&] { return realEnv->functions->GetIntField(realEnv, a1, a2); }); }
static jlong proxy_GetLongField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetLongField", [&] { return realEnv->functions->GetLongField(realEnv, a1, a2); }); }
static jfloat proxy_GetFloatField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetFloatField", [&] { return realEnv->functions->GetFloatField(realEnv, a1, a2); }); }
static jdouble proxy_GetDoubleField(JNIEnv * a0, jobject a1, jfieldID a2) { return XdStack_Invoke("JNI GetDoubleField", [&] { return realEnv->functions->GetDoubleField(realEnv, a1, a2); }); }
static void proxy_SetObjectField(JNIEnv * a0, jobject a1, jfieldID a2, jobject a3) { return XdStack_Invoke("JNI SetObjectField", [&] { return realEnv->functions->SetObjectField(realEnv, a1, a2, a3); }); }
static void proxy_SetBooleanField(JNIEnv * a0, jobject a1, jfieldID a2, jboolean a3) { return XdStack_Invoke("JNI SetBooleanField", [&] { return realEnv->functions->SetBooleanField(realEnv, a1, a2, a3); }); }
static void proxy_SetByteField(JNIEnv * a0, jobject a1, jfieldID a2, jbyte a3) { return XdStack_Invoke("JNI SetByteField", [&] { return realEnv->functions->SetByteField(realEnv, a1, a2, a3); }); }
static void proxy_SetCharField(JNIEnv * a0, jobject a1, jfieldID a2, jchar a3) { return XdStack_Invoke("JNI SetCharField", [&] { return realEnv->functions->SetCharField(realEnv, a1, a2, a3); }); }
static void proxy_SetShortField(JNIEnv * a0, jobject a1, jfieldID a2, jshort a3) { return XdStack_Invoke("JNI SetShortField", [&] { return realEnv->functions->SetShortField(realEnv, a1, a2, a3); }); }
static void proxy_SetIntField(JNIEnv * a0, jobject a1, jfieldID a2, jint a3) { return XdStack_Invoke("JNI SetIntField", [&] { return realEnv->functions->SetIntField(realEnv, a1, a2, a3); }); }
static void proxy_SetLongField(JNIEnv * a0, jobject a1, jfieldID a2, jlong a3) { return XdStack_Invoke("JNI SetLongField", [&] { return realEnv->functions->SetLongField(realEnv, a1, a2, a3); }); }
static void proxy_SetFloatField(JNIEnv * a0, jobject a1, jfieldID a2, jfloat a3) { return XdStack_Invoke("JNI SetFloatField", [&] { return realEnv->functions->SetFloatField(realEnv, a1, a2, a3); }); }
static void proxy_SetDoubleField(JNIEnv * a0, jobject a1, jfieldID a2, jdouble a3) { return XdStack_Invoke("JNI SetDoubleField", [&] { return realEnv->functions->SetDoubleField(realEnv, a1, a2, a3); }); }
static jmethodID proxy_GetStaticMethodID(JNIEnv * a0, jclass a1, const char * a2, const char * a3) { return XdStack_Invoke("JNI GetStaticMethodID", [&] { return realEnv->functions->GetStaticMethodID(realEnv, a1, a2, a3); }); }
static jobject proxy_CallStaticObjectMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticObjectMethod", [&] { return realEnv->functions->CallStaticObjectMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jobject proxy_CallStaticObjectMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticObjectMethodV", [&] { return realEnv->functions->CallStaticObjectMethodV(realEnv, a1, a2, a3); }); }
static jobject proxy_CallStaticObjectMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticObjectMethodA", [&] { return realEnv->functions->CallStaticObjectMethodA(realEnv, a1, a2, a3); }); }
static jboolean proxy_CallStaticBooleanMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticBooleanMethod", [&] { return realEnv->functions->CallStaticBooleanMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jboolean proxy_CallStaticBooleanMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticBooleanMethodV", [&] { return realEnv->functions->CallStaticBooleanMethodV(realEnv, a1, a2, a3); }); }
static jboolean proxy_CallStaticBooleanMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticBooleanMethodA", [&] { return realEnv->functions->CallStaticBooleanMethodA(realEnv, a1, a2, a3); }); }
static jbyte proxy_CallStaticByteMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticByteMethod", [&] { return realEnv->functions->CallStaticByteMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jbyte proxy_CallStaticByteMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticByteMethodV", [&] { return realEnv->functions->CallStaticByteMethodV(realEnv, a1, a2, a3); }); }
static jbyte proxy_CallStaticByteMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticByteMethodA", [&] { return realEnv->functions->CallStaticByteMethodA(realEnv, a1, a2, a3); }); }
static jchar proxy_CallStaticCharMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticCharMethod", [&] { return realEnv->functions->CallStaticCharMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jchar proxy_CallStaticCharMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticCharMethodV", [&] { return realEnv->functions->CallStaticCharMethodV(realEnv, a1, a2, a3); }); }
static jchar proxy_CallStaticCharMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticCharMethodA", [&] { return realEnv->functions->CallStaticCharMethodA(realEnv, a1, a2, a3); }); }
static jshort proxy_CallStaticShortMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticShortMethod", [&] { return realEnv->functions->CallStaticShortMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jshort proxy_CallStaticShortMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticShortMethodV", [&] { return realEnv->functions->CallStaticShortMethodV(realEnv, a1, a2, a3); }); }
static jshort proxy_CallStaticShortMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticShortMethodA", [&] { return realEnv->functions->CallStaticShortMethodA(realEnv, a1, a2, a3); }); }
static jint proxy_CallStaticIntMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticIntMethod", [&] { return realEnv->functions->CallStaticIntMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jint proxy_CallStaticIntMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticIntMethodV", [&] { return realEnv->functions->CallStaticIntMethodV(realEnv, a1, a2, a3); }); }
static jint proxy_CallStaticIntMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticIntMethodA", [&] { return realEnv->functions->CallStaticIntMethodA(realEnv, a1, a2, a3); }); }
static jlong proxy_CallStaticLongMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticLongMethod", [&] { return realEnv->functions->CallStaticLongMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jlong proxy_CallStaticLongMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticLongMethodV", [&] { return realEnv->functions->CallStaticLongMethodV(realEnv, a1, a2, a3); }); }
static jlong proxy_CallStaticLongMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticLongMethodA", [&] { return realEnv->functions->CallStaticLongMethodA(realEnv, a1, a2, a3); }); }
static jfloat proxy_CallStaticFloatMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticFloatMethod", [&] { return realEnv->functions->CallStaticFloatMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jfloat proxy_CallStaticFloatMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticFloatMethodV", [&] { return realEnv->functions->CallStaticFloatMethodV(realEnv, a1, a2, a3); }); }
static jfloat proxy_CallStaticFloatMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticFloatMethodA", [&] { return realEnv->functions->CallStaticFloatMethodA(realEnv, a1, a2, a3); }); }
static jdouble proxy_CallStaticDoubleMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);auto result=XdStack_Invoke("JNI CallStaticDoubleMethod", [&] { return realEnv->functions->CallStaticDoubleMethodV(realEnv, a1, a2, ap); }); va_end(ap); return result; }
static jdouble proxy_CallStaticDoubleMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticDoubleMethodV", [&] { return realEnv->functions->CallStaticDoubleMethodV(realEnv, a1, a2, a3); }); }
static jdouble proxy_CallStaticDoubleMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticDoubleMethodA", [&] { return realEnv->functions->CallStaticDoubleMethodA(realEnv, a1, a2, a3); }); }
static void proxy_CallStaticVoidMethod(JNIEnv * a0, jclass a1, jmethodID a2, ...) { va_list ap; va_start(ap,a2);XdStack_Invoke("JNI CallStaticVoidMethod", [&] { realEnv->functions->CallStaticVoidMethodV(realEnv, a1, a2, ap); }); va_end(ap); }
static void proxy_CallStaticVoidMethodV(JNIEnv * a0, jclass a1, jmethodID a2, va_list a3) { return XdStack_Invoke("JNI CallStaticVoidMethodV", [&] { return realEnv->functions->CallStaticVoidMethodV(realEnv, a1, a2, a3); }); }
static void proxy_CallStaticVoidMethodA(JNIEnv * a0, jclass a1, jmethodID a2, const jvalue * a3) { return XdStack_Invoke("JNI CallStaticVoidMethodA", [&] { return realEnv->functions->CallStaticVoidMethodA(realEnv, a1, a2, a3); }); }
static jfieldID proxy_GetStaticFieldID(JNIEnv * a0, jclass a1, const char * a2, const char * a3) { return XdStack_Invoke("JNI GetStaticFieldID", [&] { return realEnv->functions->GetStaticFieldID(realEnv, a1, a2, a3); }); }
static jobject proxy_GetStaticObjectField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticObjectField", [&] { return realEnv->functions->GetStaticObjectField(realEnv, a1, a2); }); }
static jboolean proxy_GetStaticBooleanField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticBooleanField", [&] { return realEnv->functions->GetStaticBooleanField(realEnv, a1, a2); }); }
static jbyte proxy_GetStaticByteField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticByteField", [&] { return realEnv->functions->GetStaticByteField(realEnv, a1, a2); }); }
static jchar proxy_GetStaticCharField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticCharField", [&] { return realEnv->functions->GetStaticCharField(realEnv, a1, a2); }); }
static jshort proxy_GetStaticShortField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticShortField", [&] { return realEnv->functions->GetStaticShortField(realEnv, a1, a2); }); }
static jint proxy_GetStaticIntField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticIntField", [&] { return realEnv->functions->GetStaticIntField(realEnv, a1, a2); }); }
static jlong proxy_GetStaticLongField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticLongField", [&] { return realEnv->functions->GetStaticLongField(realEnv, a1, a2); }); }
static jfloat proxy_GetStaticFloatField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticFloatField", [&] { return realEnv->functions->GetStaticFloatField(realEnv, a1, a2); }); }
static jdouble proxy_GetStaticDoubleField(JNIEnv * a0, jclass a1, jfieldID a2) { return XdStack_Invoke("JNI GetStaticDoubleField", [&] { return realEnv->functions->GetStaticDoubleField(realEnv, a1, a2); }); }
static void proxy_SetStaticObjectField(JNIEnv * a0, jclass a1, jfieldID a2, jobject a3) { return XdStack_Invoke("JNI SetStaticObjectField", [&] { return realEnv->functions->SetStaticObjectField(realEnv, a1, a2, a3); }); }
static void proxy_SetStaticBooleanField(JNIEnv * a0, jclass a1, jfieldID a2, jboolean a3) { return XdStack_Invoke("JNI SetStaticBooleanField", [&] { return realEnv->functions->SetStaticBooleanField(realEnv, a1, a2, a3); }); }
static void proxy_SetStaticByteField(JNIEnv * a0, jclass a1, jfieldID a2, jbyte a3) { return XdStack_Invoke("JNI SetStaticByteField", [&] { return realEnv->functions->SetStaticByteField(realEnv, a1, a2, a3); }); }
static void proxy_SetStaticCharField(JNIEnv * a0, jclass a1, jfieldID a2, jchar a3) { return XdStack_Invoke("JNI SetStaticCharField", [&] { return realEnv->functions->SetStaticCharField(realEnv, a1, a2, a3); }); }
static void proxy_SetStaticShortField(JNIEnv * a0, jclass a1, jfieldID a2, jshort a3) { return XdStack_Invoke("JNI SetStaticShortField", [&] { return realEnv->functions->SetStaticShortField(realEnv, a1, a2, a3); }); }
static void proxy_SetStaticIntField(JNIEnv * a0, jclass a1, jfieldID a2, jint a3) { return XdStack_Invoke("JNI SetStaticIntField", [&] { return realEnv->functions->SetStaticIntField(realEnv, a1, a2, a3); }); }
static void proxy_SetStaticLongField(JNIEnv * a0, jclass a1, jfieldID a2, jlong a3) { return XdStack_Invoke("JNI SetStaticLongField", [&] { return realEnv->functions->SetStaticLongField(realEnv, a1, a2, a3); }); }
static void proxy_SetStaticFloatField(JNIEnv * a0, jclass a1, jfieldID a2, jfloat a3) { return XdStack_Invoke("JNI SetStaticFloatField", [&] { return realEnv->functions->SetStaticFloatField(realEnv, a1, a2, a3); }); }
static void proxy_SetStaticDoubleField(JNIEnv * a0, jclass a1, jfieldID a2, jdouble a3) { return XdStack_Invoke("JNI SetStaticDoubleField", [&] { return realEnv->functions->SetStaticDoubleField(realEnv, a1, a2, a3); }); }
static jstring proxy_NewString(JNIEnv * a0, const jchar * a1, jsize a2) { return XdStack_Invoke("JNI NewString", [&] { return realEnv->functions->NewString(realEnv, a1, a2); }); }
static jsize proxy_GetStringLength(JNIEnv * a0, jstring a1) { return XdStack_Invoke("JNI GetStringLength", [&] { return realEnv->functions->GetStringLength(realEnv, a1); }); }
static const jchar * proxy_GetStringChars(JNIEnv * a0, jstring a1, jboolean * a2) { return XdStack_Invoke("JNI GetStringChars", [&] { return realEnv->functions->GetStringChars(realEnv, a1, a2); }); }
static void proxy_ReleaseStringChars(JNIEnv * a0, jstring a1, const jchar * a2) { return XdStack_Invoke("JNI ReleaseStringChars", [&] { return realEnv->functions->ReleaseStringChars(realEnv, a1, a2); }); }
static jstring proxy_NewStringUTF(JNIEnv * a0, const char * a1) { return XdStack_Invoke("JNI NewStringUTF", [&] { return realEnv->functions->NewStringUTF(realEnv, a1); }); }
static jsize proxy_GetStringUTFLength(JNIEnv * a0, jstring a1) { return XdStack_Invoke("JNI GetStringUTFLength", [&] { return realEnv->functions->GetStringUTFLength(realEnv, a1); }); }
static const char * proxy_GetStringUTFChars(JNIEnv * a0, jstring a1, jboolean * a2) { return XdStack_Invoke("JNI GetStringUTFChars", [&] { return realEnv->functions->GetStringUTFChars(realEnv, a1, a2); }); }
static void proxy_ReleaseStringUTFChars(JNIEnv * a0, jstring a1, const char * a2) { return XdStack_Invoke("JNI ReleaseStringUTFChars", [&] { return realEnv->functions->ReleaseStringUTFChars(realEnv, a1, a2); }); }
static jsize proxy_GetArrayLength(JNIEnv * a0, jarray a1) { return XdStack_Invoke("JNI GetArrayLength", [&] { return realEnv->functions->GetArrayLength(realEnv, a1); }); }
static jobjectArray proxy_NewObjectArray(JNIEnv * a0, jsize a1, jclass a2, jobject a3) { return XdStack_Invoke("JNI NewObjectArray", [&] { return realEnv->functions->NewObjectArray(realEnv, a1, a2, a3); }); }
static jobject proxy_GetObjectArrayElement(JNIEnv * a0, jobjectArray a1, jsize a2) { return XdStack_Invoke("JNI GetObjectArrayElement", [&] { return realEnv->functions->GetObjectArrayElement(realEnv, a1, a2); }); }
static void proxy_SetObjectArrayElement(JNIEnv * a0, jobjectArray a1, jsize a2, jobject a3) { return XdStack_Invoke("JNI SetObjectArrayElement", [&] { return realEnv->functions->SetObjectArrayElement(realEnv, a1, a2, a3); }); }
static jbooleanArray proxy_NewBooleanArray(JNIEnv * a0, jsize a1) { return XdStack_Invoke("JNI NewBooleanArray", [&] { return realEnv->functions->NewBooleanArray(realEnv, a1); }); }
static jbyteArray proxy_NewByteArray(JNIEnv * a0, jsize a1) { return XdStack_Invoke("JNI NewByteArray", [&] { return realEnv->functions->NewByteArray(realEnv, a1); }); }
static jcharArray proxy_NewCharArray(JNIEnv * a0, jsize a1) { return XdStack_Invoke("JNI NewCharArray", [&] { return realEnv->functions->NewCharArray(realEnv, a1); }); }
static jshortArray proxy_NewShortArray(JNIEnv * a0, jsize a1) { return XdStack_Invoke("JNI NewShortArray", [&] { return realEnv->functions->NewShortArray(realEnv, a1); }); }
static jintArray proxy_NewIntArray(JNIEnv * a0, jsize a1) { return XdStack_Invoke("JNI NewIntArray", [&] { return realEnv->functions->NewIntArray(realEnv, a1); }); }
static jlongArray proxy_NewLongArray(JNIEnv * a0, jsize a1) { return XdStack_Invoke("JNI NewLongArray", [&] { return realEnv->functions->NewLongArray(realEnv, a1); }); }
static jfloatArray proxy_NewFloatArray(JNIEnv * a0, jsize a1) { return XdStack_Invoke("JNI NewFloatArray", [&] { return realEnv->functions->NewFloatArray(realEnv, a1); }); }
static jdoubleArray proxy_NewDoubleArray(JNIEnv * a0, jsize a1) { return XdStack_Invoke("JNI NewDoubleArray", [&] { return realEnv->functions->NewDoubleArray(realEnv, a1); }); }
static jboolean * proxy_GetBooleanArrayElements(JNIEnv * a0, jbooleanArray a1, jboolean * a2) { return XdStack_Invoke("JNI GetBooleanArrayElements", [&] { return realEnv->functions->GetBooleanArrayElements(realEnv, a1, a2); }); }
static jbyte * proxy_GetByteArrayElements(JNIEnv * a0, jbyteArray a1, jboolean * a2) { return XdStack_Invoke("JNI GetByteArrayElements", [&] { return realEnv->functions->GetByteArrayElements(realEnv, a1, a2); }); }
static jchar * proxy_GetCharArrayElements(JNIEnv * a0, jcharArray a1, jboolean * a2) { return XdStack_Invoke("JNI GetCharArrayElements", [&] { return realEnv->functions->GetCharArrayElements(realEnv, a1, a2); }); }
static jshort * proxy_GetShortArrayElements(JNIEnv * a0, jshortArray a1, jboolean * a2) { return XdStack_Invoke("JNI GetShortArrayElements", [&] { return realEnv->functions->GetShortArrayElements(realEnv, a1, a2); }); }
static jint * proxy_GetIntArrayElements(JNIEnv * a0, jintArray a1, jboolean * a2) { return XdStack_Invoke("JNI GetIntArrayElements", [&] { return realEnv->functions->GetIntArrayElements(realEnv, a1, a2); }); }
static jlong * proxy_GetLongArrayElements(JNIEnv * a0, jlongArray a1, jboolean * a2) { return XdStack_Invoke("JNI GetLongArrayElements", [&] { return realEnv->functions->GetLongArrayElements(realEnv, a1, a2); }); }
static jfloat * proxy_GetFloatArrayElements(JNIEnv * a0, jfloatArray a1, jboolean * a2) { return XdStack_Invoke("JNI GetFloatArrayElements", [&] { return realEnv->functions->GetFloatArrayElements(realEnv, a1, a2); }); }
static jdouble * proxy_GetDoubleArrayElements(JNIEnv * a0, jdoubleArray a1, jboolean * a2) { return XdStack_Invoke("JNI GetDoubleArrayElements", [&] { return realEnv->functions->GetDoubleArrayElements(realEnv, a1, a2); }); }
static void proxy_ReleaseBooleanArrayElements(JNIEnv * a0, jbooleanArray a1, jboolean * a2, jint a3) { return XdStack_Invoke("JNI ReleaseBooleanArrayElements", [&] { return realEnv->functions->ReleaseBooleanArrayElements(realEnv, a1, a2, a3); }); }
static void proxy_ReleaseByteArrayElements(JNIEnv * a0, jbyteArray a1, jbyte * a2, jint a3) { return XdStack_Invoke("JNI ReleaseByteArrayElements", [&] { return realEnv->functions->ReleaseByteArrayElements(realEnv, a1, a2, a3); }); }
static void proxy_ReleaseCharArrayElements(JNIEnv * a0, jcharArray a1, jchar * a2, jint a3) { return XdStack_Invoke("JNI ReleaseCharArrayElements", [&] { return realEnv->functions->ReleaseCharArrayElements(realEnv, a1, a2, a3); }); }
static void proxy_ReleaseShortArrayElements(JNIEnv * a0, jshortArray a1, jshort * a2, jint a3) { return XdStack_Invoke("JNI ReleaseShortArrayElements", [&] { return realEnv->functions->ReleaseShortArrayElements(realEnv, a1, a2, a3); }); }
static void proxy_ReleaseIntArrayElements(JNIEnv * a0, jintArray a1, jint * a2, jint a3) { return XdStack_Invoke("JNI ReleaseIntArrayElements", [&] { return realEnv->functions->ReleaseIntArrayElements(realEnv, a1, a2, a3); }); }
static void proxy_ReleaseLongArrayElements(JNIEnv * a0, jlongArray a1, jlong * a2, jint a3) { return XdStack_Invoke("JNI ReleaseLongArrayElements", [&] { return realEnv->functions->ReleaseLongArrayElements(realEnv, a1, a2, a3); }); }
static void proxy_ReleaseFloatArrayElements(JNIEnv * a0, jfloatArray a1, jfloat * a2, jint a3) { return XdStack_Invoke("JNI ReleaseFloatArrayElements", [&] { return realEnv->functions->ReleaseFloatArrayElements(realEnv, a1, a2, a3); }); }
static void proxy_ReleaseDoubleArrayElements(JNIEnv * a0, jdoubleArray a1, jdouble * a2, jint a3) { return XdStack_Invoke("JNI ReleaseDoubleArrayElements", [&] { return realEnv->functions->ReleaseDoubleArrayElements(realEnv, a1, a2, a3); }); }
static void proxy_GetBooleanArrayRegion(JNIEnv * a0, jbooleanArray a1, jsize a2, jsize a3, jboolean * a4) { return XdStack_Invoke("JNI GetBooleanArrayRegion", [&] { return realEnv->functions->GetBooleanArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_GetByteArrayRegion(JNIEnv * a0, jbyteArray a1, jsize a2, jsize a3, jbyte * a4) { return XdStack_Invoke("JNI GetByteArrayRegion", [&] { return realEnv->functions->GetByteArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_GetCharArrayRegion(JNIEnv * a0, jcharArray a1, jsize a2, jsize a3, jchar * a4) { return XdStack_Invoke("JNI GetCharArrayRegion", [&] { return realEnv->functions->GetCharArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_GetShortArrayRegion(JNIEnv * a0, jshortArray a1, jsize a2, jsize a3, jshort * a4) { return XdStack_Invoke("JNI GetShortArrayRegion", [&] { return realEnv->functions->GetShortArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_GetIntArrayRegion(JNIEnv * a0, jintArray a1, jsize a2, jsize a3, jint * a4) { return XdStack_Invoke("JNI GetIntArrayRegion", [&] { return realEnv->functions->GetIntArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_GetLongArrayRegion(JNIEnv * a0, jlongArray a1, jsize a2, jsize a3, jlong * a4) { return XdStack_Invoke("JNI GetLongArrayRegion", [&] { return realEnv->functions->GetLongArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_GetFloatArrayRegion(JNIEnv * a0, jfloatArray a1, jsize a2, jsize a3, jfloat * a4) { return XdStack_Invoke("JNI GetFloatArrayRegion", [&] { return realEnv->functions->GetFloatArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_GetDoubleArrayRegion(JNIEnv * a0, jdoubleArray a1, jsize a2, jsize a3, jdouble * a4) { return XdStack_Invoke("JNI GetDoubleArrayRegion", [&] { return realEnv->functions->GetDoubleArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_SetBooleanArrayRegion(JNIEnv * a0, jbooleanArray a1, jsize a2, jsize a3, const jboolean * a4) { return XdStack_Invoke("JNI SetBooleanArrayRegion", [&] { return realEnv->functions->SetBooleanArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_SetByteArrayRegion(JNIEnv * a0, jbyteArray a1, jsize a2, jsize a3, const jbyte * a4) { return XdStack_Invoke("JNI SetByteArrayRegion", [&] { return realEnv->functions->SetByteArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_SetCharArrayRegion(JNIEnv * a0, jcharArray a1, jsize a2, jsize a3, const jchar * a4) { return XdStack_Invoke("JNI SetCharArrayRegion", [&] { return realEnv->functions->SetCharArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_SetShortArrayRegion(JNIEnv * a0, jshortArray a1, jsize a2, jsize a3, const jshort * a4) { return XdStack_Invoke("JNI SetShortArrayRegion", [&] { return realEnv->functions->SetShortArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_SetIntArrayRegion(JNIEnv * a0, jintArray a1, jsize a2, jsize a3, const jint * a4) { return XdStack_Invoke("JNI SetIntArrayRegion", [&] { return realEnv->functions->SetIntArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_SetLongArrayRegion(JNIEnv * a0, jlongArray a1, jsize a2, jsize a3, const jlong * a4) { return XdStack_Invoke("JNI SetLongArrayRegion", [&] { return realEnv->functions->SetLongArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_SetFloatArrayRegion(JNIEnv * a0, jfloatArray a1, jsize a2, jsize a3, const jfloat * a4) { return XdStack_Invoke("JNI SetFloatArrayRegion", [&] { return realEnv->functions->SetFloatArrayRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_SetDoubleArrayRegion(JNIEnv * a0, jdoubleArray a1, jsize a2, jsize a3, const jdouble * a4) { return XdStack_Invoke("JNI SetDoubleArrayRegion", [&] { return realEnv->functions->SetDoubleArrayRegion(realEnv, a1, a2, a3, a4); }); }
static jint proxy_RegisterNatives(JNIEnv * a0, jclass a1, const JNINativeMethod * a2, jint a3) { return XdStack_Invoke("JNI RegisterNatives", [&] { return realEnv->functions->RegisterNatives(realEnv, a1, a2, a3); }); }
static jint proxy_UnregisterNatives(JNIEnv * a0, jclass a1) { return XdStack_Invoke("JNI UnregisterNatives", [&] { return realEnv->functions->UnregisterNatives(realEnv, a1); }); }
static jint proxy_MonitorEnter(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI MonitorEnter", [&] { return realEnv->functions->MonitorEnter(realEnv, a1); }); }
static jint proxy_MonitorExit(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI MonitorExit", [&] { return realEnv->functions->MonitorExit(realEnv, a1); }); }
static jint proxy_GetJavaVM(JNIEnv * a0, JavaVM ** a1) { return XdStack_Invoke("JNI GetJavaVM", [&] { return realEnv->functions->GetJavaVM(realEnv, a1); }); }
static void proxy_GetStringRegion(JNIEnv * a0, jstring a1, jsize a2, jsize a3, jchar * a4) { return XdStack_Invoke("JNI GetStringRegion", [&] { return realEnv->functions->GetStringRegion(realEnv, a1, a2, a3, a4); }); }
static void proxy_GetStringUTFRegion(JNIEnv * a0, jstring a1, jsize a2, jsize a3, char * a4) { return XdStack_Invoke("JNI GetStringUTFRegion", [&] { return realEnv->functions->GetStringUTFRegion(realEnv, a1, a2, a3, a4); }); }
static void * proxy_GetPrimitiveArrayCritical(JNIEnv * a0, jarray a1, jboolean * a2) { return XdStack_Invoke("JNI GetPrimitiveArrayCritical", [&] { return realEnv->functions->GetPrimitiveArrayCritical(realEnv, a1, a2); }); }
static void proxy_ReleasePrimitiveArrayCritical(JNIEnv * a0, jarray a1, void * a2, jint a3) { return XdStack_Invoke("JNI ReleasePrimitiveArrayCritical", [&] { return realEnv->functions->ReleasePrimitiveArrayCritical(realEnv, a1, a2, a3); }); }
static const jchar * proxy_GetStringCritical(JNIEnv * a0, jstring a1, jboolean * a2) { return XdStack_Invoke("JNI GetStringCritical", [&] { return realEnv->functions->GetStringCritical(realEnv, a1, a2); }); }
static void proxy_ReleaseStringCritical(JNIEnv * a0, jstring a1, const jchar * a2) { return XdStack_Invoke("JNI ReleaseStringCritical", [&] { return realEnv->functions->ReleaseStringCritical(realEnv, a1, a2); }); }
static jweak proxy_NewWeakGlobalRef(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI NewWeakGlobalRef", [&] { return realEnv->functions->NewWeakGlobalRef(realEnv, a1); }); }
static void proxy_DeleteWeakGlobalRef(JNIEnv * a0, jweak a1) { return XdStack_Invoke("JNI DeleteWeakGlobalRef", [&] { return realEnv->functions->DeleteWeakGlobalRef(realEnv, a1); }); }
static jboolean proxy_ExceptionCheck(JNIEnv * a0) { return XdStack_Invoke("JNI ExceptionCheck", [&] { return realEnv->functions->ExceptionCheck(realEnv); }); }
static jobject proxy_NewDirectByteBuffer(JNIEnv * a0, void * a1, jlong a2) { return XdStack_Invoke("JNI NewDirectByteBuffer", [&] { return realEnv->functions->NewDirectByteBuffer(realEnv, a1, a2); }); }
static void * proxy_GetDirectBufferAddress(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI GetDirectBufferAddress", [&] { return realEnv->functions->GetDirectBufferAddress(realEnv, a1); }); }
static jlong proxy_GetDirectBufferCapacity(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI GetDirectBufferCapacity", [&] { return realEnv->functions->GetDirectBufferCapacity(realEnv, a1); }); }
static jobjectRefType proxy_GetObjectRefType(JNIEnv * a0, jobject a1) { return XdStack_Invoke("JNI GetObjectRefType", [&] { return realEnv->functions->GetObjectRefType(realEnv, a1); }); }
void* XdStack_JniEnv() { auto* env=static_cast<JNIEnv*>(XdStack_Invoke("SDL_GetAndroidJNIEnv", [] { return __real_SDL_GetAndroidJNIEnv(); })); if (!env || !XdStack_IsForeign()) return env; if (!realEnv) { realEnv=env; proxyTable=*env->functions; proxyTable.GetVersion=&proxy_GetVersion;proxyTable.DefineClass=&proxy_DefineClass;proxyTable.FindClass=&proxy_FindClass;proxyTable.FromReflectedMethod=&proxy_FromReflectedMethod;proxyTable.FromReflectedField=&proxy_FromReflectedField;proxyTable.ToReflectedMethod=&proxy_ToReflectedMethod;proxyTable.GetSuperclass=&proxy_GetSuperclass;proxyTable.IsAssignableFrom=&proxy_IsAssignableFrom;proxyTable.ToReflectedField=&proxy_ToReflectedField;proxyTable.Throw=&proxy_Throw;proxyTable.ThrowNew=&proxy_ThrowNew;proxyTable.ExceptionOccurred=&proxy_ExceptionOccurred;proxyTable.ExceptionDescribe=&proxy_ExceptionDescribe;proxyTable.ExceptionClear=&proxy_ExceptionClear;proxyTable.FatalError=&proxy_FatalError;proxyTable.PushLocalFrame=&proxy_PushLocalFrame;proxyTable.PopLocalFrame=&proxy_PopLocalFrame;proxyTable.NewGlobalRef=&proxy_NewGlobalRef;proxyTable.DeleteGlobalRef=&proxy_DeleteGlobalRef;proxyTable.DeleteLocalRef=&proxy_DeleteLocalRef;proxyTable.IsSameObject=&proxy_IsSameObject;proxyTable.NewLocalRef=&proxy_NewLocalRef;proxyTable.EnsureLocalCapacity=&proxy_EnsureLocalCapacity;proxyTable.AllocObject=&proxy_AllocObject;proxyTable.NewObject=&proxy_NewObject;proxyTable.NewObjectV=&proxy_NewObjectV;proxyTable.NewObjectA=&proxy_NewObjectA;proxyTable.GetObjectClass=&proxy_GetObjectClass;proxyTable.IsInstanceOf=&proxy_IsInstanceOf;proxyTable.GetMethodID=&proxy_GetMethodID;proxyTable.CallObjectMethod=&proxy_CallObjectMethod;proxyTable.CallObjectMethodV=&proxy_CallObjectMethodV;proxyTable.CallObjectMethodA=&proxy_CallObjectMethodA;proxyTable.CallBooleanMethod=&proxy_CallBooleanMethod;proxyTable.CallBooleanMethodV=&proxy_CallBooleanMethodV;proxyTable.CallBooleanMethodA=&proxy_CallBooleanMethodA;proxyTable.CallByteMethod=&proxy_CallByteMethod;proxyTable.CallByteMethodV=&proxy_CallByteMethodV;proxyTable.CallByteMethodA=&proxy_CallByteMethodA;proxyTable.CallCharMethod=&proxy_CallCharMethod;proxyTable.CallCharMethodV=&proxy_CallCharMethodV;proxyTable.CallCharMethodA=&proxy_CallCharMethodA;proxyTable.CallShortMethod=&proxy_CallShortMethod;proxyTable.CallShortMethodV=&proxy_CallShortMethodV;proxyTable.CallShortMethodA=&proxy_CallShortMethodA;proxyTable.CallIntMethod=&proxy_CallIntMethod;proxyTable.CallIntMethodV=&proxy_CallIntMethodV;proxyTable.CallIntMethodA=&proxy_CallIntMethodA;proxyTable.CallLongMethod=&proxy_CallLongMethod;proxyTable.CallLongMethodV=&proxy_CallLongMethodV;proxyTable.CallLongMethodA=&proxy_CallLongMethodA;proxyTable.CallFloatMethod=&proxy_CallFloatMethod;proxyTable.CallFloatMethodV=&proxy_CallFloatMethodV;proxyTable.CallFloatMethodA=&proxy_CallFloatMethodA;proxyTable.CallDoubleMethod=&proxy_CallDoubleMethod;proxyTable.CallDoubleMethodV=&proxy_CallDoubleMethodV;proxyTable.CallDoubleMethodA=&proxy_CallDoubleMethodA;proxyTable.CallVoidMethod=&proxy_CallVoidMethod;proxyTable.CallVoidMethodV=&proxy_CallVoidMethodV;proxyTable.CallVoidMethodA=&proxy_CallVoidMethodA;proxyTable.CallNonvirtualObjectMethod=&proxy_CallNonvirtualObjectMethod;proxyTable.CallNonvirtualObjectMethodV=&proxy_CallNonvirtualObjectMethodV;proxyTable.CallNonvirtualObjectMethodA=&proxy_CallNonvirtualObjectMethodA;proxyTable.CallNonvirtualBooleanMethod=&proxy_CallNonvirtualBooleanMethod;proxyTable.CallNonvirtualBooleanMethodV=&proxy_CallNonvirtualBooleanMethodV;proxyTable.CallNonvirtualBooleanMethodA=&proxy_CallNonvirtualBooleanMethodA;proxyTable.CallNonvirtualByteMethod=&proxy_CallNonvirtualByteMethod;proxyTable.CallNonvirtualByteMethodV=&proxy_CallNonvirtualByteMethodV;proxyTable.CallNonvirtualByteMethodA=&proxy_CallNonvirtualByteMethodA;proxyTable.CallNonvirtualCharMethod=&proxy_CallNonvirtualCharMethod;proxyTable.CallNonvirtualCharMethodV=&proxy_CallNonvirtualCharMethodV;proxyTable.CallNonvirtualCharMethodA=&proxy_CallNonvirtualCharMethodA;proxyTable.CallNonvirtualShortMethod=&proxy_CallNonvirtualShortMethod;proxyTable.CallNonvirtualShortMethodV=&proxy_CallNonvirtualShortMethodV;proxyTable.CallNonvirtualShortMethodA=&proxy_CallNonvirtualShortMethodA;proxyTable.CallNonvirtualIntMethod=&proxy_CallNonvirtualIntMethod;proxyTable.CallNonvirtualIntMethodV=&proxy_CallNonvirtualIntMethodV;proxyTable.CallNonvirtualIntMethodA=&proxy_CallNonvirtualIntMethodA;proxyTable.CallNonvirtualLongMethod=&proxy_CallNonvirtualLongMethod;proxyTable.CallNonvirtualLongMethodV=&proxy_CallNonvirtualLongMethodV;proxyTable.CallNonvirtualLongMethodA=&proxy_CallNonvirtualLongMethodA;proxyTable.CallNonvirtualFloatMethod=&proxy_CallNonvirtualFloatMethod;proxyTable.CallNonvirtualFloatMethodV=&proxy_CallNonvirtualFloatMethodV;proxyTable.CallNonvirtualFloatMethodA=&proxy_CallNonvirtualFloatMethodA;proxyTable.CallNonvirtualDoubleMethod=&proxy_CallNonvirtualDoubleMethod;proxyTable.CallNonvirtualDoubleMethodV=&proxy_CallNonvirtualDoubleMethodV;proxyTable.CallNonvirtualDoubleMethodA=&proxy_CallNonvirtualDoubleMethodA;proxyTable.CallNonvirtualVoidMethod=&proxy_CallNonvirtualVoidMethod;proxyTable.CallNonvirtualVoidMethodV=&proxy_CallNonvirtualVoidMethodV;proxyTable.CallNonvirtualVoidMethodA=&proxy_CallNonvirtualVoidMethodA;proxyTable.GetFieldID=&proxy_GetFieldID;proxyTable.GetObjectField=&proxy_GetObjectField;proxyTable.GetBooleanField=&proxy_GetBooleanField;proxyTable.GetByteField=&proxy_GetByteField;proxyTable.GetCharField=&proxy_GetCharField;proxyTable.GetShortField=&proxy_GetShortField;proxyTable.GetIntField=&proxy_GetIntField;proxyTable.GetLongField=&proxy_GetLongField;proxyTable.GetFloatField=&proxy_GetFloatField;proxyTable.GetDoubleField=&proxy_GetDoubleField;proxyTable.SetObjectField=&proxy_SetObjectField;proxyTable.SetBooleanField=&proxy_SetBooleanField;proxyTable.SetByteField=&proxy_SetByteField;proxyTable.SetCharField=&proxy_SetCharField;proxyTable.SetShortField=&proxy_SetShortField;proxyTable.SetIntField=&proxy_SetIntField;proxyTable.SetLongField=&proxy_SetLongField;proxyTable.SetFloatField=&proxy_SetFloatField;proxyTable.SetDoubleField=&proxy_SetDoubleField;proxyTable.GetStaticMethodID=&proxy_GetStaticMethodID;proxyTable.CallStaticObjectMethod=&proxy_CallStaticObjectMethod;proxyTable.CallStaticObjectMethodV=&proxy_CallStaticObjectMethodV;proxyTable.CallStaticObjectMethodA=&proxy_CallStaticObjectMethodA;proxyTable.CallStaticBooleanMethod=&proxy_CallStaticBooleanMethod;proxyTable.CallStaticBooleanMethodV=&proxy_CallStaticBooleanMethodV;proxyTable.CallStaticBooleanMethodA=&proxy_CallStaticBooleanMethodA;proxyTable.CallStaticByteMethod=&proxy_CallStaticByteMethod;proxyTable.CallStaticByteMethodV=&proxy_CallStaticByteMethodV;proxyTable.CallStaticByteMethodA=&proxy_CallStaticByteMethodA;proxyTable.CallStaticCharMethod=&proxy_CallStaticCharMethod;proxyTable.CallStaticCharMethodV=&proxy_CallStaticCharMethodV;proxyTable.CallStaticCharMethodA=&proxy_CallStaticCharMethodA;proxyTable.CallStaticShortMethod=&proxy_CallStaticShortMethod;proxyTable.CallStaticShortMethodV=&proxy_CallStaticShortMethodV;proxyTable.CallStaticShortMethodA=&proxy_CallStaticShortMethodA;proxyTable.CallStaticIntMethod=&proxy_CallStaticIntMethod;proxyTable.CallStaticIntMethodV=&proxy_CallStaticIntMethodV;proxyTable.CallStaticIntMethodA=&proxy_CallStaticIntMethodA;proxyTable.CallStaticLongMethod=&proxy_CallStaticLongMethod;proxyTable.CallStaticLongMethodV=&proxy_CallStaticLongMethodV;proxyTable.CallStaticLongMethodA=&proxy_CallStaticLongMethodA;proxyTable.CallStaticFloatMethod=&proxy_CallStaticFloatMethod;proxyTable.CallStaticFloatMethodV=&proxy_CallStaticFloatMethodV;proxyTable.CallStaticFloatMethodA=&proxy_CallStaticFloatMethodA;proxyTable.CallStaticDoubleMethod=&proxy_CallStaticDoubleMethod;proxyTable.CallStaticDoubleMethodV=&proxy_CallStaticDoubleMethodV;proxyTable.CallStaticDoubleMethodA=&proxy_CallStaticDoubleMethodA;proxyTable.CallStaticVoidMethod=&proxy_CallStaticVoidMethod;proxyTable.CallStaticVoidMethodV=&proxy_CallStaticVoidMethodV;proxyTable.CallStaticVoidMethodA=&proxy_CallStaticVoidMethodA;proxyTable.GetStaticFieldID=&proxy_GetStaticFieldID;proxyTable.GetStaticObjectField=&proxy_GetStaticObjectField;proxyTable.GetStaticBooleanField=&proxy_GetStaticBooleanField;proxyTable.GetStaticByteField=&proxy_GetStaticByteField;proxyTable.GetStaticCharField=&proxy_GetStaticCharField;proxyTable.GetStaticShortField=&proxy_GetStaticShortField;proxyTable.GetStaticIntField=&proxy_GetStaticIntField;proxyTable.GetStaticLongField=&proxy_GetStaticLongField;proxyTable.GetStaticFloatField=&proxy_GetStaticFloatField;proxyTable.GetStaticDoubleField=&proxy_GetStaticDoubleField;proxyTable.SetStaticObjectField=&proxy_SetStaticObjectField;proxyTable.SetStaticBooleanField=&proxy_SetStaticBooleanField;proxyTable.SetStaticByteField=&proxy_SetStaticByteField;proxyTable.SetStaticCharField=&proxy_SetStaticCharField;proxyTable.SetStaticShortField=&proxy_SetStaticShortField;proxyTable.SetStaticIntField=&proxy_SetStaticIntField;proxyTable.SetStaticLongField=&proxy_SetStaticLongField;proxyTable.SetStaticFloatField=&proxy_SetStaticFloatField;proxyTable.SetStaticDoubleField=&proxy_SetStaticDoubleField;proxyTable.NewString=&proxy_NewString;proxyTable.GetStringLength=&proxy_GetStringLength;proxyTable.GetStringChars=&proxy_GetStringChars;proxyTable.ReleaseStringChars=&proxy_ReleaseStringChars;proxyTable.NewStringUTF=&proxy_NewStringUTF;proxyTable.GetStringUTFLength=&proxy_GetStringUTFLength;proxyTable.GetStringUTFChars=&proxy_GetStringUTFChars;proxyTable.ReleaseStringUTFChars=&proxy_ReleaseStringUTFChars;proxyTable.GetArrayLength=&proxy_GetArrayLength;proxyTable.NewObjectArray=&proxy_NewObjectArray;proxyTable.GetObjectArrayElement=&proxy_GetObjectArrayElement;proxyTable.SetObjectArrayElement=&proxy_SetObjectArrayElement;proxyTable.NewBooleanArray=&proxy_NewBooleanArray;proxyTable.NewByteArray=&proxy_NewByteArray;proxyTable.NewCharArray=&proxy_NewCharArray;proxyTable.NewShortArray=&proxy_NewShortArray;proxyTable.NewIntArray=&proxy_NewIntArray;proxyTable.NewLongArray=&proxy_NewLongArray;proxyTable.NewFloatArray=&proxy_NewFloatArray;proxyTable.NewDoubleArray=&proxy_NewDoubleArray;proxyTable.GetBooleanArrayElements=&proxy_GetBooleanArrayElements;proxyTable.GetByteArrayElements=&proxy_GetByteArrayElements;proxyTable.GetCharArrayElements=&proxy_GetCharArrayElements;proxyTable.GetShortArrayElements=&proxy_GetShortArrayElements;proxyTable.GetIntArrayElements=&proxy_GetIntArrayElements;proxyTable.GetLongArrayElements=&proxy_GetLongArrayElements;proxyTable.GetFloatArrayElements=&proxy_GetFloatArrayElements;proxyTable.GetDoubleArrayElements=&proxy_GetDoubleArrayElements;proxyTable.ReleaseBooleanArrayElements=&proxy_ReleaseBooleanArrayElements;proxyTable.ReleaseByteArrayElements=&proxy_ReleaseByteArrayElements;proxyTable.ReleaseCharArrayElements=&proxy_ReleaseCharArrayElements;proxyTable.ReleaseShortArrayElements=&proxy_ReleaseShortArrayElements;proxyTable.ReleaseIntArrayElements=&proxy_ReleaseIntArrayElements;proxyTable.ReleaseLongArrayElements=&proxy_ReleaseLongArrayElements;proxyTable.ReleaseFloatArrayElements=&proxy_ReleaseFloatArrayElements;proxyTable.ReleaseDoubleArrayElements=&proxy_ReleaseDoubleArrayElements;proxyTable.GetBooleanArrayRegion=&proxy_GetBooleanArrayRegion;proxyTable.GetByteArrayRegion=&proxy_GetByteArrayRegion;proxyTable.GetCharArrayRegion=&proxy_GetCharArrayRegion;proxyTable.GetShortArrayRegion=&proxy_GetShortArrayRegion;proxyTable.GetIntArrayRegion=&proxy_GetIntArrayRegion;proxyTable.GetLongArrayRegion=&proxy_GetLongArrayRegion;proxyTable.GetFloatArrayRegion=&proxy_GetFloatArrayRegion;proxyTable.GetDoubleArrayRegion=&proxy_GetDoubleArrayRegion;proxyTable.SetBooleanArrayRegion=&proxy_SetBooleanArrayRegion;proxyTable.SetByteArrayRegion=&proxy_SetByteArrayRegion;proxyTable.SetCharArrayRegion=&proxy_SetCharArrayRegion;proxyTable.SetShortArrayRegion=&proxy_SetShortArrayRegion;proxyTable.SetIntArrayRegion=&proxy_SetIntArrayRegion;proxyTable.SetLongArrayRegion=&proxy_SetLongArrayRegion;proxyTable.SetFloatArrayRegion=&proxy_SetFloatArrayRegion;proxyTable.SetDoubleArrayRegion=&proxy_SetDoubleArrayRegion;proxyTable.RegisterNatives=&proxy_RegisterNatives;proxyTable.UnregisterNatives=&proxy_UnregisterNatives;proxyTable.MonitorEnter=&proxy_MonitorEnter;proxyTable.MonitorExit=&proxy_MonitorExit;proxyTable.GetJavaVM=&proxy_GetJavaVM;proxyTable.GetStringRegion=&proxy_GetStringRegion;proxyTable.GetStringUTFRegion=&proxy_GetStringUTFRegion;proxyTable.GetPrimitiveArrayCritical=&proxy_GetPrimitiveArrayCritical;proxyTable.ReleasePrimitiveArrayCritical=&proxy_ReleasePrimitiveArrayCritical;proxyTable.GetStringCritical=&proxy_GetStringCritical;proxyTable.ReleaseStringCritical=&proxy_ReleaseStringCritical;proxyTable.NewWeakGlobalRef=&proxy_NewWeakGlobalRef;proxyTable.DeleteWeakGlobalRef=&proxy_DeleteWeakGlobalRef;proxyTable.ExceptionCheck=&proxy_ExceptionCheck;proxyTable.NewDirectByteBuffer=&proxy_NewDirectByteBuffer;proxyTable.GetDirectBufferAddress=&proxy_GetDirectBufferAddress;proxyTable.GetDirectBufferCapacity=&proxy_GetDirectBufferCapacity;proxyTable.GetObjectRefType=&proxy_GetObjectRefType; proxyEnv.functions=&proxyTable; } return &proxyEnv; }
extern "C" SDL_Surface* __real_SDL_ConvertSurface(SDL_Surface* surface, SDL_PixelFormat format);
extern "C" SDL_Surface* __wrap_SDL_ConvertSurface(SDL_Surface* surface, SDL_PixelFormat format) {
    return XdStack_Invoke("SDL_ConvertSurface", [&] { return __real_SDL_ConvertSurface(surface, format); });
}

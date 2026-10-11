#pragma once
#include <d3d9.h>
#include <cstdint>
#include <array>
#include "game_access.hpp"

namespace re4craft {
using WorldDrawFn = bool (*)(IDirect3DDevice9*, const NativeSnapshot&);
// Install after MinHook initialization using the same validated native device
// method snapshot as EndScene. Setup never dereferences a borrowed COM object.
// The queued GX projection consumer executes on the actual GPU render thread.
// A matching main perspective with native draw calls is flushed before UI,
// depth clearing/unbinding, render-target replacement or the owner's final world scene.
bool installWorldRenderHook(GameAccess* game, const std::array<void*,85>& methods, WorldDrawFn draw, LogFn log);
void removeWorldRenderHook();
uint64_t worldRenderAttempts();
uint64_t worldRenderSuccesses();
// Guard EndScene update/HUD work before acquiring the root render mutex.
// A HUD target/state change must never recursively invoke a world callback.
bool setWorldRenderSuppressed(bool value);
// Called at the beginning of the owner's EndScene hook, before its overlay
// suppression or render lock. Drawing still requires genuine bound native depth.
void flushWorldRenderBeforeOverlay(IDirect3DDevice9* device);
// False bypasses camera capture/lookup. Enable during the bounded camera
// bootstrap as well as active drawing, including empty first-person scenes.
void setWorldRenderCaptureEnabled(bool enabled);
struct WorldRenderSuppression {
    bool previous;
    WorldRenderSuppression():previous(setWorldRenderSuppressed(true)){}
    ~WorldRenderSuppression(){setWorldRenderSuppressed(previous);}
};
}

// Process-wide RmlUi and Lua setup shared by all skin views (all plugin instances in the host).
//
// RmlUi and the Lua plugin keep global state, so the first view initialises them and the last one shuts them
// down. All access goes through the global skin lock. Lua runs sandboxed: only the base (without dofile and
// loadfile; load accepts text only), coroutine, table, string, math and utf8 libraries; every entry into Lua may
// run for at most kLuaTimeoutSeconds; errors are logged, never thrown into the host.
#pragma once
#include <mutex>
#include <string>

struct lua_State;
namespace Rml { class Context; }

namespace skin {

class SkinView;

std::recursive_mutex& skinLock();

// Reference-counted initialisation of RmlUi, the custom elements and the Lua state. Skin lock held.
bool acquireSystem(std::string& error);
void releaseSystem();
lua_State* luaState();

// Files may only be opened from these folders (the loaded skins' folders), by plain file name.
void allowFolder(const std::string& folder);
// Fonts are shared by all views; each font file is loaded once.
void loadFont(const std::string& path);

// Debugger: one RmlUi debugger for the process, shown in one context at a time.
void showDebugger(Rml::Context* context, bool visible);
void contextClosing(Rml::Context* context);

// Makes a view current for the scope: takes the skin lock, routes log messages to the view, switches Lua to the
// view's global environment and starts the Lua time budget (outermost scope only).
class ViewScope {
public:
    explicit ViewScope(SkinView* view);
    ~ViewScope();
    ViewScope(const ViewScope&) = delete;
    ViewScope& operator=(const ViewScope&) = delete;
private:
    std::lock_guard<std::recursive_mutex> lock_;
    SkinView* previous_;
    int previousEnv_;
};

// The view's Lua environment: a fresh table whose missing names come from the shared globals. Recreated on reload.
void resetEnvironment(SkinView& view);
void dropEnvironment(SkinView& view);

}  // namespace skin

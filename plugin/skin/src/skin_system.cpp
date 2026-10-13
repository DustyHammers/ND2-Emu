#include "skin_system.h"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi/Lua.h>
#include <RmlUi/Lua/IncludeLua.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>
#include "skin_elements.h"
#include "skin_view.h"

namespace fs = std::filesystem;

namespace skin {
namespace {

using Clock = std::chrono::steady_clock;

int g_users = 0;
lua_State* g_L = nullptr;
int g_baseEnv = LUA_NOREF;          // the shared globals (libraries and the RmlUi bindings)
int g_activeEnv = LUA_NOREF;        // the globals table Lua currently uses
SkinView* g_current = nullptr;
int g_scopeDepth = 0;
Clock::time_point g_deadline;
std::set<std::string> g_folders, g_fonts;
Rml::Context* g_debuggerContext = nullptr;
bool g_debuggerReady = false;

std::string folderKey(const fs::path& p) { return p.lexically_normal().generic_string(); }

class SystemInterface : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return std::chrono::duration<double>(Clock::now() - start_).count(); }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        const char* kind = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? "error"
                         : type == Rml::Log::LT_WARNING ? "warning" : "info";
        const std::string line = std::string(kind) + ": " + message;
        if (g_current) g_current->addLog(line);
        else std::fprintf(stderr, "skin %s\n", line.c_str());
        return true;                 // never stop on an assert
    }
private:
    Clock::time_point start_ = Clock::now();
};

// Opens files only from the allowed skin folders, by plain file name (no sub-folders, no other paths).
class FileInterface : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        const fs::path p = fs::u8path(path).lexically_normal();
        if (!g_folders.count(folderKey(p.parent_path()))) {
            Rml::Log::Message(Rml::Log::LT_WARNING, "File not opened (only plain file names inside the skin folder are allowed): %s", path.c_str());
            return 0;
        }
#ifdef _WIN32
        return Rml::FileHandle(_wfopen(p.c_str(), L"rb"));      // wide path: folder names in any language
#else
        return Rml::FileHandle(std::fopen(p.string().c_str(), "rb"));
#endif
    }
    void Close(Rml::FileHandle file) override { std::fclose(reinterpret_cast<FILE*>(file)); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override { return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file)); }
    bool Seek(Rml::FileHandle file, long offset, int origin) override { return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0; }
    size_t Tell(Rml::FileHandle file) override { return size_t(std::ftell(reinterpret_cast<FILE*>(file))); }
};

SystemInterface* g_system = nullptr;
FileInterface* g_files = nullptr;

// ---- Lua sandbox

void timeoutHook(lua_State* L, lua_Debug*) {
    if (g_scopeDepth > 0 && Clock::now() > g_deadline) luaL_error(L, "script stopped: it ran longer than %d s", int(kLuaTimeoutSeconds));
}

int panicHandler(lua_State* L) {
    const char* msg = lua_tostring(L, -1);
    throw std::runtime_error(std::string("Lua: ") + (msg ? msg : "unprotected error"));
}

int luaPrint(lua_State* L) {
    std::string line;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) line += "\t";
        line += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    Rml::Log::Message(Rml::Log::LT_INFO, "%s", line.c_str());
    return 0;
}

const char* kSandbox = R"lua(
dofile = nil
loadfile = nil
local load0 = load
load = function(chunk, name, mode, ...)
  if select('#', ...) > 0 then return load0(chunk, name, "t", ...) end
  return load0(chunk, name, "t")
end
)lua";

lua_State* makeLua() {
    lua_State* L = luaL_newstate();
    if (!L) return nullptr;
    lua_atpanic(L, panicHandler);
    luaL_openlibs(L);
    lua_pushcfunction(L, luaPrint);
    lua_setglobal(L, "print");
    if (luaL_dostring(L, kSandbox) != LUA_OK) { lua_close(L); return nullptr; }
    lua_sethook(L, timeoutHook, LUA_MASKCOUNT, 1000);
    return L;
}

void registerEverything() {
    // RCSS properties of the custom elements.
    Rml::StyleSheetSpecification::RegisterProperty("frames", "1", false, false).AddParser("number");
    Rml::StyleSheetSpecification::RegisterProperty("spriteprefix", "", false, false).AddParser("string");
    Rml::StyleSheetSpecification::RegisterProperty("detents", "24", false, false).AddParser("number");
    registerElements();
    Rml::RegisterEventType("frame", false, false);
}

}  // namespace

std::recursive_mutex& skinLock() {
    static std::recursive_mutex m;
    return m;
}

lua_State* luaState() { return g_L; }

bool acquireSystem(std::string& error) {
    if (g_users > 0) { ++g_users; return true; }
    if (!g_system) { g_system = new SystemInterface; g_files = new FileInterface; }
    Rml::SetSystemInterface(g_system);
    Rml::SetFileInterface(g_files);
    if (!Rml::Initialise()) { error = "The skin system (RmlUi) could not start."; return false; }
    g_L = makeLua();
    if (!g_L) { Rml::Shutdown(); error = "The skin script engine (Lua) could not start."; return false; }
    Rml::Lua::Initialise(g_L);
    registerEverything();
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    g_baseEnv = luaL_ref(g_L, LUA_REGISTRYINDEX);
    g_activeEnv = g_baseEnv;
    g_debuggerReady = false;
    g_debuggerContext = nullptr;
    ++g_users;
    return true;
}

void releaseSystem() {
    if (g_users == 0 || --g_users > 0) return;
    Rml::Shutdown();
    lua_close(g_L);
    g_L = nullptr;
    g_baseEnv = g_activeEnv = LUA_NOREF;
    g_folders.clear();
    g_fonts.clear();
    g_debuggerReady = false;
    g_debuggerContext = nullptr;
}

void allowFolder(const std::string& folder) { g_folders.insert(folderKey(fs::u8path(folder))); }

void loadFont(const std::string& path) {
    if (g_fonts.insert(path).second && !Rml::LoadFontFace(path)) g_fonts.erase(path);
}

void showDebugger(Rml::Context* context, bool visible) {
    if (!g_debuggerReady) {
        if (!visible) return;
        g_debuggerReady = Rml::Debugger::Initialise(context);
        if (!g_debuggerReady) return;
    } else if (visible && g_debuggerContext != context) {
        Rml::Debugger::SetContext(context);
    }
    if (visible) g_debuggerContext = context;
    if (visible || g_debuggerContext == context) Rml::Debugger::SetVisible(visible);
}

void contextClosing(Rml::Context* context) {
    if (g_debuggerReady && g_debuggerContext == context) {
        Rml::Debugger::SetVisible(false);
        Rml::Debugger::SetContext(nullptr);
        g_debuggerContext = nullptr;
    }
}

// ---- scopes and environments

SkinView* SkinView::current() { return g_current; }

ViewScope::ViewScope(SkinView* view) : lock_(skinLock()), previous_(g_current), previousEnv_(g_activeEnv) {
    if (g_scopeDepth++ == 0) g_deadline = Clock::now() + std::chrono::milliseconds(int(kLuaTimeoutSeconds * 1000));
    g_current = view;
    if (g_L && view && view->envRef != LUA_NOREF && view->envRef != g_activeEnv) {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, view->envRef);
        lua_rawseti(g_L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        g_activeEnv = view->envRef;
    }
}

ViewScope::~ViewScope() {
    if (g_L && previousEnv_ != g_activeEnv && previousEnv_ != LUA_NOREF) {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, previousEnv_);
        lua_rawseti(g_L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        g_activeEnv = previousEnv_;
    }
    g_current = previous_;
    --g_scopeDepth;
}

void dropEnvironment(SkinView& view) {
    if (!g_L || view.envRef == LUA_NOREF) return;
    if (g_activeEnv == view.envRef) {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_baseEnv);
        lua_rawseti(g_L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        g_activeEnv = g_baseEnv;
    }
    luaL_unref(g_L, LUA_REGISTRYINDEX, view.envRef);
    view.envRef = LUA_NOREF;
}

void resetEnvironment(SkinView& view) {
    dropEnvironment(view);
    lua_State* L = g_L;
    lua_newtable(L);                                   // env
    lua_newtable(L);                                   // its metatable: missing names come from the shared globals
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_baseEnv);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");                         // _G is the view's own environment
    view.envRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_rawgeti(L, LUA_REGISTRYINDEX, view.envRef);
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    g_activeEnv = view.envRef;
}

}  // namespace skin

// Lua's standard luaL_openlibs (linit.c) is not linked: this one opens only the sandbox's libraries, so nothing
// (including the RmlUi Lua plugin) can open io, os, package or debug.
void luaL_openlibs(lua_State* L) {
    const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base},       {LUA_COLIBNAME, luaopen_coroutine},
                             {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
                             {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}};
    for (const luaL_Reg& lib : libs) { luaL_requiref(L, lib.name, lib.func, 1); lua_pop(L, 1); }
}

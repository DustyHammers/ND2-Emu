// The skin's Lua API: global tables `panel` and `plugin` in each view's environment.
//
//   panel.led(code) -> brightness 0 (off) ... 8 (full)     panel.onLed(code, fn(level))   (on and off edges)
//   panel.press(index)  panel.release(index)  panel.isHeld(index)  panel.onButton(index, fn(down))
//   panel.turn(detents)                                    panel.onEncoder(fn(detents))
//   panel.master() -> 0 ... 1023   panel.setMaster(raw)    panel.onMaster(fn(raw))
//   panel.display() -> b0, b1, b2, b3 (leftmost first, active low)   panel.onDisplay(fn(b0, b1, b2, b3))
//   plugin.name, plugin.vendor, plugin.version
#include "skin_lua.h"
#include <RmlUi/Core/Log.h>
#include <RmlUi/Lua/IncludeLua.h>
#include <string>
#include "skin_system.h"
#include "skin_view.h"

namespace skin {
namespace {

SkinView& viewOf(lua_State* L) { return *static_cast<SkinView*>(lua_touserdata(L, lua_upvalueindex(1))); }

int intArg(lua_State* L, int i, int lo, int hi, const char* what) {
    const lua_Integer v = luaL_checkinteger(L, i);
    if (v < lo || v > hi) luaL_error(L, "%s must be %d to %d (got %d)", what, lo, hi, int(v));
    return int(v);
}

void addFnAt(lua_State* L, int arg, std::vector<int>& v) {
    luaL_checktype(L, arg, LUA_TFUNCTION);
    lua_pushvalue(L, arg);
    v.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
}

int led(lua_State* L) { lua_pushinteger(L, viewOf(L).ledLevel(intArg(L, 1, 0, kLeds - 1, "LED code"))); return 1; }
int onLed(lua_State* L) { const int c = intArg(L, 1, 0, kLeds - 1, "LED code"); addFnAt(L, 2, viewOf(L).onLedFns[c]); return 0; }
int press(lua_State* L) { viewOf(L).button(intArg(L, 1, 0, kButtons - 1, "button"), true); return 0; }
int release(lua_State* L) { viewOf(L).button(intArg(L, 1, 0, kButtons - 1, "button"), false); return 0; }
int isHeld(lua_State* L) { lua_pushboolean(L, viewOf(L).isHeld(intArg(L, 1, 0, kButtons - 1, "button"))); return 1; }
int onButton(lua_State* L) { const int b = intArg(L, 1, 0, kButtons - 1, "button"); addFnAt(L, 2, viewOf(L).onButtonFns[b]); return 0; }
int turn(lua_State* L) { viewOf(L).turnEncoder(intArg(L, 1, -64, 64, "detents")); return 0; }
int onEncoder(lua_State* L) { addFnAt(L, 1, viewOf(L).onEncoderFns); return 0; }
int master(lua_State* L) { lua_pushinteger(L, viewOf(L).port().master()); return 1; }
int setMaster(lua_State* L) { viewOf(L).setMaster(intArg(L, 1, 0, kMasterMax, "master")); return 0; }
int onMaster(lua_State* L) { addFnAt(L, 1, viewOf(L).onMasterFns); return 0; }
int display(lua_State* L) {
    for (uint8_t b : viewOf(L).displayBytes()) lua_pushinteger(L, b);
    return 4;
}
int onDisplay(lua_State* L) { addFnAt(L, 1, viewOf(L).onDisplayFns); return 0; }

int traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

template <class Push> void callAll(const std::vector<int>& fns, int nargs, Push push) {
    lua_State* L = luaState();
    if (!L) return;
    const std::vector<int> copy = fns;               // a callback may register more callbacks
    for (int ref : copy) {
        if (SkinView* v = SkinView::current()) v->countLuaCall();
        const int base = lua_gettop(L);
        lua_pushcfunction(L, traceback);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        push(L);
        if (lua_pcall(L, nargs, 0, base + 1) != LUA_OK)
            Rml::Log::Message(Rml::Log::LT_ERROR, "Lua: %s", lua_tostring(L, -1));
        lua_settop(L, base);
    }
}

}  // namespace

void installLuaApi(SkinView& v) {
    lua_State* L = luaState();
    const luaL_Reg fns[] = {{"led", led},         {"onLed", onLed},         {"press", press},       {"release", release},
                            {"isHeld", isHeld},   {"onButton", onButton},   {"turn", turn},         {"onEncoder", onEncoder},
                            {"master", master},   {"setMaster", setMaster}, {"onMaster", onMaster}, {"display", display},
                            {"onDisplay", onDisplay}, {nullptr, nullptr}};
    lua_newtable(L);
    lua_pushlightuserdata(L, &v);
    luaL_setfuncs(L, fns, 1);
    lua_setglobal(L, "panel");

    lua_newtable(L);
    lua_pushstring(L, v.info().name.c_str()); lua_setfield(L, -2, "name");
    lua_pushstring(L, v.info().vendor.c_str()); lua_setfield(L, -2, "vendor");
    lua_pushstring(L, v.info().version.c_str()); lua_setfield(L, -2, "version");
    lua_setglobal(L, "plugin");
}

void callLua(const std::vector<int>& fns, const std::vector<lua_Integer>& args) {
    callAll(fns, int(args.size()), [&](lua_State* L) { for (lua_Integer a : args) lua_pushinteger(L, a); });
}

void callLuaBool(const std::vector<int>& fns, bool arg) {
    callAll(fns, 1, [&](lua_State* L) { lua_pushboolean(L, arg); });
}

void dropLuaCallbacks(SkinView& v) {
    lua_State* L = luaState();
    auto drop = [&](std::vector<int>& refs) { if (L) for (int r : refs) luaL_unref(L, LUA_REGISTRYINDEX, r); refs.clear(); };
    for (auto* m : {&v.onLedFns, &v.onButtonFns}) {
        for (auto& kv : *m) drop(kv.second);
        m->clear();
    }
    drop(v.onDisplayFns);
    drop(v.onMasterFns);
    drop(v.onEncoderFns);
}

}  // namespace skin

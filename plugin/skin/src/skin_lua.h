// The Lua API of a skin view (see skin_lua.cpp for the list). Skin lock held, view scope active.
#pragma once
#include <vector>
#include <RmlUi/Lua/IncludeLua.h>

namespace skin {

class SkinView;

void installLuaApi(SkinView& v);                                          // into the current (view) environment
void callLua(const std::vector<int>& fns, const std::vector<lua_Integer>& args);   // errors are logged
void callLuaBool(const std::vector<int>& fns, bool arg);
void dropLuaCallbacks(SkinView& v);

}  // namespace skin

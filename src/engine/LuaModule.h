#pragma once

#include <glm/detail/type_mat4x4.hpp>
#include <safetyhook/inline_hook.hpp>
#include <utils/FunctionHook.h>

typedef const char * (*lua_Reader) (struct lua_State *L, void *ud, size_t *sz);


class LuaModule
{
public:
    void InstallHooks();

    inline static LuaModule* Get()
    {
        static auto instance(new LuaModule);
        return instance;
    }

private:
    LuaModule()  = default;
    ~LuaModule() = default;

    safetyhook::InlineHook m_onLua_load_buffer_x{};
    safetyhook::InlineHook m_onLua_load{};
public:
    static int on_load_buffer_x(struct lua_State *L, const char *buff, size_t size, const char *name, const char *mode);
    static int lua_load(struct lua_State *L, lua_Reader reader, void *data, const char *chunkname, const char *mode);
};

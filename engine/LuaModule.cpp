#include "LuaModule.h"
#include "engine/memory/memory_mul.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mods/VR.hpp>
#include <safetyhook/easy.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <string>
#include <utility/Module.hpp>

void SetupLogging() {
    try {
        auto file_logger = spdlog::basic_logger_mt("file_logger", "lua_dump.log");
        spdlog::set_default_logger(file_logger);
        // Set pattern to include thread ID and seconds.milliseconds with short log level
        spdlog::set_pattern("[%S.%e] [%L] [tid:%t] %v");
    } catch (...) {
        // Ignore setup errors
    }
}

void LuaModule::InstallHooks()
{
    SetupLogging();
    spdlog::info("Installing LuaModule hooks");
    uintptr_t addr = (uintptr_t)utility::get_executable() + 0x14f1a80;
    m_onLua_load_buffer_x = safetyhook::create_inline((void*)addr, (void*)&LuaModule::on_load_buffer_x);
    uintptr_t addr2 = (uintptr_t)utility::get_executable() + 0x14efd30;
    m_onLua_load = safetyhook::create_inline((void*)addr2, (void*)&LuaModule::lua_load);
}

int LuaModule::on_load_buffer_x(struct lua_State *L, const char *buff, size_t size, const char *name, const char *mode) {
    static auto instance = LuaModule::Get();
    spdlog::info("loading: {} bytes from buffer with name: {}", size, name);
    spdlog::info(buff);
    return instance->m_onLua_load_buffer_x.call<int>(L, buff, size, name, mode);
}

// Safe wrapper for lua_Reader that captures data for dumping
struct ReaderWrapper {
    lua_Reader original_reader;
    void* original_data;
    std::string captured_content;
    std::filesystem::path output_path;
    bool should_capture;
    
    ReaderWrapper(lua_Reader reader, void* data, const std::string& chunkname) 
        : original_reader(reader), original_data(data), should_capture(false) {
        
        if (!chunkname.empty()) {
            std::string chunk_str(chunkname);
            const std::string prefix = "@z:\\int9\\";
            
            if (chunk_str.find(prefix) == 0) {
                std::string relative_path = chunk_str.substr(prefix.length());
                std::replace(relative_path.begin(), relative_path.end(), '\\', '/');
                output_path = std::filesystem::path("sdk") / relative_path;
                
                // Only capture if file doesn't exist
                should_capture = !std::filesystem::exists(output_path);
            }
        }
    }
    
    const char* read_chunk(struct lua_State* L, size_t* sz) {
        try {
            const char* chunk_data = original_reader(L, original_data, sz);
            
            // Capture data if needed
            if (should_capture && chunk_data && sz && *sz > 0) {
                captured_content.append(chunk_data, *sz);
            }
            
            return chunk_data;
        } catch (...) {
            should_capture = false; // Disable capture on error
            if (sz) *sz = 0;
            return nullptr;
        }
    }
    
    void finalize_dump() {
        if (should_capture && !captured_content.empty() && !output_path.empty()) {
            try {
                std::filesystem::create_directories(output_path.parent_path());
                std::ofstream output_file(output_path, std::ios::binary);
                if (output_file.is_open()) {
                    output_file.write(captured_content.c_str(), captured_content.size());
                    output_file.close();
                    spdlog::info("Dumped Lua file: {}", output_path.string());
                }
            } catch (...) {
                // Silent error handling
            }
        }
    }
    
    static const char* wrapper_reader(struct lua_State* L, void* ud, size_t* sz) {
        ReaderWrapper* wrapper = static_cast<ReaderWrapper*>(ud);
        return wrapper->read_chunk(L, sz);
    }
};

int LuaModule::lua_load(struct lua_State* L, lua_Reader reader, void* data, const char* chunkname, const char* mode)
{
    static auto instance = LuaModule::Get();
    spdlog::info("loading chunk: {} with mode: {}", chunkname, mode);
    
    // Create wrapper for safe interception
    if (chunkname && reader) {
        try {
            ReaderWrapper wrapper(reader, data, chunkname);
            
            if (wrapper.should_capture) {
                // Use our wrapper reader
                int result = instance->m_onLua_load.call<int>(L, ReaderWrapper::wrapper_reader, &wrapper, chunkname, mode);
                wrapper.finalize_dump();
                return result;
            }
        } catch (...) {
            // Fall back to original on any error
        }
    }
    
    // Default: call original function without interception
    return instance->m_onLua_load.call<int>(L, reader, data, chunkname, mode);
}

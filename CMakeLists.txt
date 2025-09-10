cmake_minimum_required(VERSION 3.27)

project(
  gow_vr
  VERSION 1.0.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

include(GNUInstallDirs)

#add_compile_definitions(GLM_FORCE_LEFT_HANDED)

# CPM
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)
set(CPM_SOURCE_CACHE ${CMAKE_CURRENT_BINARY_DIR}/.cpm)

# Dependencies
#CPMAddPackage("gh:brofield/simpleini@4.20")
#CPMAddPackage("gh:stevemk14ebr/PolyHook_2_0#master")
# Target: minhook
include("cmake/spdlog.cmake")
include("cmake/minhook.cmake")
include("cmake/glm.cmake")
#include("cmake/vigemclient.cmake")
include("cmake/safetyhook.cmake")
#include("cmake/polyhook.cmake")
include("cmake/kananlib.cmake")
include("cmake/directxtk.cmake")
include("cmake/openxr.cmake")
#include("cmake/streamline.cmake")
#include("cmake/imgui.cmake")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/sdk/include)


# Add headers
file(
  GLOB_RECURSE
  headers
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

file(GLOB IMGUI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gow-imgui/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gow-imgui/*.hpp
)

list(REMOVE_ITEM headers ${CMAKE_CURRENT_SOURCE_DIR}/include/PCH.h)
list(APPEND headers ${IMGUI_HEADERS})


# Add sources
file(
  GLOB_RECURSE
  sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

file(GLOB IMGUI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imguizmo/*.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imnodes/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gow-imgui/*.cpp
)

list(APPEND sources ${IMGUI_SOURCES})

add_library(${PROJECT_NAME} SHARED ${headers} ${sources})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
#find_path(DIRECTX_INCLUDE_DIR d3dcompiler.h)
#find_library(D3D_COMPILER_LIB d3dcompiler.lib)
target_link_libraries("${PROJECT_NAME}" PUBLIC
        spdlog
        glm
        bddisasm
        shlwapi
        minhook
#        xbyak
        safetyhook
#        PolyHook_2
#        ViGEmClient
        kananlib
        openxr_loader
        delayimp
        DirectXTK
        DirectXTK12
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/openvr/lib/win64/openvr_api.lib"
)

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src>
    $<INSTALL_INTERFACE:src>
    $<BUILD_INTERFACE:${SimpleIni_SOURCE_DIR}>
    "extern/openvr/headers"
    "extern/nlohmann"
#   "${xbyak_SOURCE_DIR}/xbyak"
    "${minhook_SOURCE_DIR}/include"
    "${vigemclient_SOURCE_DIR}/include"
    "${minhook_SOURCE_DIR}/src/hde"
    # "${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/include"
    "${openxr_SOURCE_DIR}/src"
    "${spdlog_SOURCE_DIR}/src"
    "${glm_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/imguizmo"
#    "${CMAKE_CURRENT_SOURCE_DIR}/extern/imnodes"
    "${CMAKE_CURRENT_SOURCE_DIR}/gow-imgui"
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${SHADER_HEADER_DIR}>
)

#add_compile_definitions(DEBUG_PROFILING_ENABLED)


if(POLYHOOK2_FUNCTION_HOOK)
    add_compile_definitions(POLYHOOK2_FUNCTION_HOOK)
    include("cmake/polyhook.cmake")
    target_link_libraries("${PROJECT_NAME}" PUBLIC PolyHook_2)
endif()


target_compile_definitions(${PROJECT_NAME} PUBLIC
        "IMGUI_USER_CONFIG=\"${CMAKE_CURRENT_SOURCE_DIR}/gow-imgui/gow_imconfig.hpp\""
)

target_precompile_headers(
  ${PROJECT_NAME}
  PRIVATE
    include/PCH.h
)

target_compile_features(
  ${PROJECT_NAME}
  PUBLIC
    cxx_std_23
)

target_compile_options(
  ${PROJECT_NAME}
  PUBLIC
    $<$<CONFIG:Debug>:/EHa>
)

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    LINK_FLAGS
    "/DELAYLOAD:openvr_api.dll /DELAYLOAD:openxr_loader.dll /DELAYLOAD:d3d12.dll /DELAYLOAD:D3DCOMPILER_47.dll"
)

if(COMPILE_SHADERS)
  set(SHADER_HEADER_DIR ${CMAKE_BINARY_DIR}/shader-headers)
  file(MAKE_DIRECTORY ${SHADER_HEADER_DIR})

  # Build HLSL shaders
  add_custom_target(shaders)

  set_source_files_properties(debug_layer_vs.hlsl PROPERTIES ShaderType "vs_6_1" Entry "vs_main")
  set_source_files_properties(debug_layer_ps.hlsl PROPERTIES ShaderType "ps_6_1" Entry "ps_main")
  set_source_files_properties(motion_vector_correction_cs.hlsl PROPERTIES ShaderType "cs_6_1" Entry "CSMain")

  set(HLSL_SHADER_FILES
          debug_layer_ps.hlsl
          debug_layer_vs.hlsl
          motion_vector_correction_cs.hlsl
  )

  foreach(FILE ${HLSL_SHADER_FILES})
    get_filename_component(FILE_WE ${FILE} NAME_WE)
  #  list(APPEND CSO_SHADER_FILES ${CMAKE_BINARY_DIR}/${FILE_WE}.cso)
    get_source_file_property(shadertype ${FILE} ShaderType)
    get_source_file_property(functionentry ${FILE} Entry)
    add_custom_command(TARGET shaders
                       COMMAND dxc.exe /nologo /E${functionentry} /T${shadertype} $<IF:$<CONFIG:DEBUG>,/Od,/O3> /Zi /Fo ${CMAKE_BINARY_DIR}/${FILE_WE}.cso /Fh ${SHADER_HEADER_DIR}/${FILE_WE}.h /Fd ${CMAKE_BINARY_DIR}/${FILE_WE}.pdb ${FILE}
                       MAIN_DEPENDENCY ${FILE}
                       COMMENT "HLSL ${FILE}"
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders/
                       VERBATIM)
  endforeach(FILE)

  add_dependencies(${PROJECT_NAME} shaders)
endif()


#add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#                  COMMAND ${CMAKE_COMMAND} -E copy ${CSO_SHADER_FILES} $<TARGET_FILE_DIR:${PROJECT_NAME}>
#                  COMMAND_EXPAND_LISTS)
if(HOOK_METHOD STREQUAL "dinput8")
  set(OUTPUT_NAME "dinput8")
elseif(HOOK_METHOD STREQUAL "dxgi")
  set(OUTPUT_NAME "dxgi")
  add_compile_definitions(USE_DXGI_HOOK)
elseif(HOOK_METHOD STREQUAL "PHYSX")
  set(OUTPUT_NAME "PhysX3Cooking_x64")
  add_compile_definitions(USE_PHYSX_HOOK)
endif()

if(SIGNATURE_SCAN)
  add_compile_definitions(SIGNATURE_SCAN)
endif()

#add_compile_definitions(GOWR_DEMO_MODE)


install(
  TARGETS ${PROJECT_NAME}
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
  set(BUILD_NAME Debug)
else()
  set(BUILD_NAME Release)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${OUTPUT_NAME})

add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/artifact/Mod${BUILD_NAME}/gowvr
  COMMAND
    ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_CURRENT_SOURCE_DIR}/artifact/Mod${BUILD_NAME}/gowvr
  COMMAND
#    ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:${PROJECT_NAME}> ${CMAKE_CURRENT_SOURCE_DIR}/artifact/Mod${BUILD_NAME}/gowrvr
#[[  COMMAND
    ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/contrib/Config/${PROJECT_NAME}.ini ${CMAKE_CURRENT_SOURCE_DIR}/artifact/Mod${BUILD_NAME}/gowrvr]]
#   COMMAND
#    ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "C:\\Program Files (x86)\\Steam\\steamapps\\common\\God of War Ragnarök\\Data\\Plugins"
#  COMMAND
#    ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "C:\\Program Files (x86)\\Steam\\steamapps\\common\\God of War Ragnarök\\$<TARGET_FILE_NAME:${PROJECT_NAME}>"
  #COMMAND
  #  ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:${PROJECT_NAME}> "C:\\Program Files (x86)\\Steam\\steamapps\\common\\God of War Ragnarök\\gowrvr.pdb"
)

add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
       ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "C:\\Program Files (x86)\\Steam\\steamapps\\common\\GodOfWar\\$<TARGET_FILE_NAME:${PROJECT_NAME}>"
)


file(GLOB_RECURSE OUTPUT_DLLS ${CMAKE_CURRENT_SOURCE_DIR}/artifact/*.dll)
#file(GLOB_RECURSE OUTPUT_PDBS ${CMAKE_CURRENT_SOURCE_DIR}/artifact/*.pdb)

set_property(
  TARGET ${PROJECT_NAME}
  APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${OUTPUT_DLLS} ${OUTPUT_PDBS} ${CPM_SOURCE_CACHE} ${CMAKE_CURRENT_BINARY_DIR}/_deps
)

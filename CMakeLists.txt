cmake_minimum_required(VERSION 3.27)

project(
  assasins_creed_valhalla_vr
  VERSION 1.0.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

include(GNUInstallDirs)
include(FetchContent)

set(IMGUI_USER_CONFIG_PATH "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imconfig.hpp")
FetchContent_Declare(vrframework SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/vrframework)
FetchContent_MakeAvailable(vrframework)

add_library(common INTERFACE)
target_link_libraries(vr_framework PRIVATE common)


file(
  GLOB_RECURSE
  headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

file(
  GLOB_RECURSE
  sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_library(${PROJECT_NAME} SHARED ${headers} ${sources})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_link_libraries("${PROJECT_NAME}" PUBLIC common vr_framework::vr_framework)

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)



target_precompile_headers(
  ${PROJECT_NAME}
  PRIVATE
    pch/pch.h
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

if(NOT DEFINED HOOK_METHOD)
    set(HOOK_METHOD "dxgi")
endif()

if(HOOK_METHOD STREQUAL "dinput8")
  set(OUTPUT_NAME "dinput8")
elseif(HOOK_METHOD STREQUAL "dxgi")
  set(OUTPUT_NAME "dxgi")
  target_compile_definitions(common INTERFACE USE_DXGI_HOOK)
elseif(HOOK_METHOD STREQUAL "PHYSX")
  set(OUTPUT_NAME "PhysX3Cooking_x64")
  target_compile_definitions(common INTERFACE USE_PHYSX_HOOK)
elseif(HOOK_METHOD STREQUAL "inject")
  set(OUTPUT_NAME "vrmod")
endif()

if(SIGNATURE_SCAN)
  target_compile_definitions(common INTERFACE SIGNATURE_SCAN)
endif()

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
    ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/artifact/Mod${BUILD_NAME}/assasins_creed_valhalla_vr
  COMMAND
    ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_CURRENT_SOURCE_DIR}/artifact/Mod${BUILD_NAME}/assasins_creed_valhalla_vr
)

add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
       ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "C:\\UbisoftGames\\Assassin's Creed Valhalla\\$<TARGET_FILE_NAME:${PROJECT_NAME}>"
)

set_property(
  TARGET ${PROJECT_NAME}
  APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${OUTPUT_DLLS} ${OUTPUT_PDBS} ${CPM_SOURCE_CACHE} ${CMAKE_CURRENT_BINARY_DIR}/_deps
)

cmake_minimum_required(VERSION 3.27)

project(
  anvil_vr
  VERSION 1.0.0
  LANGUAGES C CXX
)

set(GAME_TARGETS Odyssey Origins Valhalla Mirage)

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

function(configure_game_target GAME_TARGET)
  set(TARGET_NAME "anvil_vr_${GAME_TARGET}")
  set(GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/games/${GAME_TARGET}")

  file(
    GLOB_RECURSE
    headers
      ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
      ${GAME_DIR}/*.h
      ${GAME_DIR}/*.hpp
  )

  file(
    GLOB_RECURSE
    sources
      ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
      ${GAME_DIR}/*.cpp
  )

  add_library(${TARGET_NAME} SHARED ${headers} ${sources})

  target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  add_library(${TARGET_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})
  target_link_libraries(${TARGET_NAME} PUBLIC common)

  target_link_libraries(${TARGET_NAME} PUBLIC vr_framework::vr_framework)

  target_include_directories(
    ${TARGET_NAME}
    PRIVATE
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
      $<BUILD_INTERFACE:${GAME_DIR}>
  )

  target_precompile_headers(
    ${TARGET_NAME}
    PRIVATE
      pch/pch.h
  )

  target_compile_features(
    ${TARGET_NAME}
    PUBLIC
      cxx_std_23
  )

  target_compile_options(
    ${TARGET_NAME}
    PUBLIC
      $<$<CONFIG:Debug>:/EHa>
  )

  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES
      LINK_FLAGS
      "/DELAYLOAD:openvr_api.dll /DELAYLOAD:d3d12.dll /DELAYLOAD:D3DCOMPILER_47.dll"
      OUTPUT_NAME
      ${OUTPUT_NAME}
      RUNTIME_OUTPUT_DIRECTORY
      "${CMAKE_CURRENT_BINARY_DIR}/bin/${GAME_TARGET}"
      LIBRARY_OUTPUT_DIRECTORY
      "${CMAKE_CURRENT_BINARY_DIR}/bin/${GAME_TARGET}"
      ARCHIVE_OUTPUT_DIRECTORY
      "${CMAKE_CURRENT_BINARY_DIR}/bin/${GAME_TARGET}"
  )

  install(
    TARGETS ${TARGET_NAME}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(BUILD_NAME Debug)
  else()
    set(BUILD_NAME Release)
  endif()

  if(GAME_TARGET STREQUAL "Odyssey")
    set(GAME_ARTIFACT_DIR "anvil_vr_odyssey")
    set(GAME_STEAM_PATH "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Assassins Creed Odyssey")
  elseif(GAME_TARGET STREQUAL "Origins")
    set(GAME_ARTIFACT_DIR "anvil_vr_origins")
    set(GAME_STEAM_PATH "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Assassins Creed Origins")
  elseif(GAME_TARGET STREQUAL "Valhalla")
    set(GAME_ARTIFACT_DIR "assasins_creed_valhalla_vr")
    set(GAME_STEAM_PATH "C:\\UbisoftGames\\Assassin's Creed Valhalla")
  elseif(GAME_TARGET STREQUAL "Mirage")
    set(GAME_ARTIFACT_DIR "assasins_creed_mirage_vr")
    set(GAME_STEAM_PATH "C:\\UbisoftGames\\Assassin's Creed Mirage")
  endif()

  add_custom_command(
    TARGET ${TARGET_NAME}
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/artifact/Mod${BUILD_NAME}/${GAME_ARTIFACT_DIR}
    COMMAND
      ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET_NAME}> ${CMAKE_CURRENT_SOURCE_DIR}/artifact/Mod${BUILD_NAME}/${GAME_ARTIFACT_DIR}
  )

  add_custom_command(
    TARGET ${TARGET_NAME}
    POST_BUILD
    COMMAND
      ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET_NAME}> "${GAME_STEAM_PATH}\\$<TARGET_FILE_NAME:${TARGET_NAME}>"
  )

  set_property(
    TARGET ${TARGET_NAME}
    APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${OUTPUT_DLLS} ${OUTPUT_PDBS} ${CPM_SOURCE_CACHE} ${CMAKE_CURRENT_BINARY_DIR}/_deps
  )
endfunction()

foreach(GAME_TARGET IN LISTS GAME_TARGETS)
  configure_game_target(${GAME_TARGET})
endforeach()

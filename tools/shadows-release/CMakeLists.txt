cmake_minimum_required(VERSION 3.27)

project(
  anvil_shadows_vr_release
  VERSION 0.1.0
  LANGUAGES C CXX
)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "The Shadows VR release targets currently require Windows and MSVC.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
# OpenXR-SDK 1.0.22 predates CMake 4's removal of pre-3.5 compatibility.
# Keep this policy floor local to the standalone release build.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)
include(GNUInstallDirs)

get_filename_component(ANVIL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(
  ANVIL_OPENXR_SOURCE_DIR
  "${ANVIL_ROOT}/build/full/_deps/openxr-src"
  CACHE PATH
  "Existing OpenXR-SDK source tree; leave empty to use find_package or the pinned public FetchContent fallback"
)
set(
  ANVIL_OPENXR_LICENSE_DIR
  ""
  CACHE PATH
  "License directory for a source-less installed OpenXR package"
)
set(ANVIL_OPENXR_FROM_INSTALLED_PACKAGE OFF)

# Prefer the explicit/default verified source checkout for reproducible builds.
# Clearing ANVIL_OPENXR_SOURCE_DIR opts into an installed SDK, with the same
# pinned public OpenXR-SDK revision retained as the final fallback.
if(ANVIL_OPENXR_SOURCE_DIR AND EXISTS "${ANVIL_OPENXR_SOURCE_DIR}/CMakeLists.txt")
  set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
  set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
  add_subdirectory("${ANVIL_OPENXR_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/openxr-build" EXCLUDE_FROM_ALL)
  set(ANVIL_OPENXR_TARGET openxr_loader)
else()
  find_package(OpenXR CONFIG QUIET)
  if(TARGET OpenXR::openxr_loader)
    set(ANVIL_OPENXR_TARGET OpenXR::openxr_loader)
    set(ANVIL_OPENXR_FROM_INSTALLED_PACKAGE ON)
  else()
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      openxr
      GIT_REPOSITORY "https://github.com/KhronosGroup/OpenXR-SDK.git"
      GIT_TAG "458984d7f59d1ae6dc1b597d94b02e4f7132eaba"
      GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(openxr)
    set(ANVIL_OPENXR_SOURCE_DIR "${openxr_SOURCE_DIR}")
    set(ANVIL_OPENXR_TARGET openxr_loader)
  endif()
endif()

add_library(
  anvil_vr_common
  STATIC
  "${ANVIL_ROOT}/common/src/CameraReprojection.cpp"
)
target_include_directories(anvil_vr_common PUBLIC "${ANVIL_ROOT}/common/include")
target_compile_features(anvil_vr_common PUBLIC cxx_std_23)
target_compile_definitions(anvil_vr_common PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(anvil_vr_common PRIVATE /W4 /WX)
target_link_libraries(anvil_vr_common PUBLIC d3d12 dxgi)

add_library(
  anvil_shadows_camera_contract
  STATIC
  "${ANVIL_ROOT}/games/Shadows/sdk/EyeCamera.cpp"
)
target_include_directories(
  anvil_shadows_camera_contract
  PUBLIC
    "${ANVIL_ROOT}/games/Shadows/sdk"
)
target_link_libraries(anvil_shadows_camera_contract PUBLIC anvil_vr_common)
target_compile_options(anvil_shadows_camera_contract PRIVATE /W4 /WX)

add_executable(ShadowsVisionPro "${ANVIL_ROOT}/tools/shadows_vr_screen.cpp")
target_include_directories(ShadowsVisionPro PRIVATE "${ANVIL_ROOT}/tools")
target_compile_definitions(ShadowsVisionPro PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ShadowsVisionPro PRIVATE /W4)
target_link_libraries(
  ShadowsVisionPro
  PRIVATE
    anvil_vr_common
    ${ANVIL_OPENXR_TARGET}
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    user32
    advapi32
    windowsapp
    runtimeobject
)

add_library(
  shadows_camera_math_bridge
  SHARED
  "${ANVIL_ROOT}/tools/shadows_camera_math_bridge.cpp"
)
target_link_libraries(shadows_camera_math_bridge PRIVATE anvil_shadows_camera_contract)
target_compile_definitions(shadows_camera_math_bridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(shadows_camera_math_bridge PRIVATE /W4 /WX)

function(add_shadows_color_bridge target output_name)
  add_library(${target} SHARED "${ANVIL_ROOT}/tools/shadows_native_color_bridge.cpp")
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output_name}")
  target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(${target} PRIVATE /W4)
  target_link_libraries(${target} PRIVATE anvil_vr_common d3d12 d3d11 dxgi user32)
endfunction()

add_shadows_color_bridge(shadows_native_color_bridge shadows_native_color_bridge)
add_shadows_color_bridge(shadows_native_color_receiver ShadowsColorReceiver)

set(ANVIL_RELEASE_TARGETS
  ShadowsVisionPro
  shadows_camera_math_bridge
  shadows_native_color_bridge
  shadows_native_color_receiver
)

foreach(target IN LISTS ANVIL_RELEASE_TARGETS)
  target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>")
endforeach()

get_target_property(ANVIL_OPENXR_TARGET_TYPE ${ANVIL_OPENXR_TARGET} TYPE)
if(ANVIL_OPENXR_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
  set(ANVIL_OPENXR_LINKAGE shared)
else()
  set(ANVIL_OPENXR_LINKAGE static)
endif()

install(
  TARGETS ${ANVIL_RELEASE_TARGETS}
  RUNTIME DESTINATION .
  LIBRARY DESTINATION .
)
install(FILES "${ANVIL_ROOT}/LICENSE" DESTINATION licenses RENAME AnvilEngine2VR-LICENSE.txt)

if(TARGET OpenXR::openxr_loader)
  get_target_property(ANVIL_OPENXR_TARGET_TYPE OpenXR::openxr_loader TYPE)
  if(ANVIL_OPENXR_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
    install(IMPORTED_RUNTIME_ARTIFACTS OpenXR::openxr_loader RUNTIME DESTINATION .)
  endif()
endif()

if(ANVIL_OPENXR_SOURCE_DIR AND EXISTS "${ANVIL_OPENXR_SOURCE_DIR}/LICENSE")
  install(FILES "${ANVIL_OPENXR_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME OpenXR-SDK-LICENSE.txt)
endif()
if(ANVIL_OPENXR_SOURCE_DIR AND EXISTS "${ANVIL_OPENXR_SOURCE_DIR}/COPYING.adoc")
  install(FILES "${ANVIL_OPENXR_SOURCE_DIR}/COPYING.adoc" DESTINATION licenses RENAME OpenXR-SDK-COPYING.adoc)
endif()
if(ANVIL_OPENXR_FROM_INSTALLED_PACKAGE)
  if(ANVIL_OPENXR_LICENSE_DIR AND EXISTS "${ANVIL_OPENXR_LICENSE_DIR}/LICENSE")
    install(FILES "${ANVIL_OPENXR_LICENSE_DIR}/LICENSE" DESTINATION licenses RENAME OpenXR-SDK-LICENSE.txt)
    if(EXISTS "${ANVIL_OPENXR_LICENSE_DIR}/COPYING.adoc")
      install(FILES "${ANVIL_OPENXR_LICENSE_DIR}/COPYING.adoc" DESTINATION licenses RENAME OpenXR-SDK-COPYING.adoc)
    endif()
  else()
    install(CODE "message(FATAL_ERROR \"Installed OpenXR package has no source license path. Set ANVIL_OPENXR_LICENSE_DIR before packaging.\")")
  endif()
endif()

install(CODE "
  if(NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL \"Release\")
    message(FATAL_ERROR \"The native release manifest is only valid for a Release install.\")
  endif()
  set(_manifest_files
    \"ShadowsVisionPro.exe\"
    \"shadows_camera_math_bridge.dll\"
    \"shadows_native_color_bridge.dll\"
    \"ShadowsColorReceiver.dll\"
  )
  set(_file_entries \"\")
  foreach(_file IN LISTS _manifest_files)
    set(_path \"\${CMAKE_INSTALL_PREFIX}/\${_file}\")
    if(NOT EXISTS \"\${_path}\")
      message(FATAL_ERROR \"Missing installed native release artifact: \${_file}\")
    endif()
    file(SIZE \"\${_path}\" _size)
    if(_file_entries)
      string(APPEND _file_entries \",\\n\")
    endif()
    string(APPEND _file_entries \"    {\\\"name\\\": \\\"\${_file}\\\", \\\"size\\\": \${_size}}\")
  endforeach()
  file(WRITE \"\${CMAKE_INSTALL_PREFIX}/native-build-manifest.json\"
    \"{\\n\"
    \"  \\\"schema_version\\\": 1,\\n\"
    \"  \\\"snapshot\\\": \\\"working-tree-snapshot\\\",\\n\"
    \"  \\\"configuration\\\": \\\"Release\\\",\\n\"
    \"  \\\"architecture\\\": \\\"x64\\\",\\n\"
    \"  \\\"openxr_loader_linkage\\\": \\\"${ANVIL_OPENXR_LINKAGE}\\\",\\n\"
    \"  \\\"artifacts\\\": [\\n\${_file_entries}\\n  ],\\n\"
    \"  \\\"expected_sources\\\": [\\n\"
    \"    \\\"tools/shadows_vr_screen.cpp\\\",\\n\"
    \"    \\\"tools/shadows_eye_gaze.hpp\\\",\\n\"
    \"    \\\"tools/shadows_camera_math_bridge.cpp\\\",\\n\"
    \"    \\\"tools/shadows_native_color_bridge.cpp\\\",\\n\"
    \"    \\\"games/Shadows/sdk/EyeCamera.cpp\\\",\\n\"
    \"    \\\"common/src/CameraReprojection.cpp\\\"\\n\"
    \"  ]\\n\"
    \"}\\n\"
  )
")

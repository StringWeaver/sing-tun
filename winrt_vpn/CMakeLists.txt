cmake_minimum_required(VERSION 3.20)
project(VpnBridge LANGUAGES CXX)

set(CMAKE_SYSTEM_VERSION "10.0")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WINSDK_METADATA_DIR "C:\\Program Files (x86)\\Windows Kits\\10\\UnionMetadata\\10.0.26100.0")
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Generated)

# --- Step 1: IDL → winmd ---
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND midlrt.exe /winrt /metadata_dir "${WINSDK_METADATA_DIR}"
            /out build /nomidl /winmd VpnTask.winmd
            VpnTask.idl
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/VpnTask.idl
    COMMENT "Compiling WinRT IDL → VpnTask.winmd"
)

# --- Step 2: winmd → projection headers (without -comp) ---
add_custom_command(
    OUTPUT ${GENERATED_DIR}/winrt/SingTun.h
    COMMAND cppwinrt.exe -in ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
            -ref "10.0.26100.0+"
            -out ${GENERATED_DIR} -overwrite
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
    COMMENT "Generating C++/WinRT projection headers"
)

# --- Step 3: winmd → component stubs (with -comp) ---
# cppwinrt -comp generates module.g.cpp + SingTun.VpnTask.h.
# We ONLY need module.g.cpp. The generated SingTun.VpnTask.h is a stub with a static_assert.
# We bypass the stub by having our own SingTun.VpnTask.h in the source root that just #include "VpnTask.h".
add_custom_command(
    OUTPUT ${GENERATED_DIR}/module.g.cpp
    COMMAND cppwinrt.exe -in ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
            -ref "10.0.26100.0+"
            -out ${GENERATED_DIR} -comp ${GENERATED_DIR} -overwrite
    # Delete the generated stub to force module.g.cpp to #include our own SingTun.VpnTask.h
    COMMAND ${CMAKE_COMMAND} -E rm -f ${GENERATED_DIR}/SingTun.VpnTask.h
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
    COMMENT "Generating C++/WinRT component module.g.cpp and removing stub"
)

add_custom_target(GenerateWinRTHeaders DEPENDS
    ${GENERATED_DIR}/winrt/SingTun.h
    ${GENERATED_DIR}/module.g.cpp
)

# --- Main DLL target ---
add_library(VpnBridge SHARED
    VpnTask.h
    VpnTask.cpp
    VpnPlugin.h
    VpnBridge.cpp
    ${GENERATED_DIR}/module.g.cpp
    VpnBridge.def
    pch.h
)

add_dependencies(VpnBridge GenerateWinRTHeaders)

target_include_directories(VpnBridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GENERATED_DIR})
target_compile_options(VpnBridge PRIVATE /bigobj)
target_compile_definitions(VpnBridge PRIVATE _WINRT_DLL)
target_precompile_headers(VpnBridge PRIVATE pch.h)
target_link_libraries(VpnBridge PRIVATE windowsapp)
target_link_options(VpnBridge PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/VpnBridge.def)
set_property(TARGET VpnBridge PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

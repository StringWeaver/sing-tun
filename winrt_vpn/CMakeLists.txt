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
    OUTPUT ${GENERATED_DIR}/winrt/Maple_Task.h
    COMMAND cppwinrt.exe -in ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
            -ref "10.0.26100.0+"
            -out ${GENERATED_DIR} -overwrite
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
    COMMENT "Generating C++/WinRT projection headers"
)

# --- Step 3: winmd → component stubs (with -comp), then strip static_assert ---
# cppwinrt -comp generates module.g.cpp + Maple_Task.VpnTask.h automatically,
# but Maple_Task.VpnTask.h contains a static_assert(false) guard that must be
# removed (as the file's own comment instructs). We do this via a post-step
# that deletes any line containing that static_assert.
add_custom_command(
    OUTPUT ${GENERATED_DIR}/module.g.cpp
           ${GENERATED_DIR}/Maple_Task.VpnTask.h
    COMMAND cppwinrt.exe -in ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
            -ref "10.0.26100.0+"
            -out ${GENERATED_DIR} -comp ${GENERATED_DIR} -overwrite
    COMMAND cmd /C "findstr /V /C:\"static_assert(false\" \"${GENERATED_DIR}\\Maple_Task.VpnTask.h\" > \"${GENERATED_DIR}\\Maple_Task.VpnTask.h.tmp\" && move /Y \"${GENERATED_DIR}\\Maple_Task.VpnTask.h.tmp\" \"${GENERATED_DIR}\\Maple_Task.VpnTask.h\""
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
    COMMENT "Generating C++/WinRT component stubs + stripping static_assert"
)

add_custom_target(GenerateWinRTHeaders DEPENDS
    ${GENERATED_DIR}/winrt/Maple_Task.h
    ${GENERATED_DIR}/module.g.cpp
)

# --- Main DLL target ---
add_library(VpnBridge SHARED
    VpnTask.h
    VpnTask.cpp
    VpnPlugin.h
    VpnBridge.cpp
    ${GENERATED_DIR}/module.g.cpp
    ${GENERATED_DIR}/Maple_Task.VpnTask.h
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

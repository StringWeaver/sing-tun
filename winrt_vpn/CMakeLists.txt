cmake_minimum_required(VERSION 3.20)
project(VpnPlugin LANGUAGES CXX)

set(CMAKE_SYSTEM_VERSION "10.0")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile MIDL manually via custom command instead of relying on the buggy MSBuild target
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND midlrt.exe /winrt /metadata_dir "C:\\Program Files (x86)\\Windows Kits\\10\\UnionMetadata\\10.0.26100.0" 
            /out build /nomidl /winmd VpnTask.winmd 
            "VpnTask.idl"
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/VpnTask.idl
)

# Run cppwinrt.exe manually
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/Generated/winrt/Maple_Task.h
    COMMAND cppwinrt.exe -in ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd 
            -ref "10.0.26100.0+" 
            -out ${CMAKE_CURRENT_SOURCE_DIR}/Generated -overwrite
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
)

# Also generate component module
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/Generated/module.g.cpp
    COMMAND cppwinrt.exe -in ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd 
            -ref "10.0.26100.0+" 
            -out ${CMAKE_CURRENT_SOURCE_DIR}/Generated -comp Generated -overwrite
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/VpnTask.winmd
)

add_custom_target(GenerateWinRTHeaders DEPENDS 
    ${CMAKE_CURRENT_SOURCE_DIR}/Generated/winrt/Maple_Task.h 
    ${CMAKE_CURRENT_SOURCE_DIR}/Generated/module.g.cpp)

add_library(VpnPlugin SHARED
    VpnTask.h
    VpnTask.cpp
    VpnPlugin.h
    VpnPlugin.cpp
    Generated/module.g.cpp
    pch.h
)

add_dependencies(VpnPlugin GenerateWinRTHeaders)

target_include_directories(VpnPlugin PRIVATE Generated ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(VpnPlugin PRIVATE /bigobj)
target_precompile_headers(VpnPlugin PRIVATE pch.h)
target_link_libraries(VpnPlugin PRIVATE windowsapp)
set_property(TARGET VpnPlugin PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

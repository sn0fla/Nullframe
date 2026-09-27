cmake_minimum_required(VERSION 3.20)
project(Nullframe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Nullframe WIN32
    "Source Code/Main.cpp"
    "Source Code/Window.cpp"
    "Source Code/Renderer.cpp"
)

target_include_directories(Nullframe PRIVATE "Source Code/Includes")

target_link_libraries(Nullframe PRIVATE
    d3d11
    dxgi
    d2d1
    dwrite
    dcomp
    user32
    gdi32
    advapi32
    shell32
)

if (MSVC)
    target_compile_options(Nullframe PRIVATE /W4 /permissive-)
    target_link_options(Nullframe PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup)
endif()
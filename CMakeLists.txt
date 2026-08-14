cmake_minimum_required (VERSION 3.10)

add_executable(Launcher WIN32 main.cpp)

set_target_properties(Launcher PROPERTIES FOLDER Launcher)

# 输出目录由顶层 CMAKE_RUNTIME_OUTPUT_DIRECTORY 统一指定（build/bin/<Config>/）
# WIN32 子系统：入口为 wWinMain，注意不要写成 int main

target_link_libraries(Launcher PRIVATE
    RendererConfig
    ImGui
    d3d11
    dxgi
)

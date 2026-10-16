cmake_minimum_required(VERSION 3.20)
project(VRChatChatboxInput VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /EHsc)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(vrc-chatbox-input WIN32 src/main.cpp src/app.rc)
target_link_libraries(vrc-chatbox-input PRIVATE ws2_32 comctl32 shell32 ole32 user32 gdi32)
target_compile_definitions(vrc-chatbox-input PRIVATE
    APP_VERSION_STR="${PROJECT_VERSION}"
    APP_VERSION_RC=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
if(MSVC)
    target_link_options(vrc-chatbox-input PRIVATE /MANIFEST:NO)
endif()

enable_testing()
add_executable(osc_test tests/osc_test.cpp)
target_include_directories(osc_test PRIVATE src)
add_test(NAME osc COMMAND osc_test)

# Types with SendInput into a stand-in for VRChat; run it by hand (tools\run-e2e.ps1).
add_executable(e2e_test tests/e2e_test.cpp)
target_include_directories(e2e_test PRIVATE src)
target_link_libraries(e2e_test PRIVATE ws2_32 user32)

cmake_minimum_required(VERSION 3.16)
# Port version: bump here (MAJOR.MINOR.PATCH), see CHANGELOG.md.
project(shinobi_legions_pc VERSION 0.6.0 LANGUAGES C)

# Short git hash of the build, if git is available.
set(SL_GIT_HASH "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE SL_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT SL_GIT_HASH)
        set(SL_GIT_HASH "unknown")
    endif()
endif()
configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_executable(shinobi_legions_pc
    src/main.c
    src/boot.c
    src/sys.c
    src/sound.c
    src/object.c
    src/mode.c
    src/resource.c
    src/stream.c
    src/render.c
    src/input.c
    src/pad.c
    src/player.c
    src/player_stubs.c
    src/ram.c
    src/physics.c
    src/lz.c
    src/anim.c
    src/sprite.c
    src/collision.c
    src/camera.c
    src/stage.c
)

find_library(MATH_LIBRARY m)

target_include_directories(shinobi_legions_pc PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated ${SDL2_INCLUDE_DIRS})
target_link_libraries(shinobi_legions_pc PRIVATE ${SDL2_LIBRARIES})
if(MATH_LIBRARY)
    target_link_libraries(shinobi_legions_pc PRIVATE ${MATH_LIBRARY})
endif()

if(MSVC)
    target_compile_options(shinobi_legions_pc PRIVATE /W3)
else()
    target_compile_options(shinobi_legions_pc PRIVATE -Wall -Wextra)
endif()

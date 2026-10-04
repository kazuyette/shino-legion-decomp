cmake_minimum_required(VERSION 3.16)
# Port version: bump here (MAJOR.MINOR.PATCH), see CHANGELOG.md.
project(shinobi_legions_pc VERSION 0.22.1 LANGUAGES C)

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
    src/config.c
    src/screenshot.c
    src/debug_menu.c
    src/boot.c
    src/sys.c
    src/sound.c
    src/object.c
    src/mode.c
    src/resource.c
    src/stream.c
    src/render.c
    src/crt.c
    src/soft_render.c
    src/input.c
    src/pad.c
    src/player.c
    src/player_stubs.c
    src/ninjutsu.c
    src/ram.c
    src/physics.c
    src/lz.c
    src/extract.c
    src/u8path.c
    src/anim.c
    src/sprite.c
    src/collision.c
    src/camera.c
    src/stage.c
    src/stage_inits.c
    src/stage_hooks.c
    src/objects.c
    src/objhandlers.c
    src/objhandlers_a.c
    src/objhandlers_b.c
    src/objhandlers_c.c
    src/objhandlers_d.c
    src/objhandlers_f.c
    src/objhandlers_f2.c
    src/objhandlers_f3.c
    src/objhandlers_f4.c
    src/objhandlers_e.c
    src/objhandlers_e2.c
    src/enemy.c
    src/fx.c
    src/fx_b.c
    src/fade.c
    src/enemy_soldier_sub.c
    src/enemy_soldier_a.c
    src/enemy_soldier_b.c
    src/enemy_soldier_c.c
    src/enemy_t3.c
    src/enemy_t4.c
    src/enemy_boss42.c
    src/enemy_more.c
    src/enemy_t0a.c
    src/enemy_t0b.c
    src/enemy_t0c.c
    src/enemy_t19.c
    src/enemy_t02.c
    src/enemy_soldier_sub_d.c
    src/enemy_t23.c
    src/enemy_t05.c
    src/enemy_t07.c
    src/enemy_t16.c
    src/enemy_t22.c
    src/enemy_t24.c
    src/enemy_boss18.c
    src/enemy_boss15.c
    src/enemy_boss06.c
    src/enemy_boss17.c
    src/enemy_boss1f.c
    src/enemy_boss21.c
    src/enemy_boss1a.c
    src/enemy_boss09.c
    src/shinobi.c
    src/director.c
    src/director_font.c
    src/director_title.c
    src/director_tally.c
    src/director_gameover.c
    src/movie.c
    src/movie_cpk.c
    src/cpk.c
    src/audio.c
    src/sfx.c
    src/hud.c
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

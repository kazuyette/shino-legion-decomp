cmake_minimum_required(VERSION 3.16)
project(shinobi_legions_pc C)

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
)

target_include_directories(shinobi_legions_pc PRIVATE src ${SDL2_INCLUDE_DIRS})
target_link_libraries(shinobi_legions_pc PRIVATE ${SDL2_LIBRARIES})

if(MSVC)
    target_compile_options(shinobi_legions_pc PRIVATE /W3)
else()
    target_compile_options(shinobi_legions_pc PRIVATE -Wall -Wextra)
endif()

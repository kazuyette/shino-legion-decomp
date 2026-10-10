# Cross-compile the Windows executable from Linux with MinGW-w64.
#   cmake -S pc-port -B build-win -DCMAKE_TOOLCHAIN_FILE=pc-port/cmake/mingw-w64-x86_64.cmake \
#         -DSDL2_DIR=/path/to/SDL2-2.30.x/x86_64-w64-mingw32/lib/cmake/SDL2
# (SDL2-devel-2.30.x-mingw.tar.gz from https://github.com/libsdl-org/SDL/releases)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
if(SDL2_DIR)
    get_filename_component(_sdl_root "${SDL2_DIR}/../../.." ABSOLUTE)
    list(APPEND CMAKE_FIND_ROOT_PATH "${_sdl_root}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

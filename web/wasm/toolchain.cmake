# CMake toolchain for building OpenVSP to WebAssembly (in-browser engine, experimental).
#
# Emscripten's own toolchain, plus: allow find_package()/find_library() outside the
# Emscripten sysroot, so the superbuild's libraries (installed under build/libs-wasm) are
# found by each other and by the main build.
#
# Point CMake at it through the environment so every ExternalProject sub-build gets it too:
#   CMAKE_TOOLCHAIN_FILE=<repo>/web/wasm/toolchain.cmake   (see build_wasm.ps1)

if( NOT DEFINED ENV{EMSDK} )
    message( FATAL_ERROR "EMSDK is not set: run emsdk_env first" )
endif()

include( "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" )

# Everything is built for the threaded engine (OpenVSP runs on its own thread; shared memory
# requires every object to be compiled for it) and with native WebAssembly exceptions
# (OpenVSP catches exceptions, e.g. from number parsing; without this a throw aborts).
set( CMAKE_C_FLAGS_INIT "-pthread -fwasm-exceptions" )
set( CMAKE_CXX_FLAGS_INIT "-pthread -fwasm-exceptions" )
set( CMAKE_EXE_LINKER_FLAGS_INIT "-pthread -fwasm-exceptions" )

set( CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH )
set( CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH )
set( CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH )

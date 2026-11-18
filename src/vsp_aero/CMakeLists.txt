CMAKE_MINIMUM_REQUIRED(VERSION 3.24)

ADD_SUBDIRECTORY( Solver )
# The viewer is a desktop OpenGL program; not built for WebAssembly.
IF( NOT EMSCRIPTEN )
    ADD_SUBDIRECTORY( Viewer )
ENDIF()
ADD_SUBDIRECTORY( Adb2Load )

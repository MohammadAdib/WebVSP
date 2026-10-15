# WebAssembly builds of the OpenVSP core (experimental in-browser engine).
# Added from src/CMakeLists.txt only when building with Emscripten.

SET( VSP_WASM_INCLUDES
    ${CodeEli_INCLUDE_DIRS} ${GEOM_API_INCLUDE_DIR} ${GEOM_CORE_INCLUDE_DIR} ${LIBIGES_INCLUDE_DIR}
    ${UTIL_INCLUDE_DIR} ${UTIL_API_INCLUDE_DIR} ${XMLVSP_INCLUDE_DIR} ${VSP_SOURCE_DIR} )

# Node smoke test: build, tessellate and round-trip a model.
ADD_EXECUTABLE( vsp_wasm_smoke smoke.cpp )
TARGET_LINK_LIBRARIES( vsp_wasm_smoke Eigen3::Eigen ${VSP_LIBRARIES_API_FIRST} )
TARGET_INCLUDE_DIRECTORIES( vsp_wasm_smoke PRIVATE ${VSP_WASM_INCLUDES} )
TARGET_LINK_OPTIONS( vsp_wasm_smoke PRIVATE -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB )

# The engine module the web UI loads (vsp.js + vsp.wasm), an ES module exporting a factory:
#   const vsp = await createVspModule(); vsp.call(...); vsp.take(); vsp.FS.writeFile(...)
# OpenVSP runs main() on its own thread (PROXY_TO_PTHREAD); the page calls in from the
# browser's main thread (see engine.cpp). Needs a cross-origin isolated page (threads).
ADD_EXECUTABLE( vsp_wasm engine.cpp )
SET_TARGET_PROPERTIES( vsp_wasm PROPERTIES OUTPUT_NAME vsp SUFFIX .js )
TARGET_INCLUDE_DIRECTORIES( vsp_wasm PRIVATE ${VSP_WASM_INCLUDES} )
IF( NOT VSP_NO_GRAPHICS )
    # With OpenVSP's GUI (FLTK on its headless platform), mirrored to the page.
    TARGET_COMPILE_DEFINITIONS( vsp_wasm PRIVATE VSP_WASM_GUI=1 )
    # The GUI never draws with OpenGL here: do-nothing GL (see gen_gl_stub.py).
    TARGET_SOURCES( vsp_wasm PRIVATE gl_stub.c )
    TARGET_LINK_LIBRARIES( vsp_wasm Eigen3::Eigen ${VSP_G_LIBRARIES_API_FIRST} )
ELSE()
    TARGET_LINK_LIBRARIES( vsp_wasm Eigen3::Eigen ${VSP_LIBRARIES_API_FIRST} )
ENDIF()
TARGET_LINK_OPTIONS( vsp_wasm PRIVATE
    -lembind
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createVspModule
    -sENVIRONMENT=web,worker
    -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=4
    -sINITIAL_MEMORY=256MB -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=8MB
    -sEXPORTED_RUNTIME_METHODS=FS
    # Function names in stack traces (small; makes engine crashes diagnosable).
    --profiling-funcs )

MACRO (TODAY RESULT)
    # Build date as mm/dd/yy. Built into CMake, so it also works when cross-compiling
    # (e.g. for WebAssembly), where the host's date command may not match the target.
    string(TIMESTAMP ${RESULT} "%m/%d/%y")
ENDMACRO (TODAY)

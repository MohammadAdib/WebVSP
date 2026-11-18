//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// Prints a symbolized native stack trace to stderr on a crash or abort (Windows only).
// Used by the web mirror, where the GUI runs inside a Python process.
//
//////////////////////////////////////////////////////////////////////

#include "CrashTrace.h"

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdio>

#pragma comment( lib, "dbghelp.lib" )

namespace
{

void PrintStack( CONTEXT *ctx, const char *why )
{
    HANDLE proc = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions( SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
    SymInitialize( proc, nullptr, TRUE );

    fprintf( stderr, "\n==== native crash: %s ====\n", why );

    CONTEXT local;
    if ( !ctx )
    {
        RtlCaptureContext( &local );
        ctx = &local;
    }
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = ctx->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    char buf[sizeof( SYMBOL_INFO ) + 512];
    for ( int i = 0; i < 48; i++ )
    {
        if ( !StackWalk64( IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame, ctx, nullptr,
                           SymFunctionTableAccess64, SymGetModuleBase64, nullptr ) || !frame.AddrPC.Offset )
        {
            break;
        }
        DWORD64 addr = frame.AddrPC.Offset;
        SYMBOL_INFO *sym = ( SYMBOL_INFO * ) buf;
        sym->SizeOfStruct = sizeof( SYMBOL_INFO );
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof( line );
        DWORD ldisp = 0;
        char modname[MAX_PATH] = "?";
        HMODULE mod = nullptr;
        if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 ( LPCSTR ) addr, &mod ) )
        {
            GetModuleFileNameA( mod, modname, MAX_PATH );
        }
        const char *base = strrchr( modname, '\\' );
        base = base ? base + 1 : modname;
        if ( SymFromAddr( proc, addr, &disp, sym ) )
        {
            if ( SymGetLineFromAddr64( proc, addr, &ldisp, &line ) )
            {
                fprintf( stderr, "  #%02d %s!%s  %s:%lu\n", i, base, sym->Name, line.FileName, line.LineNumber );
            }
            else
            {
                fprintf( stderr, "  #%02d %s!%s+0x%llx\n", i, base, sym->Name, ( unsigned long long ) disp );
            }
        }
        else
        {
            fprintf( stderr, "  #%02d %s+0x%llx\n", i, base, ( unsigned long long )( addr - ( DWORD64 ) mod ) );
        }
    }
    fflush( stderr );
}

LONG WINAPI OnException( EXCEPTION_POINTERS *ep )
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Only fatal hardware faults; C++ exceptions (0xE06D7363) are often caught further up.
    if ( code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW ||
         code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO )
    {
        char why[64];
        snprintf( why, sizeof( why ), "exception 0x%08lx", code );
        PrintStack( ep->ContextRecord, why );
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void OnAbort( int )
{
    PrintStack( nullptr, "abort()" );
}

} // namespace

void InstallCrashTrace()
{
    static bool done = false;
    if ( done )
    {
        return;
    }
    done = true;
    AddVectoredExceptionHandler( 1, OnException );
    signal( SIGABRT, OnAbort );
}

#else

void InstallCrashTrace()
{
}

#endif

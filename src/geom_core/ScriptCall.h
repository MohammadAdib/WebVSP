//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// How C++ functions are handed to AngelScript when registering the script API.
//
// Normally AngelScript calls them natively (asCALL_CDECL, asCALL_THISCALL, ...). Where that
// isn't possible -- WebAssembly, where AngelScript is built with AS_MAX_PORTABILITY -- every
// function is registered through a generic-calling-convention wrapper instead (the
// autowrapper add-on). Each macro expands to "<function pointer>, <calling convention>", so a
// registration reads the same either way:
//
//     se->RegisterGlobalFunction( "string GetVSPVersion()", VSP_FN( vsp::GetVSPVersion ) );
//
// Define VSP_AS_GENERIC to use the wrappers on any platform (e.g. to test them natively).
//
//////////////////////////////////////////////////////////////////////

#if !defined( VSP_SCRIPTCALL_H )
#define VSP_SCRIPTCALL_H

#include <angelscript.h>

#if defined( __EMSCRIPTEN__ ) && !defined( VSP_AS_GENERIC )
#define VSP_AS_GENERIC
#endif

#ifdef VSP_AS_GENERIC

#include <autowrapper/aswrappedcall.h>

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

// Generic wrapper for free functions of any arity (the add-on's stops at about ten
// arguments). Arguments and return values are handled as the add-on does (gw::Proxy).
namespace vsp_as
{
template < typename T > struct Fn;

template < typename R, typename... A >
struct Fn< R ( * )( A... ) >
{
    template < R ( *fp )( A... ) >
    static void Wrap( asIScriptGeneric *gen )
    {
        Call< fp >( gen, std::index_sequence_for< A... >() );
    }

    template < R ( *fp )( A... ), std::size_t... I >
    static void Call( asIScriptGeneric *gen, std::index_sequence< I... > )
    {
        if constexpr ( std::is_void< R >::value )
        {
            fp( static_cast< gw::Proxy< A > * >( gen->GetAddressOfArg( ( asUINT ) I ) )->value... );
        }
        else
        {
            new ( gen->GetAddressOfReturnLocation() )
                gw::Proxy< R >( fp( static_cast< gw::Proxy< A > * >( gen->GetAddressOfArg( ( asUINT ) I ) )->value... ) );
        }
    }
};

template < typename T > Fn< T > id( T ) { return Fn< T >(); }
}

#define VSP_FN( fn )                        asFUNCTION( ( decltype( ::vsp_as::id( fn ) )::template Wrap< fn > ) ), asCALL_GENERIC
#define VSP_FN_PR( f, params, ret )         WRAP_FN_PR( f, params, ret ), asCALL_GENERIC
#define VSP_MFN( cls, m )                   WRAP_MFN( cls, m ), asCALL_GENERIC
#define VSP_MFN_PR( cls, m, params, ret )   WRAP_MFN_PR( cls, m, params, ret ), asCALL_GENERIC
#define VSP_MFN_GLOBAL( cls, m )            WRAP_MFN_GLOBAL( cls, m ), asCALL_GENERIC
#define VSP_MFN_GLOBAL_PR( cls, m, params, ret ) WRAP_MFN_GLOBAL_PR( cls, m, params, ret ), asCALL_GENERIC
#define VSP_OBJ_FIRST( f )                  WRAP_OBJ_FIRST( f ), asCALL_GENERIC
#define VSP_OBJ_FIRST_PR( f, params, ret )  WRAP_OBJ_FIRST_PR( f, params, ret ), asCALL_GENERIC
#define VSP_OBJ_LAST( f )                   WRAP_OBJ_LAST( f ), asCALL_GENERIC
#define VSP_OBJ_LAST_PR( f, params, ret )   WRAP_OBJ_LAST_PR( f, params, ret ), asCALL_GENERIC

#else

#define VSP_FN( f )                         asFUNCTION( f ), asCALL_CDECL
#define VSP_FN_PR( f, params, ret )         asFUNCTIONPR( f, params, ret ), asCALL_CDECL
#define VSP_MFN( cls, m )                   asMETHOD( cls, m ), asCALL_THISCALL
#define VSP_MFN_PR( cls, m, params, ret )   asMETHODPR( cls, m, params, ret ), asCALL_THISCALL
#define VSP_MFN_GLOBAL( cls, m )            asMETHOD( cls, m ), asCALL_THISCALL_ASGLOBAL
#define VSP_MFN_GLOBAL_PR( cls, m, params, ret ) asMETHODPR( cls, m, params, ret ), asCALL_THISCALL_ASGLOBAL
#define VSP_OBJ_FIRST( f )                  asFUNCTION( f ), asCALL_CDECL_OBJFIRST
#define VSP_OBJ_FIRST_PR( f, params, ret )  asFUNCTIONPR( f, params, ret ), asCALL_CDECL_OBJFIRST
#define VSP_OBJ_LAST( f )                   asFUNCTION( f ), asCALL_CDECL_OBJLAST
#define VSP_OBJ_LAST_PR( f, params, ret )   asFUNCTIONPR( f, params, ret ), asCALL_CDECL_OBJLAST

#endif

#endif // VSP_SCRIPTCALL_H

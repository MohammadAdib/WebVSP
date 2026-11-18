//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#include "VspDialogs.h"
#include "WebMirror.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>

#include <cstdarg>
#include <cstdio>
#include <vector>

static std::string FormatV( const char *fmt, va_list ap )
{
    va_list ap2;
    va_copy( ap2, ap );
    int n = vsnprintf( nullptr, 0, fmt, ap2 );
    va_end( ap2 );
    if ( n <= 0 )
    {
        return std::string();
    }
    std::vector< char > buf( n + 1 );
    vsnprintf( buf.data(), buf.size(), fmt, ap );
    return std::string( buf.data(), n );
}

int VspChoice( const char *fmt, const char *b0, const char *b1, const char *b2, ... )
{
    va_list ap;
    va_start( ap, b2 );
    std::string msg = FormatV( fmt, ap );
    va_end( ap );

    if ( WebMirror::IsEnabled() )
    {
        return WebMirror::Choice( msg, b0 ? b0 : "", b1 ? b1 : "", b2 ? b2 : "" );
    }
    return fl_choice( "%s", b0, b1, b2, msg.c_str() );
}

void VspAlert( const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    std::string msg = FormatV( fmt, ap );
    va_end( ap );

    if ( WebMirror::IsEnabled() )
    {
        WebMirror::Message( "alert", msg );
        return;
    }
    fl_alert( "%s", msg.c_str() );
}

void VspMessage( const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    std::string msg = FormatV( fmt, ap );
    va_end( ap );

    if ( WebMirror::IsEnabled() )
    {
        WebMirror::Message( "message", msg );
        return;
    }
    fl_message( "%s", msg.c_str() );
}

void VspModalWait()
{
    if ( WebMirror::IsEnabled() )
    {
        WebMirror::Pump( 0.01 );
        return;
    }
    Fl::wait();
}

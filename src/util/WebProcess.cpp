//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// External programs run by a web page (see WebProcess.h).
//
//////////////////////////////////////////////////////////////////////

#include "WebProcess.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include <unistd.h>

namespace
{

struct Proc
{
    bool running = true;
    int code = -1;
    std::string out; // output not yet read
};

std::mutex g_Mutex;
std::map< int, Proc > g_Procs;
std::string g_Requests; // JSON objects, comma separated
int g_NextId = 1;

std::string Quote( const std::string &s )
{
    std::string out = "\"";
    for ( unsigned char c : s )
    {
        if ( c == '"' || c == '\\' )
        {
            out += '\\';
            out += ( char ) c;
        }
        else if ( c < 0x20 )
        {
            char buf[8];
            snprintf( buf, sizeof( buf ), "\\u%04x", c );
            out += buf;
        }
        else
        {
            out += ( char ) c;
        }
    }
    return out + "\"";
}

void Request( const std::string &json )
{
    g_Requests += ( g_Requests.empty() ? "" : "," ) + json;
}

} // namespace

int WebProcess::Start( const std::string &cmd, const std::vector< std::string > &args )
{
    char cwd[1024];
    if ( !getcwd( cwd, sizeof( cwd ) ) )
    {
        strcpy( cwd, "/" );
    }
    std::string a = "[";
    for ( size_t i = 0; i < args.size(); i++ )
    {
        a += ( i ? "," : "" ) + Quote( args[i] );
    }
    a += "]";

    std::lock_guard< std::mutex > lock( g_Mutex );
    int id = g_NextId++;
    g_Procs[id] = Proc();
    Request( "{\"t\":\"spawn\",\"id\":" + std::to_string( id ) + ",\"cmd\":" + Quote( cmd ) + ",\"args\":" + a +
             ",\"cwd\":" + Quote( cwd ) + "}" );
    return id;
}

bool WebProcess::Running( int id )
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    auto it = g_Procs.find( id );
    return it != g_Procs.end() && it->second.running;
}

int WebProcess::Read( int id, char *buf, int n )
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    auto it = g_Procs.find( id );
    if ( it == g_Procs.end() || n <= 0 )
    {
        return 0;
    }
    std::string &out = it->second.out;
    int len = ( int ) std::min< size_t >( out.size(), ( size_t ) n );
    memcpy( buf, out.data(), len );
    out.erase( 0, len );
    return len;
}

void WebProcess::Kill( int id )
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    auto it = g_Procs.find( id );
    if ( it != g_Procs.end() && it->second.running )
    {
        Request( "{\"t\":\"kill\",\"id\":" + std::to_string( id ) + "}" );
    }
}

int WebProcess::ExitCode( int id )
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    auto it = g_Procs.find( id );
    return ( it == g_Procs.end() || it->second.running ) ? -1 : it->second.code;
}

std::string WebProcess::TakeRequests()
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    std::string out = "[" + g_Requests + "]";
    g_Requests.clear();
    return out;
}

void WebProcess::Output( int id, const std::string &text )
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    auto it = g_Procs.find( id );
    if ( it != g_Procs.end() )
    {
        it->second.out += text;
    }
}

void WebProcess::Exit( int id, int code )
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    auto it = g_Procs.find( id );
    if ( it != g_Procs.end() )
    {
        it->second.running = false;
        it->second.code = code;
    }
}

// WebAssembly smoke test for the OpenVSP core: build a model through the API, tessellate it
// the way the web viewer does, and round-trip it through a .vsp3 file (in Emscripten's
// in-memory file system). Run with node.

#include "VSP_Geom_API.h"
#include "APIErrorMgr.h"
#include "Vec3d.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using std::string;
using std::vector;

namespace
{

double Ms( std::chrono::steady_clock::time_point t0 )
{
    return std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - t0 ).count();
}

// Same sampling as web/backend/vspweb/tess.py: the GUI's u/w tessellation of every surface.
int CountTessPoints( const string &geom_id )
{
    int total = 0;
    for ( int s = 0; s < vsp::GetTotalNumSurfs( geom_id ); s++ )
    {
        vector< double > u, w;
        vsp::GetUWTess01( geom_id, s, u, w );
        vector< double > us, ws;
        for ( double uu : u )
        {
            for ( double ww : w )
            {
                us.push_back( uu );
                ws.push_back( ww );
            }
        }
        total += ( int ) vsp::CompVecPnt01( geom_id, s, us, ws ).size();
    }
    return total;
}

void ReportErrors( const char *where )
{
    int n = vsp::ErrorMgrSingleton::getInstance().GetNumTotalErrors();
    if ( n )
    {
        printf( "  (%d API errors after %s)\n", n, where );
        while ( vsp::ErrorMgrSingleton::getInstance().GetNumTotalErrors() )
        {
            printf( "    %s\n", vsp::ErrorMgrSingleton::getInstance().PopLastError().GetErrorString().c_str() );
        }
    }
}

} // namespace

int main()
{
    auto t0 = std::chrono::steady_clock::now();
    vsp::VSPCheckSetup();
    printf( "init: %.0f ms (%s)\n", Ms( t0 ), vsp::GetVSPVersion().c_str() );
    ReportErrors( "init" );

    t0 = std::chrono::steady_clock::now();
    string pod = vsp::AddGeom( "POD" );
    string wing = vsp::AddGeom( "WING" );
    vsp::SetParmVal( wing, "TotalSpan", "WingGeom", 30.0 );
    vsp::Update();
    printf( "add pod + wing, update: %.0f ms\n", Ms( t0 ) );
    ReportErrors( "build" );

    t0 = std::chrono::steady_clock::now();
    int pts = CountTessPoints( pod ) + CountTessPoints( wing );
    printf( "tessellate: %d points in %.0f ms\n", pts, Ms( t0 ) );

    t0 = std::chrono::steady_clock::now();
    vsp::WriteVSPFile( "/model.vsp3" );
    vsp::ClearVSPModel();
    vsp::ReadVSPFile( "/model.vsp3" );
    vsp::Update();
    vector< string > geoms = vsp::FindGeoms();
    printf( "save, clear, reload: %d geoms in %.0f ms\n", ( int ) geoms.size(), Ms( t0 ) );
    ReportErrors( "file round trip" );

    string wing2 = vsp::FindGeomsWithName( "WingGeom" ).empty() ? "" : vsp::FindGeomsWithName( "WingGeom" )[0];
    if ( !wing2.empty() )
    {
        printf( "reloaded wing span: %.2f\n", vsp::GetParmVal( wing2, "TotalSpan", "WingGeom" ) );
    }

    // Scripting: an Advanced Link compiles and runs AngelScript, calling registered functions.
    string pod2 = vsp::FindGeomsWithName( "PodGeom" ).empty() ? "" : vsp::FindGeomsWithName( "PodGeom" )[0];
    if ( !wing2.empty() && !pod2.empty() )
    {
        vsp::AddAdvLink( "half_span" );
        vsp::AddAdvLinkInput( 0, vsp::FindParm( wing2, "TotalSpan", "WingGeom" ), "span" );
        vsp::AddAdvLinkOutput( 0, vsp::FindParm( pod2, "Length", "Design" ), "len" );
        vsp::SetAdvLinkCode( 0, "len = span * 0.5 + 0.0 * GetVSPVersion().length();" );
        bool built = vsp::BuildAdvLinkScript( 0 );
        vsp::SetParmValUpdate( vsp::FindParm( wing2, "TotalSpan", "WingGeom" ), 24.0 );
        vsp::Update();
        printf( "advanced link: built=%d, pod length after span=24: %.2f (expect 12)\n", built,
                vsp::GetParmVal( pod2, "Length", "Design" ) );
        ReportErrors( "advanced link" );
    }
    printf( "done\n" );
    return 0;
}

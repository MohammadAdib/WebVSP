// In-browser OpenVSP engine: the OpenVSP core (and, in the GUI build, its FLTK GUI mirrored
// to the page) compiled to WebAssembly.
//
// This is the WebAssembly counterpart of the local Python engine (web/backend/vspweb):
//   - OpenVSP runs on its own thread (main() runs on a pthread: -sPROXY_TO_PTHREAD), which owns
//     the model and the GUI, like runtime.py's OpenVSP thread.
//   - The page calls in from the browser's main thread through a few thread-safe functions:
//       call( id, op, a, b, value )   queue a request (the HTTP API's operations, below)
//       mirrorEvent( json )           input for the mirrored GUI (WebMirror's event JSON)
//       take()                        everything produced since the last take(), as JSON:
//                                       {"out":[results and model events],"mirror":[GUI output]}
//       takeBinary( id )              a request's binary result (meshes), as a Uint8Array
//   - Between requests the engine pumps the GUI and watches the model for changes made in the
//     GUI, emitting the same "model_changed" events as session.py.
//
// Requests (op, a, b, value) and their results mirror web/backend/vspweb/app.py:
//   model, geomTypes, bbox              JSON
//   mesh(a=geom id)                     binary "VSPM" mesh (see tess.py)
//   addGeom(a=type, b=parent)           {"id":...}
//   deleteGeoms(a=comma-separated ids)
//   setParm(a=parm id, value)
//   newModel, readFile(a=path), writeFile(a=path)   (paths in the in-memory file system)
//   pick(a=geom id, b="1" to toggle)    select as if picked in the 3D view (shift/ctrl-click)
//   origin(a=geom id)                   [x,y,z] of the geom's origin
//   move(a=ids, b="dx,dy,dz")           move geoms by an offset (one undo step)

#include "VSP_Geom_API.h"
#include "APIErrorMgr.h"
#include "Vec3d.h"
#include "WebProcess.h"

#include <sys/stat.h>

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using std::string;
using std::vector;

namespace
{

//==== Minimal JSON writing ====//

string Quote( const string &s )
{
    string out = "\"";
    for ( unsigned char c : s )
    {
        switch ( c )
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ( c < 0x20 )
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
    }
    return out + "\"";
}

string Num( double v )
{
    if ( !std::isfinite( v ) )
    {
        return "null";
    }
    char buf[32];
    snprintf( buf, sizeof( buf ), "%.17g", v );
    return buf;
}

string StrArray( const vector< string > &v )
{
    string out = "[";
    for ( size_t i = 0; i < v.size(); i++ )
    {
        out += ( i ? "," : "" ) + Quote( v[i] );
    }
    return out + "]";
}

// API errors raised since the last call, as one message (empty if none).
string TakeErrors()
{
    string msg;
    vsp::ErrorMgrSingleton &mgr = vsp::ErrorMgrSingleton::getInstance();
    while ( mgr.GetNumTotalErrors() )
    {
        string e = mgr.PopLastError().GetErrorString();
        msg = msg.empty() ? e : e + "; " + msg;
    }
    return msg;
}

//==== Shared state (browser main thread <-> engine thread) ====//

struct Job
{
    int id;
    string op, a, b;
    double value;
};

std::mutex g_Mutex;
std::deque< Job > g_Jobs;
vector< string > g_Out;                              // JSON messages for take()
std::map< int, vector< uint8_t > > g_Binary;         // results for takeBinary()

void Emit( const string &json )
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    g_Out.push_back( json );
}

//==== Model ====//

string ModelJson()
{
    string geoms = "[";
    vector< string > ids = vsp::FindGeoms();
    for ( size_t i = 0; i < ids.size(); i++ )
    {
        const string &id = ids[i];
        string parent = vsp::GetGeomParent( id );
        vec3d color = vsp::GetGeomWireColor( id );
        geoms += i ? "," : "";
        geoms += "{\"id\":" + Quote( id ) +
                 ",\"name\":" + Quote( vsp::GetGeomName( id ) ) +
                 ",\"type\":" + Quote( vsp::GetGeomTypeName( id ) ) +
                 ",\"parent\":" + ( parent.empty() || parent == "NONE" ? string( "null" ) : Quote( parent ) ) +
                 ",\"children\":" + StrArray( vsp::GetGeomChildren( id ) ) +
                 ",\"shown\":" + ( vsp::GetSetFlag( id, vsp::SET_SHOWN ) ? "true" : "false" ) +
                 ",\"drawType\":" + std::to_string( vsp::GetGeomDrawType( id ) ) +
                 ",\"color\":[" + Num( color.x() / 255.0 ) + "," + Num( color.y() / 255.0 ) + "," +
                 Num( color.z() / 255.0 ) + "]" +
                 ",\"numXSecSurfs\":" + std::to_string( vsp::GetNumXSecSurfs( id ) ) + "}";
    }
    geoms += "]";
    string file = vsp::GetVSPFileName();
    string out = "{\"geoms\":" + geoms +
                 ",\"active\":" + StrArray( vsp::GetActiveGeoms() ) +
                 ",\"vehicleId\":" + Quote( vsp::GetVehicleID() ) +
                 ",\"fileName\":" + Quote( file ) +
                 ",\"version\":" + Quote( vsp::GetVSPVersion() ) + "}";
    TakeErrors();
    return out;
}

// Geoms drawn from triangles rather than surfaces (GetMeshGeomTriPnts): imported/CompGeom
// meshes, and NGon meshes such as VSPAERO's geometry.
bool IsTriMesh( const string &type )
{
    return type == "Mesh" || type == "NGonMesh";
}

void Grow( double lo[3], double hi[3], double x, double y, double z )
{
    const double p[3] = { x, y, z };
    for ( int k = 0; k < 3; k++ )
    {
        lo[k] = std::min( lo[k], p[k] );
        hi[k] = std::max( hi[k], p[k] );
    }
}

string BBoxJson()
{
    double lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY };
    bool any = false;
    for ( const string &id : vsp::FindGeoms() )
    {
        if ( !vsp::GetSetFlag( id, vsp::SET_SHOWN ) )
        {
            continue;
        }
        int nsurf = vsp::GetTotalNumSurfs( id );
        for ( int s = 0; s < nsurf; s++ )
        {
            vec3d a = vsp::GetGeomBBoxMin( id, s, true ), b = vsp::GetGeomBBoxMax( id, s, true );
            Grow( lo, hi, a.x(), a.y(), a.z() );
            Grow( lo, hi, b.x(), b.y(), b.z() );
            any = true;
        }
        if ( nsurf == 0 && IsTriMesh( vsp::GetGeomTypeName( id ) ) )
        {
            vector< double > p = vsp::GetMeshGeomTriPnts( id );
            for ( size_t i = 0; i + 2 < p.size(); i += 3 )
            {
                Grow( lo, hi, p[i], p[i + 1], p[i + 2] );
                any = true;
            }
        }
    }
    TakeErrors();
    if ( !any )
    {
        return "null";
    }
    return "{\"min\":[" + Num( lo[0] ) + "," + Num( lo[1] ) + "," + Num( lo[2] ) + "],\"max\":[" +
           Num( hi[0] ) + "," + Num( hi[1] ) + "," + Num( hi[2] ) + "]}";
}

//==== Meshes (port of web/backend/vspweb/tess.py) ====//

struct Surface
{
    uint32_t nu = 0, nw = 0; // nw = 0: triangle list of nu vertices
    vector< float > pos, nrm;
    vector< uint32_t > idx;
};

bool GridSurface( const string &id, int surf, Surface &out )
{
    vector< double > u, w;
    vsp::GetUWTess01( id, surf, u, w );
    if ( u.size() < 2 || w.size() < 2 )
    {
        return false;
    }
    vector< double > us, ws;
    us.reserve( u.size() * w.size() );
    ws.reserve( u.size() * w.size() );
    for ( double uu : u )
    {
        for ( double ww : w )
        {
            us.push_back( uu );
            ws.push_back( ww );
        }
    }
    vector< vec3d > p = vsp::CompVecPnt01( id, surf, us, ws );
    vector< vec3d > n = vsp::CompVecNorm01( id, surf, us, ws );
    out.nu = ( uint32_t ) u.size();
    out.nw = ( uint32_t ) w.size();
    for ( size_t k = 0; k < p.size(); k++ )
    {
        out.pos.insert( out.pos.end(), { ( float ) p[k].x(), ( float ) p[k].y(), ( float ) p[k].z() } );
        out.nrm.insert( out.nrm.end(), { ( float ) n[k].x(), ( float ) n[k].y(), ( float ) n[k].z() } );
    }
    // Two triangles per grid cell; winding made to agree with the normals overall
    // (symmetric copies are mirrored).
    double agree = 0;
    for ( uint32_t i = 0; i + 1 < out.nu; i++ )
    {
        for ( uint32_t j = 0; j + 1 < out.nw; j++ )
        {
            uint32_t a = i * out.nw + j, b = a + out.nw, c = b + 1, d = a + 1;
            out.idx.insert( out.idx.end(), { a, b, c, a, c, d } );
            vec3d fn = cross( p[b] - p[a], p[c] - p[a] );
            agree += dot( fn, n[a] + n[b] + n[c] );
        }
    }
    if ( agree < 0 )
    {
        for ( size_t t = 0; t + 2 < out.idx.size(); t += 3 )
        {
            std::swap( out.idx[t + 1], out.idx[t + 2] );
        }
    }
    return true;
}

bool TriangleSurface( const string &id, Surface &out )
{
    vector< double > p = vsp::GetMeshGeomTriPnts( id );
    if ( p.size() < 9 )
    {
        return false;
    }
    size_t nv = p.size() / 3;
    out.nu = ( uint32_t ) nv;
    out.nw = 0;
    out.pos.assign( p.begin(), p.end() );
    out.nrm.resize( p.size() );
    out.idx.resize( nv );
    for ( size_t t = 0; t + 2 < nv; t += 3 )
    {
        vec3d a( p[3 * t], p[3 * t + 1], p[3 * t + 2] );
        vec3d b( p[3 * t + 3], p[3 * t + 4], p[3 * t + 5] );
        vec3d c( p[3 * t + 6], p[3 * t + 7], p[3 * t + 8] );
        vec3d fn = cross( b - a, c - a );
        double len = fn.mag();
        if ( len > 0 )
        {
            fn = fn / len;
        }
        for ( size_t v = 0; v < 3; v++ )
        {
            out.nrm[3 * ( t + v )] = ( float ) fn.x();
            out.nrm[3 * ( t + v ) + 1] = ( float ) fn.y();
            out.nrm[3 * ( t + v ) + 2] = ( float ) fn.z();
        }
    }
    for ( size_t i = 0; i < nv; i++ )
    {
        out.idx[i] = ( uint32_t ) i;
    }
    return true;
}

void Put32( vector< uint8_t > &b, uint32_t v )
{
    uint8_t bytes[4];
    memcpy( bytes, &v, 4 ); // wasm is little-endian, as the format is
    b.insert( b.end(), bytes, bytes + 4 );
}

template < class T >
void PutVec( vector< uint8_t > &b, const vector< T > &v )
{
    const uint8_t *p = reinterpret_cast< const uint8_t * >( v.data() );
    b.insert( b.end(), p, p + v.size() * sizeof( T ) );
}

vector< uint8_t > MeshBytes( const string &id )
{
    vector< Surface > surfs;
    // The viewer can ask for a Geom that has just been deleted: an empty mesh, quietly.
    vector< string > geoms = vsp::FindGeoms();
    int nsurf = std::find( geoms.begin(), geoms.end(), id ) == geoms.end() ? 0 : vsp::GetTotalNumSurfs( id );
    for ( int s = 0; s < nsurf; s++ )
    {
        Surface sf;
        if ( GridSurface( id, s, sf ) )
        {
            surfs.push_back( std::move( sf ) );
        }
    }
    if ( surfs.empty() && nsurf == 0 && std::find( geoms.begin(), geoms.end(), id ) != geoms.end() &&
         IsTriMesh( vsp::GetGeomTypeName( id ) ) )
    {
        Surface sf;
        if ( TriangleSurface( id, sf ) )
        {
            surfs.push_back( std::move( sf ) );
        }
    }
    TakeErrors();

    vector< uint8_t > b = { 'V', 'S', 'P', 'M' };
    Put32( b, ( uint32_t ) surfs.size() );
    for ( const Surface &sf : surfs )
    {
        Put32( b, sf.nu );
        Put32( b, sf.nw );
        Put32( b, ( uint32_t ) sf.idx.size() );
        PutVec( b, sf.pos );
        PutVec( b, sf.nrm );
        PutVec( b, sf.idx );
    }
    // Then the outline (feature lines), as segments: count of points, then x,y,z floats.
    vector< float > feat;
    if ( nsurf > 0 )
    {
        for ( const vec3d &p : vsp::MirrorFeatureLines( id ) )
        {
            feat.insert( feat.end(), { ( float ) p.x(), ( float ) p.y(), ( float ) p.z() } );
        }
        TakeErrors();
    }
    Put32( b, ( uint32_t ) ( feat.size() / 3 ) );
    PutVec( b, feat );
    return b;
}

//==== Change detection (port of session.py's watcher) ====//

int g_Rev = 0;
bool g_Dirty = false;
string g_TreeSig;
std::unordered_map< string, size_t > g_Fingerprints;

// What the Geom Browser shows: changes here re-send the tree.
string TreeSignature()
{
    string sig;
    for ( const string &g : vsp::FindGeoms() )
    {
        sig += g + '\x1f' + vsp::GetGeomName( g ) + '\x1f' + vsp::GetGeomParent( g ) + '\x1f' +
               ( vsp::GetSetFlag( g, vsp::SET_SHOWN ) ? "1" : "0" ) + std::to_string( vsp::GetGeomDrawType( g ) ) + '\x1e';
    }
    for ( const string &g : vsp::GetActiveGeoms() )
    {
        sig += g + ',';
    }
    return sig + vsp::GetVSPFileName();
}

// Everything that shapes a Geom's surfaces: its Parms, its XSecs' Parms, and its parent's.
size_t Fingerprint( const string &gid, std::unordered_map< string, size_t > &cache )
{
    auto hit = cache.find( gid );
    if ( hit != cache.end() )
    {
        return hit->second;
    }
    std::hash< string > hs;
    std::hash< double > hd;
    size_t h = hs( gid );
    auto mix = [&h]( size_t v ) { h ^= v + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 ); };
    for ( const string &pid : vsp::GetGeomParmIDs( gid ) )
    {
        mix( hs( pid ) );
        mix( hd( vsp::GetParmVal( pid ) ) );
    }
    for ( const string &surf : vsp::GetXSecSurfIDVec( gid ) )
    {
        mix( hs( surf ) );
        for ( int xi = 0; xi < vsp::GetNumXSec( surf ); xi++ )
        {
            string xs = vsp::GetXSec( surf, xi );
            mix( hs( xs ) );
            for ( const string &pid : vsp::GetXSecParmIDs( xs ) )
            {
                mix( hs( pid ) );
                mix( hd( vsp::GetParmVal( pid ) ) );
            }
        }
    }
    string parent = vsp::GetGeomParent( gid );
    if ( !parent.empty() && parent != "NONE" )
    {
        // Children move with their parent.
        mix( Fingerprint( parent, cache ) );
    }
    cache[gid] = h;
    return h;
}

void RefreshFingerprints( vector< string > &changed, vector< string > &removed )
{
    std::unordered_map< string, size_t > cache;
    vector< string > current = vsp::FindGeoms();
    for ( const string &g : current )
    {
        size_t fp = Fingerprint( g, cache );
        auto it = g_Fingerprints.find( g );
        if ( it == g_Fingerprints.end() || it->second != fp )
        {
            changed.push_back( g );
        }
    }
    for ( const auto &kv : g_Fingerprints )
    {
        if ( !cache.count( kv.first ) )
        {
            removed.push_back( kv.first );
        }
    }
    g_Fingerprints.clear();
    for ( const string &g : current )
    {
        g_Fingerprints[g] = cache[g];
    }
    TakeErrors();
}

const char *NO_UNDO = "{\"canUndo\":false,\"canRedo\":false,\"undoLabel\":null,\"redoLabel\":null}";

void EmitModelChanged( bool tree, const vector< string > &changed, const vector< string > &removed, const string &label )
{
    g_Rev++;
    Emit( "{\"type\":\"model_changed\",\"rev\":" + std::to_string( g_Rev ) +
          ",\"tree\":" + ( tree || !removed.empty() ? "true" : "false" ) +
          ",\"changed\":" + StrArray( changed ) + ",\"removed\":" + StrArray( removed ) +
          ",\"label\":" + ( label.empty() ? string( "null" ) : Quote( label ) ) +
          ",\"undo\":" + NO_UNDO + "}" );
}

// Every ~150 ms, and right after each edit: report what the GUI or a request changed.
void Watch( const string &label = "", bool force = false )
{
    bool updated = vsp::GetAndResetUpdateCount() > 0;
    string sig = TreeSignature();
    bool tree_changed = sig != g_TreeSig;
    if ( !updated && !tree_changed && !force )
    {
        return;
    }
    g_TreeSig = sig;
    vector< string > changed, removed;
    RefreshFingerprints( changed, removed );
    if ( !changed.empty() || !removed.empty() )
    {
        g_Dirty = true;
    }
    if ( !changed.empty() || !removed.empty() || tree_changed || force )
    {
        EmitModelChanged( tree_changed, changed, removed, label );
    }
}

//==== Requests ====//

void Result( int id, const string &error, const string &value_json, bool binary = false )
{
    if ( !error.empty() )
    {
        Emit( "{\"t\":\"result\",\"id\":" + std::to_string( id ) + ",\"ok\":false,\"error\":" + Quote( error ) + "}" );
    }
    else
    {
        Emit( "{\"t\":\"result\",\"id\":" + std::to_string( id ) + ",\"ok\":true" +
              ( binary ? ",\"bin\":true" : ",\"value\":" + ( value_json.empty() ? string( "null" ) : value_json ) ) + "}" );
    }
}

vector< string > SplitCsv( const string &s )
{
    vector< string > out;
    std::stringstream ss( s );
    string item;
    while ( std::getline( ss, item, ',' ) )
    {
        if ( !item.empty() )
        {
            out.push_back( item );
        }
    }
    return out;
}

void Run( const Job &j )
{
    TakeErrors();
    if ( j.op == "model" )
    {
        string m = ModelJson();
        // Add the engine's own bookkeeping, as session.tree() does.
        m.pop_back();
        m += ",\"rev\":" + std::to_string( g_Rev ) + ",\"dirty\":" + ( g_Dirty ? "true" : "false" ) +
             ",\"undo\":" + NO_UNDO + "}";
        return Result( j.id, "", m );
    }
    if ( j.op == "geomTypes" )
    {
        return Result( j.id, "", StrArray( vsp::GetGeomTypes() ) );
    }
    if ( j.op == "bbox" )
    {
        return Result( j.id, "", BBoxJson() );
    }
    if ( j.op == "mesh" )
    {
        vector< uint8_t > b = MeshBytes( j.a );
        {
            std::lock_guard< std::mutex > lock( g_Mutex );
            g_Binary[j.id] = std::move( b );
        }
        return Result( j.id, "", "", true );
    }
    if ( j.op == "addGeom" )
    {
        string id = vsp::AddGeom( j.a, j.b );
        vsp::Update();
        string err = TakeErrors();
        Watch( "Add " + j.a, true );
        return Result( j.id, err, "{\"id\":" + Quote( id ) + "}" );
    }
    if ( j.op == "deleteGeoms" )
    {
        vsp::DeleteGeomVec( SplitCsv( j.a ) );
        vsp::Update();
        string err = TakeErrors();
        Watch( "Delete", true );
        return Result( j.id, err, "{\"ok\":true}" );
    }
    if ( j.op == "setParm" )
    {
        vsp::SetParmValUpdate( j.a, j.value );
        string err = TakeErrors();
        Watch( "Parameter change", true );
        return Result( j.id, err, "{\"ok\":true}" );
    }
    if ( j.op == "move" )
    {
        vector< string > c = SplitCsv( j.b );
        vec3d d( c.size() > 0 ? atof( c[0].c_str() ) : 0, c.size() > 1 ? atof( c[1].c_str() ) : 0,
                 c.size() > 2 ? atof( c[2].c_str() ) : 0 );
        vsp::MirrorMoveGeoms( SplitCsv( j.a ), d );
        string err = TakeErrors();
        Watch( "Move", true );
        return Result( j.id, err, "{\"ok\":true}" );
    }
    if ( j.op == "origin" )
    {
        vec3d p = vsp::MirrorGeomOrigin( j.a );
        string err = TakeErrors();
        char buf[128];
        snprintf( buf, sizeof( buf ), "[%.17g,%.17g,%.17g]", p.x(), p.y(), p.z() );
        return Result( j.id, err, buf );
    }
    if ( j.op == "newModel" )
    {
        vsp::ClearVSPModel();
        vsp::Update();
        string err = TakeErrors();
        g_Dirty = false;
        Watch( "New", true );
        g_Dirty = false;
        return Result( j.id, err, "{\"ok\":true}" );
    }
    if ( j.op == "readFile" )
    {
        vsp::ClearVSPModel();
        vsp::ReadVSPFile( j.a );
        vsp::Update();
        string err = TakeErrors();
        Watch( "Open", true );
        g_Dirty = false;
        return Result( j.id, err, "{\"ok\":true}" );
    }
    if ( j.op == "writeFile" )
    {
        vsp::SetVSP3FileName( j.a );
        vsp::WriteVSPFile( j.a );
        string err = TakeErrors();
        g_Dirty = false;
        Emit( "{\"type\":\"saved\",\"fileName\":" + Quote( j.a ) + "}" );
        return Result( j.id, err, "{\"path\":" + Quote( j.a ) + "}" );
    }
    if ( j.op == "pick" )
    {
        vsp::MirrorPickGeom( j.a, j.b == "1" );
        return Result( j.id, TakeErrors(), "{\"ok\":true}" );
    }
    Result( j.id, "unknown request: " + j.op, "" );
}

bool g_Gui = false;

} // namespace

//==== Engine thread ====//

int main()
{
    vsp::VSPCheckSetup();
    // Errors are collected per request (TakeErrors) and returned to the page, not printed.
    vsp::ErrorMgrSingleton::getInstance().SilenceErrors();
    // VSPAERO's programs, compiled to WebAssembly, are run by the page (WebProcess.h):
    // OpenVSP only checks that they exist, so give it a folder with placeholders.
    mkdir( "/bin", 0755 );
    for ( const char *exe : { "/bin/vspaero", "/bin/vsploads", "/bin/vspviewer" } )
    {
        if ( FILE *f = fopen( exe, "w" ) )
        {
            fputs( "run by the web page\n", f );
            fclose( f );
        }
    }
    vsp::SetVSPAEROPath( "/bin" );
#ifdef VSP_WASM_GUI
    vsp::InitGUI();
    vsp::MirrorEnable();
    g_Gui = true;
#endif
    TakeErrors();
    g_TreeSig = TreeSignature();
    vector< string > changed, removed;
    RefreshFingerprints( changed, removed );
    Emit( "{\"type\":\"engine_ready\",\"gui\":" + string( g_Gui ? "true" : "false" ) + "}" );

    auto last_watch = std::chrono::steady_clock::now();
    while ( true )
    {
        // Requests first, then let the GUI process timers/events and produce its diff.
        while ( true )
        {
            Job j;
            {
                std::lock_guard< std::mutex > lock( g_Mutex );
                if ( g_Jobs.empty() )
                {
                    break;
                }
                j = std::move( g_Jobs.front() );
                g_Jobs.pop_front();
            }
            Run( j );
        }
        if ( g_Gui )
        {
            vsp::MirrorPump( 0.005 );
        }
        else
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        auto now = std::chrono::steady_clock::now();
        if ( now - last_watch > std::chrono::milliseconds( 150 ) )
        {
            last_watch = now;
            Watch();
        }
    }
    return 0;
}

//==== Called from the browser's main thread ====//

namespace
{

void Call( int id, const string &op, const string &a, const string &b, double value )
{
    std::lock_guard< std::mutex > lock( g_Mutex );
    g_Jobs.push_back( { id, op, a, b, value } );
}

void MirrorEvent( const string &json )
{
    vsp::MirrorPostEvent( json ); // thread-safe: queued for the engine thread
}

string Take()
{
    string out;
    {
        std::lock_guard< std::mutex > lock( g_Mutex );
        out = "{\"out\":[";
        for ( size_t i = 0; i < g_Out.size(); i++ )
        {
            out += ( i ? "," : "" ) + g_Out[i];
        }
        g_Out.clear();
    }
    out += "],\"mirror\":" + vsp::MirrorTakeOutput() + ",\"proc\":" + WebProcess::TakeRequests() + "}";
    return out;
}

// A program's output and exit, from the page (see WebProcess.h).
void ProcessOutput( int id, const string &text )
{
    WebProcess::Output( id, text );
}

void ProcessExit( int id, int code )
{
    WebProcess::Exit( id, code );
}

emscripten::val TakeBinary( int id )
{
    vector< uint8_t > b;
    {
        std::lock_guard< std::mutex > lock( g_Mutex );
        auto it = g_Binary.find( id );
        if ( it == g_Binary.end() )
        {
            return emscripten::val::null();
        }
        b = std::move( it->second );
        g_Binary.erase( it );
    }
    // Copied into an ordinary (non-shared) buffer owned by JS.
    return emscripten::val::global( "Uint8Array" ).new_( emscripten::typed_memory_view( b.size(), b.data() ) );
}

void RequestFull()
{
    vsp::MirrorRequestFull();
}

} // namespace

EMSCRIPTEN_BINDINGS( openvsp )
{
    emscripten::function( "call", &Call );
    emscripten::function( "mirrorEvent", &MirrorEvent );
    emscripten::function( "take", &Take );
    emscripten::function( "takeBinary", &TakeBinary );
    emscripten::function( "requestFull", &RequestFull );
    emscripten::function( "processOutput", &ProcessOutput );
    emscripten::function( "processExit", &ProcessExit );
}

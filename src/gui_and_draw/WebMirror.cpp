//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// WebMirror: see WebMirror.h.
//
// Output messages (JSON objects, batched into an array by TakeOutput):
//   {"t":"diff","full":b,"windows":[win...],"closed":[wid...],"widgets":[w...],"removed":[id...]}
//     win:  {"id","role","screen","title","w","h"}        role: main|browser|geom|screen|dialog
//     w:    {"id","win","p","o","k",...}                   p: parent id (0 = window), o: child index,
//           k: kind, plus kind-specific fields (see SerializeWidget).
//   {"t":"dialog","seq":n,"kind":"choice|alert|message|file",...}
//
// Input events (flat JSON objects):
//   {"op":"set","id":n,"v":"..."}            set a value and fire the widget's callback
//   {"op":"click","id":n}                   press a button
//   {"op":"pick","id":n,"i":n}              choose item i of a menu bar / menu button / choice
//   {"op":"browser","id":n,"line":n,"clicks":n,"mods":n}
//   {"op":"check","id":n,"line":n,"v":0|1}  check browser item
//   {"op":"mouse","win":n,"kind":"push|drag|release|move|wheel","x":n,"y":n,"b":n,"mods":n,"clicks":n,"dy":n}
//   {"op":"key","win":n,"key":n,"text":"...","mods":n}
//   {"op":"text","id":n,"v":"..."}          replace a text editor's buffer
//   {"op":"tab","id":n,"child":n}           select a tab
//   {"op":"close","win":n}                  close a window as its close box would
//   {"op":"resize","win":n,"w":n,"h":n}     resize a window (FLTK re-lays it out)
//   {"op":"tree_icon","id":n,"item":n,"icon":1|2}  geom browser show / surface icon
//   {"op":"dialog","seq":n,"result":n,"path":"..."}
//   {"op":"full"}
//
//////////////////////////////////////////////////////////////////////

#include "WebMirror.h"
#include "CrashTrace.h"
#include "MirrorGL.h"

#include "ScreenMgr.h"
#include "ScreenBase.h"
#include "ManageGeomScreen.h"
#include "MainVSPScreen.h"
#include "GuiDevice.h"
#include "TreeIconWidget.h"
#include "ParmMgr.h"
#include "StringUtil.h"
#include "MainGLWindow.h"
#include "SubGLWindow.h"
#include "DrawObj.h"

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Tree.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Check_Browser.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Table.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Counter.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/Fl_Slider.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Roller.H>
#include <FL/Fl_Dial.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Input_.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Light_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Repeat_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/fl_draw.H>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

using std::string;
using std::vector;

namespace
{

//==== Minimal JSON writing ====//
void AppendEscaped( string &out, const char *s )
{
    out += '"';
    if ( s )
    {
        for ( const unsigned char *p = ( const unsigned char * ) s; *p; p++ )
        {
            unsigned char c = *p;
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
    }
    out += '"';
}

class Obj
{
public:
    Obj()
    {
        m_S = "{";
    }
    Obj &Str( const char *k, const char *v )
    {
        Key( k );
        AppendEscaped( m_S, v );
        return *this;
    }
    Obj &Str( const char *k, const string &v )
    {
        return Str( k, v.c_str() );
    }
    Obj &Int( const char *k, long long v )
    {
        Key( k );
        m_S += std::to_string( v );
        return *this;
    }
    Obj &Num( const char *k, double v )
    {
        Key( k );
        char buf[32];
        snprintf( buf, sizeof( buf ), "%.10g", v );
        m_S += buf;
        return *this;
    }
    Obj &Bool( const char *k, bool v )
    {
        Key( k );
        m_S += v ? "true" : "false";
        return *this;
    }
    Obj &Raw( const char *k, const string &json )
    {
        Key( k );
        m_S += json;
        return *this;
    }
    string Done()
    {
        return m_S + "}";
    }
private:
    void Key( const char *k )
    {
        if ( m_S.size() > 1 )
        {
            m_S += ',';
        }
        AppendEscaped( m_S, k );
        m_S += ':';
    }
    string m_S;
};

string JoinArray( const vector< string > &items )
{
    string s = "[";
    for ( size_t i = 0; i < items.size(); i++ )
    {
        if ( i )
        {
            s += ',';
        }
        s += items[i];
    }
    return s + "]";
}

//==== Minimal parsing of flat JSON objects (string / number values) ====//
std::map< string, string > ParseFlat( const string &json )
{
    std::map< string, string > out;
    size_t i = 0, n = json.size();
    auto skip = [&]() { while ( i < n && ( json[i] == ' ' || json[i] == '\n' || json[i] == '\t' || json[i] == '\r' || json[i] == ',' || json[i] == '{' || json[i] == '}' ) ) i++; };
    auto read_string = [&]() -> string
    {
        string s;
        i++; // opening quote
        while ( i < n && json[i] != '"' )
        {
            char c = json[i++];
            if ( c == '\\' && i < n )
            {
                char e = json[i++];
                switch ( e )
                {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'u':
                    {
                        unsigned int cp = ( unsigned int ) strtoul( json.substr( i, 4 ).c_str(), nullptr, 16 );
                        i += 4;
                        if ( cp < 0x80 ) s += ( char ) cp;
                        else if ( cp < 0x800 ) { s += ( char )( 0xC0 | ( cp >> 6 ) ); s += ( char )( 0x80 | ( cp & 0x3F ) ); }
                        else { s += ( char )( 0xE0 | ( cp >> 12 ) ); s += ( char )( 0x80 | ( ( cp >> 6 ) & 0x3F ) ); s += ( char )( 0x80 | ( cp & 0x3F ) ); }
                        break;
                    }
                    default: s += e;
                }
            }
            else
            {
                s += c;
            }
        }
        i++; // closing quote
        return s;
    };
    while ( true )
    {
        skip();
        if ( i >= n || json[i] != '"' )
        {
            break;
        }
        string key = read_string();
        while ( i < n && ( json[i] == ' ' || json[i] == ':' ) ) i++;
        if ( i >= n )
        {
            break;
        }
        if ( json[i] == '"' )
        {
            out[key] = read_string();
        }
        else
        {
            size_t start = i;
            while ( i < n && json[i] != ',' && json[i] != '}' ) i++;
            string v = json.substr( start, i - start );
            while ( !v.empty() && v.back() == ' ' ) v.pop_back();
            out[key] = v;
        }
    }
    return out;
}

int GetInt( const std::map< string, string > &m, const char *k, int def = 0 )
{
    auto it = m.find( k );
    if ( it == m.end() || it->second.empty() )
    {
        return def;
    }
    if ( it->second == "true" ) return 1;
    if ( it->second == "false" ) return 0;
    return atoi( it->second.c_str() );
}

string GetStr( const std::map< string, string > &m, const char *k )
{
    auto it = m.find( k );
    return it == m.end() ? string() : it->second;
}

//==== Pixels of custom-drawn widgets (GL views, plots, tables) ====//
string Base64( const unsigned char *data, size_t n )
{
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    out.reserve( ( n + 2 ) / 3 * 4 );
    size_t i = 0;
    for ( ; i + 2 < n; i += 3 )
    {
        unsigned v = ( data[i] << 16 ) | ( data[i + 1] << 8 ) | data[i + 2];
        out += tbl[( v >> 18 ) & 63];
        out += tbl[( v >> 12 ) & 63];
        out += tbl[( v >> 6 ) & 63];
        out += tbl[v & 63];
    }
    if ( i < n )
    {
        unsigned v = data[i] << 16;
        if ( i + 1 < n ) v |= data[i + 1] << 8;
        out += tbl[( v >> 18 ) & 63];
        out += tbl[( v >> 12 ) & 63];
        out += i + 1 < n ? tbl[( v >> 6 ) & 63] : '=';
        out += '=';
    }
    return out;
}

struct Capture
{
    string b64;         // tightly packed RGB rows
    int w = 0, h = 0;
    std::chrono::steady_clock::time_point when;
};

std::unordered_map< Fl_Widget*, Capture > &Captures()
{
    static std::unordered_map< Fl_Widget*, Capture > c;
    return c;
}

// Capture a widget's pixels at most every 250 ms (or at once when its size changed).
const char *KindOf( Fl_Widget *w );

const Capture *CaptureWidget( Fl_Widget *w )
{
    if ( w->as_window() || w->w() <= 0 || w->h() <= 0 || !w->visible_r() )
    {
        return nullptr;
    }
    auto now = std::chrono::steady_clock::now();
    Capture &cap = Captures()[w];
    if ( !cap.b64.empty() && cap.w == w->w() && cap.h == w->h() && now - cap.when < std::chrono::milliseconds( 250 ) )
    {
        return &cap;
    }

    Fl_RGB_Image *img = nullptr;
    {
        Fl_Image_Surface surf( w->w(), w->h() );
        Fl_Surface_Device::push_current( &surf );
        fl_color( FL_WHITE );
        fl_rectf( 0, 0, w->w(), w->h() );
        surf.draw( w );
        img = surf.image();
        Fl_Surface_Device::pop_current();
    }
    if ( !img )
    {
        return nullptr;
    }
    int iw = img->data_w(), ih = img->data_h(), d = img->d();
    int ld = img->ld() ? img->ld() : iw * d;
    const unsigned char *src = ( const unsigned char * ) img->data()[0];
    vector< unsigned char > rgb( ( size_t ) iw * ih * 3 );
    for ( int y = 0; y < ih; y++ )
    {
        const unsigned char *row = src + ( size_t ) y * ld;
        for ( int x = 0; x < iw; x++ )
        {
            const unsigned char *px = row + ( size_t ) x * d;
            unsigned char *dst = &rgb[( ( size_t ) y * iw + x ) * 3];
            if ( d >= 3 )
            {
                dst[0] = px[0]; dst[1] = px[1]; dst[2] = px[2];
            }
            else
            {
                dst[0] = dst[1] = dst[2] = px[0];
            }
        }
    }
    delete img;
    cap.b64 = Base64( rgb.data(), rgb.size() );
    cap.w = iw;
    cap.h = ih;
    cap.when = now;
    return &cap;
}


// Lines and points a GL view draws, plus its 2D camera, so the client can draw them.
string GlDrawJson( Fl_Widget *w )
{
    vector< DrawObj* > objs;
    DrawObj::ScreenEnum screen;
    float zoom = 1, pan_x = 0, pan_y = 0;
    int plane = 0;
    if ( VSPGUI::VspGlWindow *gw = dynamic_cast< VSPGUI::VspGlWindow* >( w ) )
    {
        gw->LoadAllDrawObjs( objs );
        gw->GetMirrorView( zoom, pan_x, pan_y );
        screen = gw->GetLinkedScreen();
        plane = gw->GetMirrorViewPlane();
    }
    else if ( VSPGUI::VspSubGlWindow *sw = dynamic_cast< VSPGUI::VspSubGlWindow* >( w ) )
    {
        sw->LoadAllDrawObjs( objs );
        sw->GetMirrorView( zoom, pan_x, pan_y );
        screen = sw->GetLinkedScreen();
    }
    else
    {
        return string();
    }

    vector< string > items;
    for ( DrawObj *d : objs )
    {
        if ( !d || d->m_Screen != screen || !d->m_Visible || d->m_PntVec.empty() )
        {
            continue;
        }
        const char *type = nullptr;
        switch ( d->m_Type )
        {
            case DrawObj::VSP_LINES: type = "lines"; break;
            case DrawObj::VSP_LINE_STRIP: type = "strip"; break;
            case DrawObj::VSP_LINE_LOOP: type = "loop"; break;
            case DrawObj::VSP_POINTS: type = "points"; break;
            default: break;
        }
        if ( !type )
        {
            continue;
        }
        string pts = "[";
        char buf[64];
        for ( size_t i = 0; i < d->m_PntVec.size(); i++ )
        {
            const vec3d &v = d->m_PntVec[i];
            snprintf( buf, sizeof( buf ), "%s%.6g,%.6g,%.6g", i ? "," : "", v.x(), v.y(), v.z() );
            pts += buf;
        }
        pts += "]";
        auto col = []( const vec3d &c )
        {
            char cb[8];
            auto cl = []( double v ) { return ( int )( v < 0 ? 0 : v > 1 ? 255 : v * 255 + 0.5 ); };
            snprintf( cb, sizeof( cb ), "#%02x%02x%02x", cl( c.x() ), cl( c.y() ), cl( c.z() ) );
            return string( cb );
        };
        Obj o;
        o.Str( "t", type ).Str( "c", col( d->m_LineColor ) ).Str( "pc", col( d->m_PointColor ) )
         .Num( "lw", d->m_LineWidth ).Num( "ps", d->m_PointSize ).Raw( "p", pts );
        items.push_back( o.Done() );
    }
    Obj v;
    v.Num( "zoom", zoom ).Num( "px", pan_x ).Num( "py", pan_y ).Int( "plane", plane )
     .Num( "ppu", dynamic_cast< Fl_Gl_Window* >( w ) ? static_cast< Fl_Gl_Window* >( w )->pixels_per_unit() : 1.0 ).Raw( "objs", JoinArray( items ) );
    return v.Done();
}

string ColorHex( Fl_Color c )
{
    unsigned char r, g, b;
    Fl::get_color( c, r, g, b );
    char buf[8];
    snprintf( buf, sizeof( buf ), "#%02x%02x%02x", r, g, b );
    return buf;
}

//==== State ====//
struct WinInfo
{
    string role;
    string screen;
    int screen_index = -1;
};

struct WidgetRec
{
    int id = 0;
    int win = 0;
    string json;
    bool seen = false;
};

struct WinRec
{
    int id = 0;
    string json;
    bool seen = false;
};

struct State
{
    bool enabled = false;
    ScreenMgr *mgr = nullptr;
    int next_id = 1;
    std::unordered_map< Fl_Widget*, WidgetRec > widgets;
    std::unordered_map< Fl_Window*, WinRec > windows;
    std::unordered_map< int, Fl_Widget* > by_id;
    std::unordered_map< Fl_Window*, WinInfo > info;
    bool want_full = true;
    std::chrono::steady_clock::time_point last_diff;

    std::mutex io_mutex;                 // guards everything below
    std::deque< string > inbox;
    vector< string > outbox;
    int dialog_seq = 0;
    std::map< int, std::map< string, string > > dialog_replies;
    int modal_depth = 0;
};

State &S()
{
    static State s;
    return s;
}

void Emit( const string &msg )
{
    std::lock_guard< std::mutex > lock( S().io_mutex );
    S().outbox.push_back( msg );
}

int IdFor( Fl_Widget *w )
{
    State &s = S();
    auto it = s.widgets.find( w );
    if ( it != s.widgets.end() )
    {
        return it->second.id;
    }
    WidgetRec rec;
    rec.id = s.next_id++;
    s.widgets[w] = rec;
    s.by_id[rec.id] = w;
    return rec.id;
}

Fl_Widget *WidgetById( int id )
{
    auto it = S().by_id.find( id );
    if ( it == S().by_id.end() )
    {
        return nullptr;
    }
    // Only hand back widgets that are still part of a shown window. The stored pointer may
    // be stale (widget deleted since the last diff), so search the live trees by address
    // instead of dereferencing it.
    Fl_Widget *w = it->second;
    std::function< bool( Fl_Group* ) > contains = [&]( Fl_Group *g ) -> bool
    {
        for ( int i = 0; i < g->children(); i++ )
        {
            Fl_Widget *c = g->child( i );
            if ( c == w )
            {
                return true;
            }
            Fl_Group *cg = c->as_group();
            if ( cg && contains( cg ) )
            {
                return true;
            }
        }
        return false;
    };
    for ( Fl_Window *win = Fl::first_window(); win; win = Fl::next_window( win ) )
    {
        if ( w == win || contains( win ) )
        {
            return w;
        }
    }
    return nullptr;
}

Fl_Window *WindowById( int id )
{
    for ( auto &kv : S().windows )
    {
        if ( kv.second.id == id )
        {
            return kv.first;
        }
    }
    return nullptr;
}

//==== Widget kinds ====//
const char *KindOf( Fl_Widget *w )
{
    if ( dynamic_cast< Fl_Gl_Window* >( w ) ) return "gl";
    if ( dynamic_cast< Fl_Window* >( w ) ) return "subwindow";
    if ( dynamic_cast< Fl_Tabs* >( w ) ) return "tabs";
    if ( dynamic_cast< Fl_Scroll* >( w ) ) return "scroll";
    if ( dynamic_cast< Fl_Tree* >( w ) ) return "tree";
    if ( dynamic_cast< Fl_Check_Browser* >( w ) ) return "check_browser";
    if ( dynamic_cast< Fl_Browser* >( w ) ) return "browser";
    if ( dynamic_cast< Fl_Text_Editor* >( w ) ) return "text_editor";
    if ( dynamic_cast< Fl_Text_Display* >( w ) ) return "text_display";
    if ( dynamic_cast< Fl_Table* >( w ) ) return "table";
    if ( dynamic_cast< Vsp_Canvas* >( w ) ) return "canvas";
    if ( dynamic_cast< Fl_Input_Choice* >( w ) ) return "input_choice";
    if ( dynamic_cast< Fl_Menu_Bar* >( w ) ) return "menubar";
    if ( dynamic_cast< Fl_Choice* >( w ) ) return "choice";
    if ( dynamic_cast< Fl_Menu_Button* >( w ) ) return "menubutton";
    if ( dynamic_cast< Fl_Counter* >( w ) ) return "counter";
    if ( dynamic_cast< Fl_Value_Slider* >( w ) ) return "value_slider";
    if ( dynamic_cast< Fl_Scrollbar* >( w ) ) return "scrollbar";
    if ( dynamic_cast< Fl_Slider* >( w ) ) return "slider";
    if ( dynamic_cast< Fl_Roller* >( w ) ) return "roller";
    if ( dynamic_cast< Fl_Dial* >( w ) ) return "dial";
    if ( dynamic_cast< Fl_Progress* >( w ) ) return "progress";
    if ( dynamic_cast< Fl_Output* >( w ) ) return "output";
    if ( dynamic_cast< Fl_Input_* >( w ) ) return "input";
    if ( dynamic_cast< Fl_Check_Button* >( w ) ) return "check";
    if ( dynamic_cast< Fl_Round_Button* >( w ) ) return "round";
    if ( dynamic_cast< Fl_Light_Button* >( w ) ) return "light";
    if ( dynamic_cast< Fl_Repeat_Button* >( w ) ) return "repeat";
    if ( dynamic_cast< Fl_Button* >( w ) ) return "button";
    if ( dynamic_cast< Fl_Box* >( w ) ) return "box";
    if ( dynamic_cast< Fl_Group* >( w ) ) return "group";
    return "widget";
}

// Composite widgets whose children are serialized as part of the parent instead.
bool IsLeafComposite( Fl_Widget *w )
{
    return dynamic_cast< Fl_Browser_* >( w ) || dynamic_cast< Fl_Text_Display* >( w ) ||
           dynamic_cast< Fl_Input_Choice* >( w ) || dynamic_cast< Fl_Table* >( w );
}

string MenuJson( const Fl_Menu_ *m )
{
    // Flat list of items with their depth; i is the index into the menu() array (for "pick").
    vector< string > items;
    const Fl_Menu_Item *arr = m->menu();
    int n = m->size();
    int depth = 0;
    for ( int i = 0; arr && i < n; i++ )
    {
        const Fl_Menu_Item &it = arr[i];
        if ( !it.text )
        {
            if ( depth > 0 )
            {
                depth--;
            }
            continue;
        }
        Obj o;
        o.Int( "i", i ).Str( "label", it.text ).Int( "d", depth ).Int( "flags", it.flags ).Int( "sc", it.shortcut_ );
        items.push_back( o.Done() );
        if ( it.flags & FL_SUBMENU )
        {
            depth++;
        }
    }
    return JoinArray( items );
}

void SerializeTree( Obj &o, Fl_Tree *tree )
{
    tree->calc_tree();
    vector< string > items;
    int index = 0;
    for ( Fl_Tree_Item *item = tree->first(); item; item = tree->next( item ), index++ )
    {
        if ( !item->visible_r() )
        {
            continue;
        }
        if ( item == tree->root() && !tree->showroot() )
        {
            continue;
        }
        Obj it;
        it.Int( "n", index ).Str( "label", item->label() ? item->label() : "" ).Int( "d", item->depth() )
          .Bool( "open", item->is_open() ).Bool( "sel", item->is_selected() ).Int( "kids", item->children() )
          .Int( "x", item->x() ).Int( "y", item->y() ).Int( "w", item->w() ).Int( "h", item->h() )
          .Int( "lx", item->label_x() ).Bool( "act", item->is_active() );
        TreeIconItem *ti = dynamic_cast< TreeIconItem* >( item );
        if ( ti )
        {
            const int *sx = ti->GetShowIconXy();
            const int *fx = ti->GetSurfIconXy();
            it.Bool( "show", ti->GetShowState() ).Int( "surf", ti->GetSurfState() ).Str( "ref", ti->GetRefID() )
              .Raw( "showxy", "[" + std::to_string( sx[0] ) + "," + std::to_string( sx[1] ) + "," + std::to_string( sx[2] ) + "," + std::to_string( sx[3] ) + "]" )
              .Raw( "surfxy", "[" + std::to_string( fx[0] ) + "," + std::to_string( fx[1] ) + "," + std::to_string( fx[2] ) + "," + std::to_string( fx[3] ) + "]" );
        }
        items.push_back( it.Done() );
    }
    o.Raw( "items", JoinArray( items ) );
}

string SerializeWidget( Fl_Widget *w, int win_id, int parent_id, int order, int dx, int dy )
{
    const char *kind = KindOf( w );
    Obj o;
    o.Int( "id", IdFor( w ) ).Int( "win", win_id ).Int( "p", parent_id ).Int( "o", order ).Str( "k", kind );
    o.Int( "x", w->x() + dx ).Int( "y", w->y() + dy ).Int( "w", w->w() ).Int( "h", w->h() );
    o.Bool( "vis", w->visible() != 0 ).Bool( "act", w->active_r() != 0 );
    if ( w->label() && w->labeltype() != FL_NO_LABEL )
    {
        o.Str( "label", w->label() );
    }
    o.Int( "align", w->align() ).Int( "box", w->box() ).Int( "ls", w->labelsize() ).Int( "lf", w->labelfont() );
    o.Str( "col", ColorHex( w->color() ) ).Str( "lcol", ColorHex( w->labelcolor() ) ).Str( "scol", ColorHex( w->selection_color() ) );
    if ( w->tooltip() )
    {
        o.Str( "tip", w->tooltip() );
    }

    // Bound Parm, for hover info.
    if ( w->callback() == GuiDevice::StaticDeviceCB && w->user_data() )
    {
        GuiDevice *dev = static_cast< GuiDevice* >( w->user_data() );
        Parm *p = ParmMgr.FindParm( dev->GetParmID() );
        if ( p )
        {
            o.Str( "parm", p->GetGroupName() + ":" + p->GetName() ).Str( "desc", p->GetDescript() );
        }
    }

    string k = kind;
    if ( k == "button" || k == "light" || k == "check" || k == "round" || k == "repeat" )
    {
        Fl_Button *b = static_cast< Fl_Button* >( w );
        o.Int( "v", b->value() ).Int( "type", b->type() );
    }
    else if ( k == "slider" || k == "value_slider" || k == "scrollbar" || k == "roller" || k == "dial" || k == "counter" )
    {
        Fl_Valuator *v = static_cast< Fl_Valuator* >( w );
        o.Num( "v", v->value() ).Num( "min", v->minimum() ).Num( "max", v->maximum() ).Num( "step", v->step() ).Int( "type", v->type() );
        if ( k == "scrollbar" )
        {
            o.Num( "ss", static_cast< Fl_Slider* >( w )->slider_size() );
        }
    }
    else if ( k == "progress" )
    {
        Fl_Progress *p = static_cast< Fl_Progress* >( w );
        o.Num( "v", p->value() ).Num( "min", p->minimum() ).Num( "max", p->maximum() );
    }
    else if ( k == "input" || k == "output" )
    {
        Fl_Input_ *in = static_cast< Fl_Input_* >( w );
        o.Str( "v", in->value() ? in->value() : "" ).Int( "type", in->input_type() ).Bool( "ro", in->readonly() != 0 )
         .Int( "ts", in->textsize() ).Str( "tcol", ColorHex( in->textcolor() ) );
    }
    else if ( k == "choice" || k == "menubar" || k == "menubutton" )
    {
        Fl_Menu_ *m = static_cast< Fl_Menu_* >( w );
        o.Raw( "items", MenuJson( m ) );
        if ( k == "choice" )
        {
            o.Int( "v", static_cast< Fl_Choice* >( w )->value() ).Int( "ts", m->textsize() ).Str( "tcol", ColorHex( m->textcolor() ) );
        }
    }
    else if ( k == "input_choice" )
    {
        Fl_Input_Choice *ic = static_cast< Fl_Input_Choice* >( w );
        o.Str( "v", ic->value() ? ic->value() : "" ).Raw( "items", MenuJson( ic->menubutton() ) );
    }
    else if ( k == "tabs" )
    {
        Fl_Tabs *t = static_cast< Fl_Tabs* >( w );
        Fl_Widget *cur = t->value();
        o.Int( "v", cur ? IdFor( cur ) : 0 );
    }
    else if ( k == "browser" || k == "check_browser" )
    {
        vector< string > lines;
        if ( k == "check_browser" )
        {
            Fl_Check_Browser *cb = static_cast< Fl_Check_Browser* >( w );
            for ( int i = 1; i <= cb->nitems(); i++ )
            {
                Obj li;
                li.Str( "t", cb->text( i ) ? cb->text( i ) : "" ).Bool( "c", cb->checked( i ) != 0 ).Bool( "s", cb->value() == i );
                lines.push_back( li.Done() );
            }
        }
        else
        {
            Fl_Browser *br = static_cast< Fl_Browser* >( w );
            for ( int i = 1; i <= br->size(); i++ )
            {
                Obj li;
                li.Str( "t", br->text( i ) ? br->text( i ) : "" ).Bool( "s", br->selected( i ) != 0 ).Bool( "v", br->visible( i ) != 0 );
                lines.push_back( li.Done() );
            }
            const int *cw = br->column_widths();
            string cols = "[";
            for ( int i = 0; cw && cw[i]; i++ )
            {
                if ( i ) cols += ",";
                cols += std::to_string( cw[i] );
            }
            o.Raw( "cols", cols + "]" ).Str( "csep", string( 1, br->column_char() ) ).Int( "type", br->type() ).Int( "top", br->topline() );
        }
        o.Raw( "lines", JoinArray( lines ) ).Int( "ts", static_cast< Fl_Browser_* >( w )->textsize() );
    }
    else if ( k == "tree" )
    {
        SerializeTree( o, static_cast< Fl_Tree* >( w ) );
    }
    else if ( k == "text_editor" || k == "text_display" )
    {
        Fl_Text_Display *td = static_cast< Fl_Text_Display* >( w );
        if ( td->buffer() )
        {
            char *txt = td->buffer()->text();
            string t = txt ? txt : "";
            free( txt );
            if ( t.size() > 200000 )
            {
                t = t.substr( t.size() - 200000 );
            }
            o.Str( "v", t );
        }
        o.Int( "ts", td->textsize() );
    }
    if ( k == "table" || k == "canvas" || k == "gl" )
    {
        // The main 3D view is replaced by the client's own renderer.
        VspScreen *main = S().mgr ? S().mgr->GetScreen( vsp::VSP_MAIN_SCREEN ) : nullptr;
        bool in_main = main && main->GetFlWindow() && w->top_window() == main->GetFlWindow();
        if ( k == "table" )
        {
            Fl_Table *t = static_cast< Fl_Table* >( w );
            o.Int( "rows", t->rows() ).Int( "ncols", t->cols() );
        }
        if ( k == "gl" )
        {
            string draw = in_main ? string() : GlDrawJson( w );
            if ( !draw.empty() )
            {
                o.Raw( "draw", draw );
            }
        }
        else if ( const Capture *cap = CaptureWidget( w ) )
        {
            o.Int( "iw", cap->w ).Int( "ih", cap->h ).Str( "img", cap->b64 );
        }
    }
    return o.Done();
}

void WalkGroup( Fl_Group *g, int win_id, int parent_id, int dx, int dy, vector< string > &changed );

void VisitWidget( Fl_Widget *w, int win_id, int parent_id, int order, int dx, int dy, vector< string > &changed )
{
    State &s = S();
    int id = IdFor( w );
    string json = SerializeWidget( w, win_id, parent_id, order, dx, dy );
    WidgetRec &rec = s.widgets[w];
    rec.seen = true;
    rec.win = win_id;
    if ( rec.json != json || s.want_full )
    {
        rec.json = json;
        changed.push_back( json );
    }

    Fl_Group *g = w->as_group();
    if ( g && !IsLeafComposite( w ) )
    {
        // Children of a nested window are positioned relative to it.
        int cdx = dx, cdy = dy;
        if ( w->as_window() )
        {
            cdx += w->x();
            cdy += w->y();
        }
        WalkGroup( g, win_id, id, cdx, cdy, changed );
    }
}

void WalkGroup( Fl_Group *g, int win_id, int parent_id, int dx, int dy, vector< string > &changed )
{
    for ( int i = 0; i < g->children(); i++ )
    {
        VisitWidget( g->child( i ), win_id, parent_id, i, dx, dy, changed );
    }
}

WinInfo InfoFor( Fl_Window *win )
{
    auto it = S().info.find( win );
    if ( it != S().info.end() )
    {
        return it->second;
    }
    WinInfo wi;
    wi.role = "dialog";
    return wi;
}

void BuildWindowInfo()
{
    State &s = S();
    s.info.clear();
    if ( !s.mgr )
    {
        return;
    }
    for ( int i = 0; i < vsp::VSP_NUM_SCREENS; i++ )
    {
        VspScreen *scr = s.mgr->GetScreen( i );
        if ( !scr || !scr->GetFlWindow() )
        {
            continue;
        }
        WinInfo wi;
        wi.screen_index = i;
        wi.role = i == vsp::VSP_MAIN_SCREEN ? "main" : i == vsp::VSP_MANAGE_GEOM_SCREEN ? "browser" : "screen";
        BasicScreen *bs = dynamic_cast< BasicScreen* >( scr );
        wi.screen = bs ? bs->GetTitle() : ( i == vsp::VSP_MAIN_SCREEN ? "Main" : "" );
        s.info[scr->GetFlWindow()] = wi;
    }
    ManageGeomScreen *mgs = dynamic_cast< ManageGeomScreen* >( s.mgr->GetScreen( vsp::VSP_MANAGE_GEOM_SCREEN ) );
    if ( mgs )
    {
        vector< VspScreen* > gv = mgs->GetGeomScreenVec();
        for ( int i = 0; i < ( int ) gv.size(); i++ )
        {
            if ( gv[i] && gv[i]->GetFlWindow() )
            {
                WinInfo wi;
                wi.role = "geom";
                wi.screen_index = i;
                BasicScreen *bs = dynamic_cast< BasicScreen* >( gv[i] );
                wi.screen = bs ? bs->GetTitle() : "";
                s.info[gv[i]->GetFlWindow()] = wi;
            }
        }
    }
}

void Diff()
{
    State &s = S();
    vector< string > windows, closed, widgets, removed;

    for ( auto &kv : s.windows )
    {
        kv.second.seen = false;
    }
    for ( auto &kv : s.widgets )
    {
        kv.second.seen = false;
    }

    for ( Fl_Window *win = Fl::first_window(); win; win = Fl::next_window( win ) )
    {
        if ( win->parent() || win == MirrorGLWindow() )
        {
            continue; // subwindows are walked with their top-level window; skip our GL context
        }
        WinRec &wr = s.windows[win];
        if ( wr.id == 0 )
        {
            wr.id = IdFor( win );
        }
        wr.seen = true;
        WinInfo wi = InfoFor( win );
        Obj o;
        o.Int( "id", wr.id ).Str( "role", wi.role ).Str( "screen", wi.screen ).Int( "si", wi.screen_index )
         .Str( "title", win->label() ? win->label() : "" ).Int( "w", win->w() ).Int( "h", win->h() )
         .Int( "x", win->x() ).Int( "y", win->y() )
         .Bool( "modal", win->modal() != 0 );
        string json = o.Done();
        if ( wr.json != json || s.want_full )
        {
            wr.json = json;
            windows.push_back( json );
        }
        WalkGroup( win, wr.id, 0, 0, 0, widgets );
    }

    for ( auto it = s.windows.begin(); it != s.windows.end(); )
    {
        if ( !it->second.seen )
        {
            closed.push_back( std::to_string( it->second.id ) );
            it = s.windows.erase( it );
        }
        else
        {
            ++it;
        }
    }
    // Unseen widgets may already be deleted: compare pointers, never dereference them.
    std::set< const Fl_Widget* > window_ptrs;
    for ( auto &kv : s.windows )
    {
        window_ptrs.insert( kv.first );
    }
    for ( auto it = s.widgets.begin(); it != s.widgets.end(); )
    {
        bool is_window = window_ptrs.count( it->first ) > 0;
        if ( !it->second.seen && !is_window )
        {
            if ( !it->second.json.empty() )
            {
                removed.push_back( std::to_string( it->second.id ) );
            }
            s.by_id.erase( it->second.id );
            Captures().erase( it->first );
            it = s.widgets.erase( it );
        }
        else
        {
            ++it;
        }
    }

    bool full = s.want_full;
    s.want_full = false;
    if ( full || !windows.empty() || !closed.empty() || !widgets.empty() || !removed.empty() )
    {
        Obj o;
        o.Str( "t", "diff" ).Bool( "full", full ).Raw( "windows", JoinArray( windows ) ).Raw( "closed", JoinArray( closed ) )
         .Raw( "widgets", JoinArray( widgets ) ).Raw( "removed", JoinArray( removed ) );
        Emit( o.Done() );
    }
}

//==== Applying events ====//
void SetEventCoords( int x, int y, int mods, int clicks )
{
    Fl::e_x = x;
    Fl::e_y = y;
    Fl::e_x_root = x;
    Fl::e_y_root = y;
    Fl::e_state = mods;
    Fl::e_clicks = clicks;
}

void ApplyEvent( const std::map< string, string > &ev )
{
    State &s = S();
    string op = GetStr( ev, "op" );

    if ( op == "full" )
    {
        s.want_full = true;
        return;
    }

    if ( op == "mouse" || op == "key" || op == "close" || op == "resize" )
    {
        Fl_Window *win = WindowById( GetInt( ev, "win" ) );
        if ( !win )
        {
            return;
        }
        if ( op == "close" )
        {
            win->do_callback();
            return;
        }
        if ( op == "resize" )
        {
            // The client docks some windows at a different size (e.g. full panel height);
            // FLTK re-lays out the window by its own resizable rules.
            int nw = GetInt( ev, "w", win->w() ), nh = GetInt( ev, "h", win->h() );
            if ( nw > 0 && nh > 0 && ( nw != win->w() || nh != win->h() ) )
            {
                win->resize( win->x(), win->y(), nw, nh );
            }
            return;
        }
        int mods = GetInt( ev, "mods" );
        if ( op == "key" )
        {
            static string text;
            text = GetStr( ev, "text" );
            Fl::e_keysym = GetInt( ev, "key" );
            Fl::e_state = mods;
            Fl::e_text = const_cast< char* >( text.c_str() );
            Fl::e_length = ( int ) text.size();
            Fl::handle( FL_KEYBOARD, win );
            return;
        }
        string kind = GetStr( ev, "kind" );
        int b = GetInt( ev, "b", 1 );
        int bit = b == 1 ? FL_BUTTON1 : b == 2 ? FL_BUTTON2 : FL_BUTTON3;
        int x = GetInt( ev, "x" ), y = GetInt( ev, "y" );
        if ( kind == "push" )
        {
            SetEventCoords( x, y, mods | bit, GetInt( ev, "clicks" ) );
            Fl::e_keysym = FL_Button + b;
            Fl::e_is_click = 1;
            Fl::handle( FL_PUSH, win );
        }
        else if ( kind == "drag" )
        {
            SetEventCoords( x, y, mods | bit, 0 );
            Fl::e_is_click = 0;
            Fl::handle( FL_DRAG, win );
        }
        else if ( kind == "release" )
        {
            SetEventCoords( x, y, mods, GetInt( ev, "clicks" ) );
            Fl::e_keysym = FL_Button + b;
            Fl::handle( FL_RELEASE, win );
        }
        else if ( kind == "move" )
        {
            SetEventCoords( x, y, mods, 0 );
            Fl::handle( FL_MOVE, win );
        }
        else if ( kind == "wheel" )
        {
            SetEventCoords( x, y, mods, 0 );
            Fl::e_dx = 0;
            Fl::e_dy = GetInt( ev, "dy" );
            Fl::handle( FL_MOUSEWHEEL, win );
        }
        return;
    }

    if ( op == "dialog" )
    {
        return; // handled by the waiting prompt
    }

    Fl_Widget *w = WidgetById( GetInt( ev, "id" ) );
    if ( !w || !w->active_r() )
    {
        return;
    }
    string k = KindOf( w );

    if ( op == "set" )
    {
        string v = GetStr( ev, "v" );
        if ( k == "input" || k == "output" )
        {
            static_cast< Fl_Input_* >( w )->value( v.c_str() );
        }
        else if ( k == "input_choice" )
        {
            static_cast< Fl_Input_Choice* >( w )->value( v.c_str() );
        }
        else if ( k == "choice" )
        {
            static_cast< Fl_Choice* >( w )->value( atoi( v.c_str() ) );
        }
        else if ( k == "button" || k == "light" || k == "check" || k == "round" )
        {
            Fl_Button *b = static_cast< Fl_Button* >( w );
            if ( b->type() == FL_RADIO_BUTTON && atoi( v.c_str() ) )
            {
                b->setonly();
            }
            else
            {
                b->value( atoi( v.c_str() ) );
            }
        }
        else if ( dynamic_cast< Fl_Valuator* >( w ) )
        {
            Fl_Valuator *val = static_cast< Fl_Valuator* >( w );
            val->value( val->clamp( atof( v.c_str() ) ) );
        }
        else
        {
            return;
        }
        w->do_callback();
    }
    else if ( op == "click" )
    {
        Fl_Button *b = dynamic_cast< Fl_Button* >( w );
        if ( b )
        {
            if ( b->type() == FL_RADIO_BUTTON )
            {
                b->setonly();
            }
            else if ( b->type() == FL_TOGGLE_BUTTON )
            {
                b->value( !b->value() );
            }
        }
        Fl::e_clicks = 0;
        w->do_callback();
    }
    else if ( op == "pick" )
    {
        Fl_Menu_ *m = dynamic_cast< Fl_Menu_* >( w );
        if ( k == "input_choice" )
        {
            Fl_Input_Choice *ic = static_cast< Fl_Input_Choice* >( w );
            int i = GetInt( ev, "i" );
            if ( i >= 0 && i < ic->menubutton()->size() )
            {
                ic->menubutton()->value( i );
                ic->value( ic->menubutton()->text() );
                ic->do_callback();
            }
            return;
        }
        if ( m )
        {
            int i = GetInt( ev, "i" );
            if ( i >= 0 && i < m->size() && m->menu()[i].text )
            {
                const Fl_Menu_Item *item = &m->menu()[i];
                if ( item->flags & ( FL_MENU_INACTIVE | FL_SUBMENU ) )
                {
                    return;
                }
                m->picked( item );
            }
        }
    }
    else if ( op == "browser" )
    {
        Fl_Browser *br = dynamic_cast< Fl_Browser* >( w );
        if ( br )
        {
            int line = GetInt( ev, "line" );
            int mods = GetInt( ev, "mods" );
            bool multi = br->type() == FL_MULTI_BROWSER;
            if ( !multi || !( mods & ( FL_CTRL | FL_SHIFT ) ) )
            {
                br->deselect();
            }
            if ( line > 0 )
            {
                br->select( line, multi && ( mods & FL_CTRL ) ? !br->selected( line ) : 1 );
                br->value( line );
            }
            Fl::e_clicks = GetInt( ev, "clicks" ) > 1 ? 1 : 0;
            Fl::e_state = mods;
            br->do_callback();
            Fl::e_clicks = 0;
        }
    }
    else if ( op == "check" )
    {
        Fl_Check_Browser *cb = dynamic_cast< Fl_Check_Browser* >( w );
        if ( cb )
        {
            cb->checked( GetInt( ev, "line" ), GetInt( ev, "v" ) );
            cb->do_callback();
        }
    }
    else if ( op == "text" )
    {
        Fl_Text_Display *td = dynamic_cast< Fl_Text_Display* >( w );
        if ( td && td->buffer() )
        {
            td->buffer()->text( GetStr( ev, "v" ).c_str() );
            td->do_callback();
        }
    }
    else if ( op == "tab" )
    {
        Fl_Tabs *t = dynamic_cast< Fl_Tabs* >( w );
        Fl_Widget *child = WidgetById( GetInt( ev, "child" ) );
        if ( t && child && t->find( child ) < t->children() )
        {
            t->value( child );
            t->do_callback();
        }
    }
    else if ( op == "tree_icon" )
    {
        TreeWithIcons *tree = dynamic_cast< TreeWithIcons* >( w );
        if ( tree )
        {
            int target = GetInt( ev, "item" ), index = 0;
            for ( Fl_Tree_Item *item = tree->first(); item; item = tree->next( item ), index++ )
            {
                if ( index == target )
                {
                    TreeIconItem *ti = dynamic_cast< TreeIconItem* >( item );
                    if ( ti )
                    {
                        tree->MirrorIconEvent( ti, GetInt( ev, "icon" ) );
                    }
                    break;
                }
            }
        }
    }
}

void ApplyQueued( bool dialogs_only )
{
    while ( true )
    {
        string json;
        {
            std::lock_guard< std::mutex > lock( S().io_mutex );
            if ( S().inbox.empty() )
            {
                return;
            }
            json = S().inbox.front();
            S().inbox.pop_front();
        }
        std::map< string, string > ev = ParseFlat( json );
        if ( GetStr( ev, "op" ) == "dialog" )
        {
            std::lock_guard< std::mutex > lock( S().io_mutex );
            S().dialog_replies[GetInt( ev, "seq" )] = ev;
            continue;
        }
        if ( dialogs_only )
        {
            continue; // input to other windows is dropped while a prompt is up, as with a native modal
        }
        ApplyEvent( ev );
    }
}

std::map< string, string > WaitForDialog( int seq )
{
    State &s = S();
    s.modal_depth++;
    while ( true )
    {
        {
            std::lock_guard< std::mutex > lock( s.io_mutex );
            auto it = s.dialog_replies.find( seq );
            if ( it != s.dialog_replies.end() )
            {
                std::map< string, string > r = it->second;
                s.dialog_replies.erase( it );
                s.modal_depth--;
                return r;
            }
        }
        ApplyQueued( true );
        Fl::wait( 0.0 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        Diff();
    }
}

int NewDialogSeq()
{
    std::lock_guard< std::mutex > lock( S().io_mutex );
    return ++S().dialog_seq;
}

} // namespace

//==== Public API ====//

void WebMirror::Enable( ScreenMgr *mgr )
{
    State &s = S();
    if ( s.enabled )
    {
        return;
    }
    s.enabled = true;
    s.mgr = mgr;
    InstallCrashTrace();
    MirrorCreateGLContext();
    BuildWindowInfo();
    if ( mgr )
    {
        mgr->FirstShow();
    }
    s.want_full = true;
}

bool WebMirror::IsEnabled()
{
    return S().enabled;
}

void WebMirror::Pump( double timeout )
{
    State &s = S();
    if ( !s.enabled )
    {
        return;
    }
    MirrorMakeGLCurrent();
    ApplyQueued( s.modal_depth > 0 );

    // Hidden windows never get repainted, so FLTK always thinks a redraw is pending and
    // Fl::wait() returns at once; sleep out the rest of the slice so we don't spin.
    auto start = std::chrono::steady_clock::now();
    Fl::wait( timeout );
    auto budget = std::chrono::duration< double >( timeout );
    auto spent = std::chrono::steady_clock::now() - start;
    if ( spent < budget )
    {
        std::this_thread::sleep_for( budget - spent );
    }

    auto now = std::chrono::steady_clock::now();
    if ( s.want_full || now - s.last_diff > std::chrono::milliseconds( 30 ) )
    {
        s.last_diff = now;
        Diff();
    }
}

void WebMirror::PostEvent( const std::string &json )
{
    std::lock_guard< std::mutex > lock( S().io_mutex );
    S().inbox.push_back( json );
}

std::string WebMirror::TakeOutput()
{
    vector< string > out;
    {
        std::lock_guard< std::mutex > lock( S().io_mutex );
        out.swap( S().outbox );
    }
    return JoinArray( out );
}

void WebMirror::RequestFull()
{
    PostEvent( "{\"op\":\"full\"}" );
}

void WebMirror::PickGeom( const std::string &geom_id )
{
    State &s = S();
    if ( !s.mgr )
    {
        return;
    }
    ManageGeomScreen *mgs = dynamic_cast< ManageGeomScreen* >( s.mgr->GetScreen( vsp::VSP_MANAGE_GEOM_SCREEN ) );
    if ( mgs )
    {
        mgs->Set( geom_id );
    }
}

int WebMirror::Choice( const std::string &msg, const std::string &b0, const std::string &b1, const std::string &b2 )
{
    int seq = NewDialogSeq();
    Obj o;
    o.Str( "t", "dialog" ).Int( "seq", seq ).Str( "kind", "choice" ).Str( "msg", msg ).Str( "b0", b0 ).Str( "b1", b1 ).Str( "b2", b2 );
    Emit( o.Done() );
    return GetInt( WaitForDialog( seq ), "result" );
}

void WebMirror::Message( const std::string &kind, const std::string &msg )
{
    int seq = NewDialogSeq();
    Obj o;
    o.Str( "t", "dialog" ).Int( "seq", seq ).Str( "kind", kind ).Str( "msg", msg );
    Emit( o.Done() );
    // Nothing depends on how a message is dismissed, so don't block the GUI thread on it
    // (API errors raise these, and no client may be connected).
}

std::string WebMirror::FileChooser( const std::string &title, const std::string &filter, int mode,
                                    const std::string &preset_fname, const std::string &dir )
{
    int seq = NewDialogSeq();
    Obj o;
    o.Str( "t", "dialog" ).Int( "seq", seq ).Str( "kind", "file" ).Str( "title", title ).Str( "filter", filter )
     .Int( "mode", mode ).Str( "preset", preset_fname ).Str( "dir", dir );
    Emit( o.Done() );
    return GetStr( WaitForDialog( seq ), "path" );
}

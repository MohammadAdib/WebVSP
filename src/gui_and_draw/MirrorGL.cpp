//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// A hidden OpenGL context for the web-mirrored GUI. The GUI's own GL views are never
// created in mirror mode (the client draws them), but screens still create graphics
// resources (textures, buffers, backgrounds) as a side effect of updating. This context
// keeps those GL calls valid.
//
//////////////////////////////////////////////////////////////////////

#include "MirrorGL.h"

#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/platform.H>

#include "GraphicEngine.h"

namespace
{
Fl_Gl_Window *s_ctx = nullptr;
}

void MirrorCreateGLContext()
{
    if ( s_ctx )
    {
        return;
    }
    Fl_Group *cur = Fl_Group::current();
    Fl_Group::current( nullptr ); // top-level, not a child of whatever group is open
    s_ctx = new Fl_Gl_Window( -32000, -32000, 16, 16, "vspweb gl context" );
    s_ctx->mode( FL_RGB | FL_ALPHA | FL_DEPTH | FL_DOUBLE );
    s_ctx->end();
    Fl_Group::current( cur );

    s_ctx->show();
#ifdef _WIN32
    ShowWindow( fl_xid( s_ctx ), SW_HIDE );
#endif
    s_ctx->make_current();
    VSPGraphic::GraphicEngine::initGlew();
}

void MirrorMakeGLCurrent()
{
    if ( s_ctx && s_ctx->shown() )
    {
        s_ctx->make_current();
    }
}

Fl_Window *MirrorGLWindow()
{
    return s_ctx;
}

//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// WebMirror: run the FLTK GUI without putting windows on screen and mirror it to a web
// client.  Every shown window's widget tree (type, label, geometry, value, state) is
// serialized as JSON diffs; client input is applied back to the real widgets, so the
// screens' own logic does all the work.  Modal prompts and file choosers are answered by
// the client.
//
// Threading: Enable() and Pump() run on the GUI thread (the thread that called InitGUI).
// PostEvent() and TakeOutput() may be called from any thread.
//
//////////////////////////////////////////////////////////////////////

#ifndef WEBMIRROR_H_
#define WEBMIRROR_H_

#include <string>

class ScreenMgr;

class WebMirror
{
public:
    static void Enable( ScreenMgr* mgr );
    static bool IsEnabled();

    // Apply queued client events, run the FLTK event loop for up to timeout seconds and
    // queue a diff of what changed.
    static void Pump( double timeout );

    // Queue a client event (a flat JSON object, see WebMirror.cpp for the ops).
    static void PostEvent( const std::string &json );

    // Take all queued output messages as a JSON array (possibly "[]").
    static std::string TakeOutput();

    // Make the next diff a full snapshot (e.g. a new client connected).
    static void RequestFull();

    // Select the given geom as if picked in the 3D view.
    static void PickGeom( const std::string &geom_id );

    // Client-answered prompts; block (while pumping) until the client replies.
    static int Choice( const std::string &msg, const std::string &b0, const std::string &b1, const std::string &b2 );
    static void Message( const std::string &kind, const std::string &msg );
    static std::string FileChooser( const std::string &title, const std::string &filter, int mode,
                                    const std::string &preset_fname, const std::string &dir );
};

#endif // WEBMIRROR_H_

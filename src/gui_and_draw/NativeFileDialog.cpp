//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// The operating system's own open/save dialog, for the web GUI (see NativeFileDialog.h).
//
//////////////////////////////////////////////////////////////////////

#include "NativeFileDialog.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

#include <vector>

#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "shell32.lib" )

namespace
{

std::wstring Widen( const std::string &s )
{
    if ( s.empty() )
    {
        return std::wstring();
    }
    int n = MultiByteToWideChar( CP_UTF8, 0, s.data(), ( int ) s.size(), nullptr, 0 );
    std::wstring w( n, L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, s.data(), ( int ) s.size(), &w[0], n );
    return w;
}

std::string Narrow( const wchar_t *w )
{
    int n = WideCharToMultiByte( CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr );
    if ( n <= 1 )
    {
        return std::string();
    }
    std::string s( n - 1, '\0' );
    WideCharToMultiByte( CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr );
    return s;
}

std::string Error( const char *what, HRESULT hr )
{
    char buf[128];
    snprintf( buf, sizeof( buf ), "*error %s (0x%08lx)", what, ( unsigned long ) hr );
    return buf;
}

// The engine is a background process, so Windows won't let its dialog take the foreground
// from the browser: the dialog would open behind it. Own the dialog with an invisible,
// topmost window centred on the active window (the browser), and bring that window to the
// front by briefly sharing input state with the foreground thread.
HWND MakeOwner()
{
    HWND fg = GetForegroundWindow();
    RECT r = { 0, 0, 0, 0 };
    if ( !fg || !GetWindowRect( fg, &r ) )
    {
        SystemParametersInfoW( SPI_GETWORKAREA, 0, &r, 0 );
    }
    int cx = ( r.left + r.right ) / 2, cy = ( r.top + r.bottom ) / 3;
    HWND owner = CreateWindowExW( WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP, cx, cy, 0, 0,
                                  nullptr, nullptr, GetModuleHandleW( nullptr ), nullptr );
    if ( !owner )
    {
        return nullptr;
    }
    ShowWindow( owner, SW_SHOWNA );
    DWORD me = GetCurrentThreadId();
    DWORD fg_thread = fg ? GetWindowThreadProcessId( fg, nullptr ) : 0;
    bool attached = fg_thread && fg_thread != me && AttachThreadInput( me, fg_thread, TRUE );
    SetForegroundWindow( owner );
    SetActiveWindow( owner );
    if ( attached )
    {
        AttachThreadInput( me, fg_thread, FALSE );
    }
    return owner;
}

std::string ShowDialog( IFileDialog *dlg, const std::string &title, bool save, const std::string &types,
                        const std::string &dir, const std::string &file, const std::string &defext )
{
    // File types: keep the strings alive until Show() returns.
    std::vector< std::wstring > strs;
    std::vector< COMDLG_FILTERSPEC > specs;
    size_t start = 0;
    while ( start < types.size() )
    {
        size_t end = types.find( '\n', start );
        std::string line = types.substr( start, end == std::string::npos ? std::string::npos : end - start );
        start = end == std::string::npos ? types.size() : end + 1;
        size_t tab = line.find( '\t' );
        if ( tab == std::string::npos )
        {
            continue;
        }
        strs.push_back( Widen( line.substr( 0, tab ) ) );
        strs.push_back( Widen( line.substr( tab + 1 ) ) );
    }
    for ( size_t i = 0; i + 1 < strs.size(); i += 2 )
    {
        specs.push_back( { strs[i].c_str(), strs[i + 1].c_str() } );
    }
    if ( !specs.empty() )
    {
        dlg->SetFileTypes( ( UINT ) specs.size(), specs.data() );
        dlg->SetFileTypeIndex( 1 );
    }

    DWORD opts = 0;
    dlg->GetOptions( &opts );
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    opts |= save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST;
    dlg->SetOptions( opts );

    if ( !title.empty() )
    {
        dlg->SetTitle( Widen( title ).c_str() );
    }
    if ( !defext.empty() )
    {
        dlg->SetDefaultExtension( Widen( defext ).c_str() );
    }
    if ( !dir.empty() )
    {
        IShellItem *folder = nullptr;
        if ( SUCCEEDED( SHCreateItemFromParsingName( Widen( dir ).c_str(), nullptr, IID_PPV_ARGS( &folder ) ) ) )
        {
            dlg->SetFolder( folder );
            folder->Release();
        }
    }
    if ( !file.empty() )
    {
        dlg->SetFileName( Widen( file ).c_str() );
    }

    HWND owner = MakeOwner();
    HRESULT hr = dlg->Show( owner );
    if ( owner )
    {
        DestroyWindow( owner );
    }
    if ( hr == HRESULT_FROM_WIN32( ERROR_CANCELLED ) )
    {
        return std::string();
    }
    if ( FAILED( hr ) )
    {
        return Error( "Show failed", hr );
    }

    IShellItem *item = nullptr;
    hr = dlg->GetResult( &item );
    if ( FAILED( hr ) )
    {
        return Error( "GetResult failed", hr );
    }
    std::string path;
    PWSTR wpath = nullptr;
    if ( SUCCEEDED( item->GetDisplayName( SIGDN_FILESYSPATH, &wpath ) ) )
    {
        path = Narrow( wpath );
        CoTaskMemFree( wpath );
    }
    item->Release();
    return path;
}

} // namespace

std::string NativeFileDialog::Show( const std::string &title, bool save, const std::string &types,
                                    const std::string &dir, const std::string &file, const std::string &defext )
{
    HRESULT init = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE );
    IFileDialog *dlg = nullptr;
    HRESULT hr = CoCreateInstance( save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   save ? IID_IFileSaveDialog : IID_IFileOpenDialog, ( void** ) &dlg );
    std::string result = SUCCEEDED( hr ) ? ShowDialog( dlg, title, save, types, dir, file, defext )
                                         : Error( "no file dialog", hr );
    if ( dlg )
    {
        dlg->Release();
    }
    if ( SUCCEEDED( init ) )
    {
        CoUninitialize();
    }
    return result;
}

#else

std::string NativeFileDialog::Show( const std::string &, bool, const std::string &, const std::string &,
                                    const std::string &, const std::string & )
{
    return "*error native file dialogs are only available on Windows";
}

#endif

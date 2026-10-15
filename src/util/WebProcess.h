//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// External programs (VSPAERO's solver and loads tools) where processes can't be started:
// WebAssembly in a browser. ProcessUtil queues a "spawn" request here; the web page runs the
// program (itself compiled to WebAssembly) in a worker with copies of the working files,
// feeds its output back with Output(), copies the files it wrote back, then calls Exit().
// To the code using ProcessUtil it behaves like a child process with a stdout pipe.
//
// Thread-safe: requests are made on OpenVSP's thread, output arrives on the page's thread.
//
//////////////////////////////////////////////////////////////////////

#if !defined( WEBPROCESS__INCLUDED_ )
#define WEBPROCESS__INCLUDED_

#include <string>
#include <vector>

namespace WebProcess
{

// Queue a program run; returns its id (> 0).
int Start( const std::string &cmd, const std::vector< std::string > &args );

// Until the page reports the exit (after the program's files are back).
bool Running( int id );

// Up to n bytes of the program's output not yet read; 0 when there's none.
int Read( int id, char *buf, int n );

// Ask the page to stop the program.
void Kill( int id );

// The program's exit code, once it has exited (else -1).
int ExitCode( int id );

// For the page: pending requests as a JSON array, e.g.
//   [{"t":"spawn","id":1,"cmd":"vspaero","args":["-omp","4","/home/web_user/wing"],"cwd":"/"},
//    {"t":"kill","id":1}]
std::string TakeRequests();
void Output( int id, const std::string &text );
void Exit( int id, int code );

}

#endif // WEBPROCESS__INCLUDED_

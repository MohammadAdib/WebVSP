//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#ifndef CRASHTRACE_H_
#define CRASHTRACE_H_

// Print a symbolized native stack trace to stderr on crashes and abort() (Windows only).
void InstallCrashTrace();

#endif // CRASHTRACE_H_

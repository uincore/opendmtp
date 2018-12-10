// ----------------------------------------------------------------------------
// Copyright 2006-2007, Martin D. Flynn
// All rights reserved
// ----------------------------------------------------------------------------
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// ----------------------------------------------------------------------------
// Description:
//  Debug/Info logging.
//  Since each platform may provide its own type of logging facilities, this
//  module has been placed in the 'custom/' folder.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#define SKIP_TRANSPORT_MEDIA_CHECK // only if TRANSPORT_MEDIA not used in this file 
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "tools/stdtypes.h"
#include "tools/utctools.h"
#include "tools/strtools.h"

#include "log.h"

// ----------------------------------------------------------------------------

/* maintain debug mode */
static utBool _isDebugMode = utFalse;
utBool isDebugMode() { return _isDebugMode; }
void setDebugMode(utBool mode) { _isDebugMode = mode; }

// ----------------------------------------------------------------------------

static void _logVMsg(const UInt8 *ftn, int line, const char *type, const char *fmt, va_list ap)
{
    // This function is not thread-safe.  It is possible that log messages from separate 
    // threads could be interleaved.
    
    /* function:line */
    if (ftn) {
        // print function:line (attempt to remove any prefixing path)
        const char *p = ftn, *s = ftn;
        for (;*s;s++) { if ((*s=='/')||(*s=='\\')) { p=s+1; } }
        fprintf(stdout, "[%s:%d] ", p, line);
    }
    
    /* message header */
    if (type) { 
        fprintf(stdout, "%s", type); 
    }
    
    /* print the message */
    vprintf(fmt, ap);
    
    /* trailing newline */
    if (fmt[strlen(fmt)-1] != '\n') { 
        fprintf(stdout, "\n"); 
    }
    
}

// ----------------------------------------------------------------------------

void logDebug_(const UInt8 *ftn, int line, const char *fmt, ...)
{
    if (isDebugMode()) {
        va_list ap;
        va_start(ap, fmt);
        _logVMsg(ftn, line, "DEBUG: ", fmt, ap);
        va_end(ap);
    }
}

void logInfo_(const UInt8 *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logVMsg(ftn, line, "INFO: ", fmt, ap);
    va_end(ap);
}

void logWarning_(const UInt8 *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logVMsg(ftn, line, "WARN: ", fmt, ap);
    va_end(ap);
}

void logError_(const UInt8 *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logVMsg(ftn, line, "ERROR: ", fmt, ap);
    va_end(ap);
}

void logCritical_(const UInt8 *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logVMsg(ftn, line, "CRITICAL: ", fmt, ap);
    va_end(ap);
}

// ----------------------------------------------------------------------------

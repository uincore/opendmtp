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
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/07/28  Martin D. Flynn
//     -Added temporary suspend feature (typically for debug purposes only)
// ----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "tools/stdtypes.h"
#include "tools/utctools.h"
#include "tools/strtools.h"
#include "tools/buffer.h"
#include "tools/io.h"

#include "server/log.h"

// ----------------------------------------------------------------------------

#define USE_STRING_BUFFER
#define SUPPORT_SUSPEND
#define SUSPEND_BUFFER_SIZE         7000L
#define MAX_LOG_STRING_SIZE         1000  // unlikely it will ever be this large

// ----------------------------------------------------------------------------

/* extract the source file name from the full path (ie. from '__FILE__') */
const char *logSrcFile(const char *fn)
{
    if (fn && *fn) {
        int fnLen = strlen(fn), fi = fnLen - 1, fLen = 0;
        const char *f = (char*)fn, *fp = (char*)0;
        for (; fi >= 0; fi--) {
            if (fn[fi] == '.') { fp = &fn[fi]; }
            if ((fn[fi] == '/') || (fn[fi] == '\\')) { 
                f = &fn[fi + 1];
                fLen = fp? (fp - f) : (fnLen - (fi + 1));
                break;
            }
        }
        // 'f'    - points to the beginning of the source file name
        // 'fp'   - points to the (first) '.' before the extension
        // 'fLen' - is the length of the source file name without the extension
        return f; // just return the source file pointer
    } else {
        return fn;
    }
}

// ----------------------------------------------------------------------------

/* maintain debug mode */
static utBool _isDebugMode = utFalse;
utBool isDebugMode() { return _isDebugMode; }
void setDebugMode(utBool mode) { _isDebugMode = mode; }

// ----------------------------------------------------------------------------
// allow temporary suspension of logging
// (this is generally used for debugging purposes and probably should not be
// included in a production system)

#if defined(USE_STRING_BUFFER) && defined(SUPPORT_SUSPEND)
/* suspend support */
static utBool logSuspended = utFalse;
static CircleBuffer_t *suspendCB = (CircleBuffer_t*)0;
void logSetSuspend(utBool suspend)
{
    if (suspend) {
        if (!logSuspended) {
            if (!suspendCB) { suspendCB = bufferCreate(SUSPEND_BUFFER_SIZE); }
            bufferClear(suspendCB);
            logSuspended = utTrue;
        }
    } else {
        if (logSuspended) {
            logSuspended = utFalse;
            char b[MAX_LOG_STRING_SIZE];
            while (bufferGetLength(suspendCB) > 0L) {
                bufferGetString(suspendCB, b, sizeof(b));
                fprintf(stdout, "%s", b);
            }
            ioFlushStream(stdout);
        }
    }
}
#endif

// ----------------------------------------------------------------------------

static void _logVMsg(const UInt8 *ftn, int line, const char *type, const char *fmt, va_list ap)
{
    // This function may not be thread-safe.  It is possible that log messages from  
    // separate threads could be interleaved.
    FILE *output = stdout;
#if defined(USE_STRING_BUFFER)
    char strBuf[MAX_LOG_STRING_SIZE], *sb = strBuf;
    int sbLen = sizeof(strBuf);
#endif
    
    /* function:line */
    if (ftn) {
        // print function:line (attempt to remove any prefixing path)
        const char *p = ftn, *s = ftn;
        for (;*s;s++) { if ((*s=='/')||(*s=='\\')) { p=s+1; } }
#if defined(USE_STRING_BUFFER)
        snprintf(sb, sbLen, "[%s:%d] ", p, line); {int n=strlen(sb);sb+=n;sbLen-=n;}
#else
        fprintf(output, "[%s:%d] ", p, line);
#endif
    }
    
    /* message header */
    if (type) {
#if defined(USE_STRING_BUFFER)
        snprintf(sb, sbLen, "%s", type); {int n=strlen(sb);sb+=n;sbLen-=n;}
#else
        fprintf(output, "%s", type);
#endif
    }
    
    /* print the message */
#if defined(USE_STRING_BUFFER)
    vsnprintf(sb, sbLen, fmt, ap); {int n=strlen(sb);sb+=n;sbLen-=n;}
#else
    vprintf(fmt, ap);
#endif

    /* trailing newline */
    if (fmt[strlen(fmt)-1] != '\n') {
#if defined(USE_STRING_BUFFER)
        snprintf(sb, sbLen, "\n"); {int n=strlen(sb);sb+=n;sbLen-=n;}
#else
        fprintf(output, "\n");
#endif
    }
    
#if defined(USE_STRING_BUFFER)
    /* print string */
#if defined(SUPPORT_SUSPEND)
    if (logSuspended) {
        bufferPutString(suspendCB, strBuf);
        return;
    }
#endif
    fprintf(output, "%s", strBuf);  // check returned error codes?
#endif
    
    /* flush output */
    ioFlushStream(output);

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

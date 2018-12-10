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
//  - Since each platform may provide its own type of logging facilities, this
//  module has been placed in the 'custom/' folder.
//  - The logging facility provided here now includes support for Linux 'syslog'
//  output (which may be overkill for some embedded systems).
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/12  Martin D. Flynn
//     -Upgraded with syslog support
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
//     -Added Aux storage logging
//     -Added threaded logging (to prevent logging from blocking other threads)
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

// For auxiliary message logging, the following must be defined:
//   "LOGGING_MESSAGE_FILE"
//   "LOGGING_AUX_DIRECTORY"

// ----------------------------------------------------------------------------

// uncomment to include syslog support
#if defined(TARGET_LINUX) || defined(TARGET_GUMSTIX)
#  define INCLUDE_SYSLOG_SUPPORT
#endif

// uncomment to display log messages in a separate thread
#define SYSLOG_THREAD

// ----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#if defined(INCLUDE_SYSLOG_SUPPORT)
#  include <syslog.h>
#endif

#include "custom/log.h"

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/threads.h"
#include "tools/buffer.h"
#include "tools/io.h"

// ----------------------------------------------------------------------------

#if defined(TARGET_WINCE)
// (Windows return '-1' if lengths exceeds 'size' which is different than Linux)
#  define VSNPRINTF             _vsnprintf
#else
// (Linux return the number of bytes that would have been written if the buffer were long enough)
#  define VSNPRINTF             vsnprintf
#endif

// ----------------------------------------------------------------------------

// This value may be overridden in "defaults.h" by defining "LOGGING_DEFAULT_LEVEL"
#if defined(LOGGING_DEFAULT_LEVEL)
#  define DEFAULT_LOG_LEVEL     LOGGING_DEFAULT_LEVEL
#else
#  define DEFAULT_LOG_LEVEL     -SYSLOG_ERROR // (neg) default to 'stderr'
#endif

#define MAX_MESSAGE_LENGTH      512

#define CONSOLE_OUTPUT          stderr

#define FLUSH_MODULO            5L

// ----------------------------------------------------------------------------

static char                 syslogName[32];
static int                  syslogLevel = (DEFAULT_LOG_LEVEL >= 0)? DEFAULT_LOG_LEVEL : -DEFAULT_LOG_LEVEL;
static utBool               syslogInit  = utFalse;

#if defined(INCLUDE_SYSLOG_SUPPORT)
static utBool               syslogEnabled = utTrue;
#endif

static threadMutex_t        syslogMutex;
#define LOG_LOCK            MUTEX_LOCK(&syslogMutex);
#define LOG_UNLOCK          MUTEX_UNLOCK(&syslogMutex);

#if defined(SYSLOG_THREAD)
#  define LOG_BUFFER_SIZE   3000 // should be sufficient for most cases
static utBool               syslogRunThread = utFalse;
static threadThread_t       syslogThread;
static CircleBuffer_t       *syslogBuffer = (CircleBuffer_t*)0;
static threadCond_t         syslogCond;
static struct timespec      syslogCondWaitTime;
#  define LOG_WAIT(T)       CONDITION_TIMED_WAIT(&syslogCond,&syslogMutex,utcGetAbsoluteTimespec(&syslogCondWaitTime,(T)));
#  define LOG_NOTIFY        CONDITION_NOTIFY(&syslogCond);
#endif

#if defined(LOGGING_MESSAGE_FILE) && defined(LOGGING_AUX_DIRECTORY)
static FILE                 *syslogAuxFile = (FILE*)0;
#endif

static UInt32               syslogMsgCount = 0L;

// ----------------------------------------------------------------------------

/* maintain debug mode */
static utBool _isDebugMode = utFalse;
utBool isDebugMode() 
{
    return _isDebugMode; 
}

/* set debug mode */
void setDebugMode(utBool mode)
{ 
    _isDebugMode = mode;
    logSetLevel(_isDebugMode? SYSLOG_DEBUG : DEFAULT_LOG_LEVEL);
    logEnableSyslog(utFalse);
}

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

/* syslog output */
#if defined(INCLUDE_SYSLOG_SUPPORT)
static void _logSyslogPrint(int level, const char *trace, const char *fmt, va_list ap)
{
    char buf[MAX_MESSAGE_LENGTH];
    if (!syslogInit) { logInitialize(0); } // <-- if not yet initialized
    int maxLen = 1 + strlen(trace) + 2 + strlen(fmt) + 1;
    if (trace && *trace && (maxLen < sizeof(buf))) {
        // combine 'trace' with 'fmt'
        sprintf(buf, "[%s] %s", trace, fmt);
        fmt = buf;
    }
    vsyslog(level, fmt, ap);
}
#endif

// ----------------------------------------------------------------------------

/* auxilliary output */
#if defined(LOGGING_MESSAGE_FILE) && defined(LOGGING_AUX_DIRECTORY)
static void _logAuxlogPrint(int level, const char *msg, int msgLen)
{
    // Log to aux storage.
    //  - Log to aux storage, but minimize the burden on the CPU.
    //  - Allow the aux storage media to be removed at any time.
    if (syslogAuxFile || ioIsDirectory(LOGGING_AUX_DIRECTORY)) {
        // aux-storage dir exists (SD/MMC card is available)
        // Note: opening/closing the log file multiple times does impact
        // performance, so this feature should only be used for debugging purposes.
        if (!syslogAuxFile) {
            // open aux file
            syslogAuxFile = ioOpenStream(LOGGING_MESSAGE_FILE, IO_OPEN_APPEND);
        }
        if (syslogAuxFile) {
            // aux file is open, continue ...
            if (msgLen < 0) { msgLen = strlen(msg); } // includes '\n'
            if (fwrite(msg,1,msgLen,syslogAuxFile) < 0) {
                // write error occurred, close file
                ioCloseStream(syslogAuxFile);
                syslogAuxFile = (FILE*)0;
            }
        }
    }
}
#endif

/* console output */
#if defined(LOGGING_CONSOLE)
static void _logConsolePrint(int level, const char *msg, int msgLen)
{
    if ((level <= SYSLOG_INFO) || isDebugMode()) {
        fwrite(msg, 1, msgLen, CONSOLE_OUTPUT);
    }
}
#endif

/* lock and log */
static void _logMessagePrint(const char *msg, int msgLen)
{

    /* nothing to print? */
    if (!msg || !*msg || (msgLen <= 0)) {
        return;
    }

    /* skip first message character */
    int level = *msg - '0';
    const char *m = msg + 1;
    int mLen = msgLen - 1;

    /* aux file logging */
#if defined(LOGGING_MESSAGE_FILE) && defined(LOGGING_AUX_DIRECTORY)
    _logAuxlogPrint(level, m, mLen);
#endif

    /* console logging */
#if defined(LOGGING_CONSOLE)
    _logConsolePrint(level, m, mLen);
#endif

}

/* flush output */
static void _logMessageFlush()
{
#if defined(LOGGING_MESSAGE_FILE) && defined(LOGGING_AUX_DIRECTORY)
    if (syslogAuxFile) {
        //if (isDebugMode()) { fprintf(CONSOLE_OUTPUT, "Flush ...\n"); } // ignore returned error code
        ioCloseStream(syslogAuxFile); // flush & close
        syslogAuxFile = (FILE*)0;
    }
#endif
#if defined(LOGGING_CONSOLE)
    ioFlushStream(CONSOLE_OUTPUT);
#endif
}

// ----------------------------------------------------------------------------

/* base logging print function */
static void _logPrint(utBool force, int level, const char *fn, int line, const char *fmt, va_list ap)
{
    // Levels:
    //   LOG_EMERG      0   [not used]
    //   LOG_ALERT      1   [not used]
    //   LOG_CRIT       2   
    //   LOG_ERR        3   
    //   LOG_WARNING    4   
    //   LOG_NOTICE     5   [not used]
    //   LOG_INFO       6   
    //   LOG_DEBUG      7
    
    /* skip this message? */
    if (!force && (level > syslogLevel)) {
        return;
    }
        
    /* extract source file from name */
    // It is important that 'fn' not contain any string formatting characters
    char trace[80];
    if (fn && *fn && (line > 0)) {
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
        if ((fLen + 7) < sizeof(trace)) {
            sprintf(trace,"%.*s:%d",fLen,f,line);
            fn = trace;
        }
    }

    /* syslog */
#if defined(INCLUDE_SYSLOG_SUPPORT)
    if (syslogEnabled) {
        _logSyslogPrint(level, fn, fmt, ap);
        return;
    } 
#endif

    /* 'Level' title */
    const char *lvlName = "?";
    switch (level) {
        case SYSLOG_CRITICAL: lvlName = "CRITICAL";  break; // Critical errors
        case SYSLOG_ERROR:    lvlName = "ERROR";     break; // General errors
        case SYSLOG_WARNING:  lvlName = "WARN";      break; // Warnings
        case SYSLOG_INFO:     lvlName = "info";      break; // Informational
        case SYSLOG_DEBUG:    lvlName = "dbug";      break; // Debug
    }

    /* format the message */
    char msg[MAX_MESSAGE_LENGTH + 2];
    int msgLen = 0;
    if (fn && *fn) {
        // pre-pend function information
        msgLen = sprintf(msg, "%d%s.%s[%s] ", level % 10, syslogName, lvlName, fn);
    }
    // append message
    // (Note: Windows return value for 'VSNPRINTF' is different than Linux)
    int bufSize = MAX_MESSAGE_LENGTH - msgLen;
    int vsnLen = VSNPRINTF(&msg[msgLen], bufSize, fmt, ap);
    if ((vsnLen < 0) || (vsnLen >= bufSize)) {
        // Windows returns '-1' if the written length exceeds 'bufSize'
        // Linux returns the number of bytes that _would_ have been written
        msgLen = MAX_MESSAGE_LENGTH;
    } else {
        // 'vsnLen' bytes written
        msgLen += vsnLen;
    }
    // trailing '\n'
    if (msg[msgLen - 1] != '\n') {
        strcpy(&msg[msgLen++], "\n");
    } else {
        msg[msgLen] = 0;
    }

    /* output message */
    LOG_LOCK {
#if defined(SYSLOG_THREAD)
        if (syslogRunThread) {
            if (!bufferPutData(syslogBuffer,msg,msgLen+1)) { // <-- incl terminator
                // this log message will be discarded
                if (isDebugMode()) { fprintf(CONSOLE_OUTPUT, "Log overflow! [%s]\n", msg); } // ignore returned error code
            }
            LOG_NOTIFY;
        } else 
#endif
        {
            _logMessagePrint(msg, msgLen);
            if ((syslogMsgCount % FLUSH_MODULO) == 0) {
                _logMessageFlush();
            }
        }
        syslogMsgCount++;
    } LOG_UNLOCK

}

// ----------------------------------------------------------------------------

/* return true if current level equals specified level */
utBool logIsLevel(int level)
{
    return (level <= syslogLevel)? utTrue : utFalse;
}

/* return true if syslog is in effect */
utBool logIsSyslog()
{
#if defined(INCLUDE_SYSLOG_SUPPORT)
    return syslogEnabled;
#else
    return utFalse;
#endif
}

/* parse logging level name */
int logParseLevel(const char *level)
{
    if (level && *level) {
        // a prefixing '+'/'-' character means send log messages to 'stderr'
        // ('+' was included to allow for easier command-line parsing)
        int tty = (*level == '+') || (*level == '-')? -1 : 1;
        const char *lvl = (tty < 0)? (level + 1) : level;
        if (isdigit(*lvl)) {
            return tty * (int)strParseInt32(lvl, SYSLOG_ERROR);
        } else
        if (strStartsWithIgnoreCase(lvl,"cri")) {
            return tty * SYSLOG_CRITICAL;
        } else
        if (strStartsWithIgnoreCase(lvl,"err")) {
            return tty * SYSLOG_ERROR;
        } else
        if (strStartsWithIgnoreCase(lvl,"war")) {
            return tty * SYSLOG_WARNING;
        } else
        if (strStartsWithIgnoreCase(lvl,"inf")) {
            return tty * SYSLOG_INFO;
        } else
        if (strStartsWithIgnoreCase(lvl,"deb") || strStartsWithIgnoreCase(lvl,"dbg")) {
            return tty * SYSLOG_DEBUG;
        } else {
            return tty * SYSLOG_ERROR; // the default
        }
    } else {
        return SYSLOG_NONE;
    }
}

/* set logging level */
void logSetLevel(int level)
{
    int lvl = (level >= 0)? level : -level; // absolute value
    if (lvl != syslogLevel) {
        syslogLevel = lvl;
        logPRINTF(LOGSRC,SYSLOG_INFO,"Set log level: %d", syslogLevel);
    } else {
        //logERROR(LOGSRC,"Already at log level: %d", syslogLevel);
    }
}

void logEnableSyslog(utBool enable)
{
#if defined(INCLUDE_SYSLOG_SUPPORT)
    syslogEnabled = enable;
#endif
}

// ----------------------------------------------------------------------------

/* log messages */
void logPrintf_(const char *ftn, int line, int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logPrint(utTrue, level, ftn, line, fmt, ap);
    va_end(ap);
}

/* log debug messages */
void logDebug_(const char *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logPrint(utFalse, SYSLOG_DEBUG, ftn, line, fmt, ap);
    va_end(ap);
}

/* log info messages */
void logInfo_(const char *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logPrint(utFalse, SYSLOG_INFO, ftn, line, fmt, ap);
    va_end(ap);
}

/* log warning messages */
void logWarning_(const char *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logPrint(utFalse, SYSLOG_WARNING, ftn, line, fmt, ap);
    va_end(ap);
}

/* log error messages */
void logError_(const char *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logPrint(utFalse, SYSLOG_ERROR, ftn, line, fmt, ap);
    va_end(ap);
}

/* log critical messages */
void logCritical_(const char *ftn, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _logPrint(utFalse, SYSLOG_CRITICAL, ftn, line, fmt, ap);
    va_end(ap);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

#if defined(SYSLOG_THREAD)

/* thread main */
static void _syslogThreadRunnable(void *arg)
{
    char data[MAX_MESSAGE_LENGTH + 2];
    int maxDataLen = sizeof(data);
    while (syslogRunThread) {
        
        /* wait for data */
        int len = 0;
        while (syslogRunThread && (len <= 0)) {
            LOG_LOCK {
                // get a single string
                len = bufferGetString(syslogBuffer, data, maxDataLen);
            } LOG_UNLOCK
            if (len <= 0L) {
                // queue is empty, flush output now (outside of the lock!)
                _logMessageFlush();
                // wait until we have something in the queue
                LOG_LOCK {
                    if (bufferGetLength(syslogBuffer) <= 0L) {
                        // still nothing in the queue
                        // wait a few seconds, or until we get notified
                        LOG_WAIT(5000L);
                    }
                } LOG_UNLOCK
            }
        }
        if (!syslogRunThread) {
            break;
        }

        /* print string */
        //fprintf(CONSOLE_OUTPUT, "{Thread} ");  // ignore returned error code
        _logMessagePrint(data, len);

    }
    // once this thread stops, it isn't starting again
    logERROR(LOGSRC,"Stopping thread");
    threadExit();
}

/* call-back to stop thread */
static void _syslogStopThread(void *arg)
{
    syslogRunThread = utFalse;
#if defined(SYSLOG_THREAD)
    LOG_LOCK { LOG_NOTIFY; } LOG_UNLOCK
#endif
}

/* start serial communication thread */
static utBool _syslogStartThread()
{
    
    /* thread already running? */
    if (syslogRunThread) {
        return utTrue;
    }
    
    /* init */
    threadConditionInit(&syslogCond);
    syslogBuffer = bufferCreate(LOG_BUFFER_SIZE);

    /* create thread */
    syslogRunThread = utTrue;
    if (threadCreate(&syslogThread,&_syslogThreadRunnable,0,"Syslog") == 0) {
        // thread started successfully
        threadAddThreadStopFtn(&_syslogStopThread,0);
        return utTrue;
    } else {
        syslogRunThread = utFalse;
        return utFalse;
    }
    
}

#endif // defined(SYSLOG_THREAD)

/* start serial communication thread */
utBool logStartThread()
{
#if defined(SYSLOG_THREAD)
    return _syslogStartThread();
#else
    return utFalse;
#endif
}

// ----------------------------------------------------------------------------

/* logging initialization */
void logInitialize(const char *id)
{
    
    /* init syslog */
    if (!syslogInit) {
        syslogInit = utTrue;

        /* save name */
        strncpy(syslogName, ((id && *id)? id : "unknown"), sizeof(syslogName) - 1);
        syslogName[sizeof(syslogName) - 1] = 0;

        /* init log mutex */
        threadMutexInit(&syslogMutex);

        /* init syslog */
#if defined(INCLUDE_SYSLOG_SUPPORT)
        int syslogOptions = LOG_NDELAY;
        if (!id || !*id) { syslogOptions |= LOG_PID; }
        openlog(syslogName, syslogOptions, LOG_DAEMON);
#endif

    }
    
}

// ----------------------------------------------------------------------------

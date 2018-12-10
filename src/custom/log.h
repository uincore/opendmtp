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

#ifndef _LOG_H
#define _LOG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include "tools/stdtypes.h"

// ----------------------------------------------------------------------------
//   LOG_EMERG       0      // not used
//   LOG_ALERT       1      // not used
//   LOG_CRIT        2
//   LOG_ERR         3
//   LOG_WARNING     4
//   LOG_NOTICE      5      // not used
//   LOG_INFO        6
//   LOG_DEBUG       7

// NOTE: These MUST mirror the "LOG_XXXX" definitions
#define SYSLOG_NONE         0 // LOG_EMERG   [not used]
#define SYSLOG_ALERT        1 // LOG_ALERT   [not used]
#define SYSLOG_CRITICAL     2 // LOG_CRIT
#define SYSLOG_ERROR        3 // LOG_ERR
#define SYSLOG_WARNING      4 // LOG_WARNING
#define SYSLOG_NOTICE       5 // LOG_NOTICE  [not used]
#define SYSLOG_INFO         6 // LOG_INFO
#define SYSLOG_DEBUG        7 // LOG_DEBUG

// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------

#define LOGSRC          __FILE__,__LINE__
#define logDEBUG        logDebug_
#define logINFO         logInfo_
#define logWARNING      logWarning_
#define logERROR        logError_
#define logCRITICAL     logCritical_
#define logPRINTF       logPrintf_

// Macro elipsis not supported by Windows compilers
//#define logDEBUG(FMT, ARGS...)    logDebug_(__FILE__, __LINE__, FMT, ## ARGS)

void logDebug_(const char *ftn, int line, const char *fmt, ...);
void logInfo_(const char *ftn, int line, const char *fmt, ...);
void logWarning_(const char *ftn, int line, const char *fmt, ...);
void logError_(const char *ftn, int line, const char *fmt, ...);
void logCritical_(const char *ftn, int line, const char *fmt, ...);
void logPrintf_(const char *ftn, int line, int level, const char *fmt, ...);

// ----------------------------------------------------------------------------

utBool isDebugMode();
void setDebugMode(utBool mode);

// ----------------------------------------------------------------------------

const char *logSrcFile(const char *fn);

// ----------------------------------------------------------------------------

void logInitialize(const char *id);
utBool logStartThread();

// ----------------------------------------------------------------------------

utBool logIsLevel(int level);
int logParseLevel(const char *level);
void logSetLevel(int level);
void logEnableSyslog(utBool enable);

// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif

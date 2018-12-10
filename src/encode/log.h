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

#include "tools/stdtypes.h"

// ----------------------------------------------------------------------------

/* the logging module also maintains the debug mode */
utBool isDebugMode();
void setDebugMode(utBool mode);

// ----------------------------------------------------------------------------

#define logDEBUG(FMT, ARGS...)      logDebug_   (__FILE__, __LINE__, FMT, ## ARGS)
#define logINFO(FMT, ARGS...)       logInfo_    (__FILE__, __LINE__, FMT, ## ARGS)
#define logWARNING(FMT, ARGS...)    logWarning_ (__FILE__, __LINE__, FMT, ## ARGS)
#define logERROR(FMT, ARGS...)      logError_   (__FILE__, __LINE__, FMT, ## ARGS)
#define logCRITICAL(FMT, ARGS...)   logCritical_(__FILE__, __LINE__, FMT, ## ARGS)

void logDebug_(const UInt8 *ftn, int line, const char *fmt, ...);
void logInfo_(const UInt8 *ftn, int line, const char *fmt, ...);
void logWarning_(const UInt8 *ftn, int line, const char *fmt, ...);
void logError_(const UInt8 *ftn, int line, const char *fmt, ...);
void logCritical_(const UInt8 *ftn, int line, const char *fmt, ...);

// ----------------------------------------------------------------------------

#endif

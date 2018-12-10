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

#ifndef _UTCTOOLS_H
#define _UTCTOOLS_H
#ifdef __cplusplus
extern "C" {
#endif

#include "tools/stdtypes.h"

// ----------------------------------------------------------------------------

#if defined(TARGET_WINCE)
#include <winsock2.h>
//struct timeval {
//    long tv_sec;
//    long tv_usec;
//};
struct timespec {
    UInt32 tv_sec;
    UInt32 tv_nsec;
};
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};
#else
#include <sys/time.h>
#endif

// ----------------------------------------------------------------------------

// This value simply represents an arbitrary time in the recent past.
// The purpose being that if the device finds it has a time prior to this value
// then it's clock is obviously incorrect (not a foolproof check for the validity
// of the system clock time, but does provide a simple sanity check).  This value 
// should be updated on each release.
#define MIN_CLOCK_TIME        1160590700L     // 2006/10/11 18:19:00 GMT
//#define MIN_CLOCK_TIME          1171200000L     // 2007/02/11 13:20:00 GMT
//#define MIN_CLOCK_TIME          1173000000L     // 2007/03/04 09:20:00 GMT
//#define MIN_CLOCK_TIME          1179000000L     // 2007/05/12 20:20:00 GMT

// minute/hour/day conversions to seconds
#define MINUTE_SECONDS(X)       ((UInt32)(X) * 60L)
#define HOUR_SECONDS(X)         ((UInt32)(X) * 60L * 60L)
#define DAY_SECONDS(X)          ((UInt32)(X) * 60L * 60L * 24L)
#define WEEK_SECONDS(X)         ((UInt32)(X) * 60L * 60L * 24L *   7L)
#define YEAR_SECONDS(X)         ((UInt32)(X) * 60L * 60L * 24L * 365L)

// ----------------------------------------------------------------------------

typedef UInt32  TimerSec_t;

#define UTC_TO_TIMER(U)         (TimerSec_t)(((U) > utcGetStartupTimeSec())? ((U) - utcGetStartupTimeSec()) : 0L)
//#define TIMER_TO_UTC(T)       (UInt32)((T) + utcGetStartupTimeSec())
#define TIMER_TO_UTC(T)         (UInt32)(((T) > 0L)? ((T) + utcGetStartupTimeSec()) : 0L)

// ----------------------------------------------------------------------------

#if defined(TARGET_WINCE)
#include <winbase.h>
typedef SYSTEMTIME  YMDHMS_t;
#else
typedef struct {
    int         wYear;
    int         wMonth;
    int         wDay;
    int         wHour;
    int         wMinute;
    int         wSecond;
    int         wMilliseconds;
} YMDHMS_t;
#endif

UInt32 utcYmdHmsToSeconds(YMDHMS_t *yh);
YMDHMS_t *utcSecondsToYmdHms(YMDHMS_t *yh, UInt32 utcSec);
const char *utcFormatDateTime(char *dt, UInt32 utcSec);

// ----------------------------------------------------------------------------

void utcMarkStartupTime();
UInt32 utcGetStartupTimeSec();

UInt32 utcGetTimeSec();
utBool utcSetTimeSec(UInt32 utcSec);

TimerSec_t utcGetTimer();
Int32 utcGetTimerAgeSec(TimerSec_t timerSec);
utBool utcIsTimerExpired(TimerSec_t timerSec, Int32 timeoutSec);

struct timeval *utcGetTimestamp(struct timeval *ts);
UInt32 utcGetDeltaMillis(struct timeval *ts1, struct timeval *ts2);
struct timeval *utcGetTimestampDelta(struct timeval *tv, Int32 deltaMS);

struct timespec *utcGetAbsoluteTimespec(struct timespec *ts, UInt32 offsetMS);

// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif

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
//  Machine interface to GPS module.
//  Abstraction layer for the machine interface to the GPS module.
// Notes:
//  - The code included here is only an example of how to parse NMEA-0183 GPS records.
//  This module should perform sufficient error checking to insure that the GPS module
//  is functioning properly.
//  - Ideally, GPS aquisition should occur in it's own thread in order to return
//  the latest GPS fix to the requestor immediately (without blocking).  In non-thread
//  mode, this implementation will block until a new GPS fix has been aquired (or if a 
//  timeout occurs).
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/02/09  Martin D. Flynn
//     -This module now maintains its "stale" state.
//     -"gpsGetDiagnostics" now returns a structure.
//  2006/06/08  Martin D. Flynn
//     -Added support for parsing "$GPGSA" 
//      (the DOP values are not currently used for fix discrimination).
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
//     -Switched to generic thread access methods in 'tools/threads.h'
//     -Initial implementation of a 'power-save' mode feature that closes the GPS 
//      comport if the sampling interval is greater than a minute (or so).  The HP 
//      hw6945 turns off the GPS receiver when the comport has been closed - to save 
//      power.  This feature allows GPS tracking on the HP hw6945 to conserve power
//      (at the expense of some event accuracy).  Note: this feature is still under
//      development and may not currently produce the desired results if used.
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include "custom/log.h"
#include "custom/gps.h"
#include "custom/gpsmods.h"

#include "tools/stdtypes.h"
#include "tools/utctools.h"
#include "tools/strtools.h"
#include "tools/checksum.h"
#include "tools/comport.h"

#include "base/events.h"
#include "base/propman.h"
#include "base/statcode.h"

// ----------------------------------------------------------------------------

#if !defined(GPS_COM_DATA_FORMAT)
#  define GPS_COM_DATA_FORMAT           "8N1"
#endif

// ----------------------------------------------------------------------------

/* compiling with thread support? */
#if defined(GPS_THREAD)
//#  warning GPS thread support enabled
#  include "tools/threads.h"
#else
//#  warning GPS thread support disabled
#endif

// ----------------------------------------------------------------------------
// References:
//   "GPS Hackery": http://www.ualberta.ca/~ckuethe/gps/
//   "GPS Applications": http://www.codeproject.com/vb/net/WritingGPSApplications1.asp

// ----------------------------------------------------------------------------

/* default GPS port */
// This default value is not used if the GPS comport property is in effect.
// The real default value is specified in 'propman.c'
#if defined(TARGET_WINCE)
#  define DEFAULT_GPS_PORT      ""
#else
#  define DEFAULT_GPS_PORT      "TTYS2"
#endif

/* simjulator port name */
#define GPS_SIMULATOR_PORT      "sim"

/* GPS bps */
#define DEFAULT_GPS_SPEED       4800L // 8N1 assumed

/* maximum out-of-sync seconds between GPS time and system time */
// set this value to '0' to ignore sync'ing with system clock 
#define MAX_DELTA_CLOCK_TIME    20L

/* minimum out-of-sync seconds between GPS time and system time */
// delta must be at least this value to cause a system clock update
#define MIN_DELTA_CLOCK_TIME    5L

/* maximum number of allowed seconds in a $GP record 'cycle' */
// In most GPS receivers, the default 'cycle' is 1 second
#define GP_EXPIRE               5L

/* power-save threshold */
// if PROP_GPS_SAMPLE_RATE is >= this value, the GPS port will not be openned
// until 'gpsAquire' is called, and will be closed after a GPS fix is aquired.
#define POWER_SAVE_THRESHOLD    45L // seconds

// ----------------------------------------------------------------------------

/* control characters */
#define ASCII_XON               17 // DC1, CTL_Q
#define ASCII_XOFF              19 // DC3, CTL_S
// ----------------------------------------------------------------------------

#define SUPPORT_TYPE_GPRMC      0x0001 // should always be defined
#define SUPPORT_TYPE_GPGGA      0x0002 // altitude, HDOP
#define SUPPORT_TYPE_GPGSA      0x0004 // PDOP, HDOP, VDOP

// ----------------------------------------------------------------------------

#if defined(GPS_DEVICE_SIMULATOR)
static utBool                   gpsSimulator = utFalse;
#endif

static ComPort_t                gpsComPort;
static utBool                   gpsPortDebug;

static GPS_t                    gpsFixLast;
static GPS_t                    gpsFixUnsafe;

static double                   gpsLastPDOP             = GPS_UNDEFINED_DOP;
static double                   gpsLastHDOP             = GPS_UNDEFINED_DOP;
static double                   gpsLastVDOP             = GPS_UNDEFINED_DOP;
static TimerSec_t               gpsLastHDOPTimer        = 0L;

static utBool                   gpsIsStale              = utFalse;
static UInt32                   gpsSampleCount_A        = 0L; // valid
static UInt32                   gpsSampleCount_V        = 0L; // invalid
static UInt32                   gpsRestartCount         = 0L;
static TimerSec_t               gpsLastSampleTimer      = 0L;
static TimerSec_t               gpsLastValidTimer       = 0L;

static TimerSec_t               gpsLastReadErrorTimer   = 0L;
static TimerSec_t               gpsLastLostErrorTimer   = 0L;

static utBool                   gpsAquireRequest        = utFalse;
static UInt32                   gpsAquireTimeoutMS      = 0L;

#if defined(GPS_THREAD)
static utBool                   gpsRunThread = utFalse;
static threadThread_t           gpsThread;
static threadMutex_t            gpsMutex;
static threadMutex_t            gpsSampleMutex;
static threadMutex_t            gpsAquireMutex;
static threadCond_t             gpsAquireCond;
#define SAMPLE_LOCK             MUTEX_LOCK(&gpsSampleMutex);
#define SAMPLE_UNLOCK           MUTEX_UNLOCK(&gpsSampleMutex);
#define GPS_LOCK                MUTEX_LOCK(&gpsMutex);
#define GPS_UNLOCK              MUTEX_UNLOCK(&gpsMutex);
#define AQUIRE_LOCK             MUTEX_LOCK(&gpsAquireMutex);
#define AQUIRE_UNLOCK           MUTEX_UNLOCK(&gpsAquireMutex);
#define AQUIRE_WAIT             CONDITION_WAIT(&gpsAquireCond, &gpsAquireMutex);
#define AQUIRE_NOTIFY           CONDITION_NOTIFY(&gpsAquireCond);
#else
#define SAMPLE_LOCK     
#define SAMPLE_UNLOCK   
#define GPS_LOCK        
#define GPS_UNLOCK      
#define AQUIRE_LOCK             
#define AQUIRE_UNLOCK           
#define AQUIRE_WAIT             
#define AQUIRE_NOTIFY           
#endif

// ----------------------------------------------------------------------------

static GPSDiagnostics_t gpsStats = { 0L, 0L, 0L, 0L, 0L };

GPSDiagnostics_t *gpsGetDiagnostics(GPSDiagnostics_t *stats)
{
#if !defined(GPS_THREAD)
// These values will not be accurate if not running in thread mode
#  warning GPS is not running in thread mode, diagnostic values will not be accurate.
#endif
    GPSDiagnostics_t *s = stats? stats : &gpsStats;
    SAMPLE_LOCK {
        s->lastSampleTime = TIMER_TO_UTC(gpsLastSampleTimer);
        s->lastValidTime  = TIMER_TO_UTC(gpsLastValidTimer);
        s->sampleCount_A  = gpsSampleCount_A;
        s->sampleCount_V  = gpsSampleCount_V;
        s->restartCount   = gpsRestartCount;
    } SAMPLE_UNLOCK
    return s;
}

// ----------------------------------------------------------------------------

#if defined(TARGET_WINCE)
// skip handling of specific GPS receiver types
#else
static void _gpsConfigGarmin(ComPort_t *com)
{
    // This configures a Garmin receiver to send only GPRMC & GPGGA records,
    // and send them only once every 2 seconds.
    
    // disable all sentences
    comPortWriteString(com, "$PGRMO,,2\r\r\n");
    // "$PGRMO,,4" restores factory default sentences
    comPortFlush(com, 100L);
    
    // Fix mode: Automatic
    // Differential mode: Automatic
    comPortWriteString(com, "$PGRMC,A,,,,,,,,A\r\r\n");
    comPortFlush(com, 100L);
    
    // output every 2 seconds
    // automatic position averaging when stopped
    // NMEA-0183 v2.30 off
    // enable WAAS
    comPortWriteString(com, "$PGRMC1,2,,2,,,,1,W\r\r\n");
    // "$PGRMC1,1" set it back to 1/sec
    // "$PGRMC1E" queries current config
    comPortFlush(com, 100L);

    // enable GPRMC sentence
    comPortWriteString(com, "$PGRMO,GPRMC,1\r\r\n");
    comPortFlush(com, 100L);

    // enable GPGGA sentence
    comPortWriteString(com, "$PGRMO,GPGGA,1\r\r\n");
    comPortFlush(com, 100L);

    /* done */
    logDEBUG(LOGSRC,"Garmin GPS configured");

}
#endif

// ----------------------------------------------------------------------------

/* open gps serial port */
static utBool _gpsOpen()
{
    ComPort_t *com = &gpsComPort;
    comPortInitStruct(com);

    /* check port */
    const char *portName = propGetString(PROP_CFG_GPS_PORT, "");
    if (!portName || !*portName) { portName = DEFAULT_GPS_PORT; }
    
    /* simulator mode? */
#if defined(GPS_DEVICE_SIMULATOR)
    gpsSimulator = strEqualsIgnoreCase(portName,GPS_SIMULATOR_PORT);
    if (gpsSimulator) {
        // should not be here if we are running in simulator mode
        return utFalse;
    }
#endif

    /* speed */
    long bpsSpeed = (long)propGetUInt32(PROP_CFG_GPS_BPS, -1L);
    if (bpsSpeed <= 0L) { bpsSpeed = DEFAULT_GPS_SPEED; }

    /* open */
    if (comPortOpen(com,portName,bpsSpeed,GPS_COM_DATA_FORMAT,utFalse) == (ComPort_t*)0) {
        // The outer loop will retry the open later
        // Generally, this should not occur on the GumStix
        //logWARNING(LOGSRC,"Unable to open GPS port '%s' [%s]", portName, strerror(errno));
        logWARNING(LOGSRC,"Unable to open GPS port '%s'", portName);
        return utFalse;
    }
    logDEBUG(LOGSRC,"Opened GPS port: %s [%ld bps]", comPortName(com), bpsSpeed);
    threadSleepMS(500L); // wait for connection to settle

    /* comport logging */
    gpsPortDebug = propGetBoolean(PROP_CFG_GPS_DEBUG,utFalse);
#if defined(TARGET_WINCE)
    // TODO: implement comport logging feature
#else
    if (gpsPortDebug) {
        comPortSetDebugLogger(com, 0);
    }
#endif

    /* specific GPS device configuration */
#if defined(TARGET_WINCE)
    // skip handling of specific GPS receiver types
#else
    const char *receiver = propGetString(PROP_CFG_GPS_MODEL,"");
    if (strEqualsIgnoreCase(receiver,GPS_RECEIVER_GARMIN)) {
        // Garmin 15, 18PC
        _gpsConfigGarmin(com);
    }
#endif

    /* return success */
    return utTrue;

}

/* close gps serial port */
static void _gpsClose()
{
    comPortClose(&gpsComPort);
}

// ----------------------------------------------------------------------------

/* update system clock time */
static utBool gpsUpdateSystemClock(long fixtime)
{
    long delta = (long)propGetUInt32(PROP_GPS_CLOCK_DELTA, MAX_DELTA_CLOCK_TIME);
    
    /* don't update clock if 'delta' is '0' */
    if (delta <= 0L) {
        return utFalse;
    }
    
    /* minimum delta */
    if (delta < MIN_DELTA_CLOCK_TIME) {
        delta = MIN_DELTA_CLOCK_TIME;
    }
    
    /* check actual time delta */
    long d = labs(fixtime - utcGetTimeSec()); // setting systime to fixtime
    if (d <= delta) {
        // actual time delta is within acceptable range
        return utFalse;
    }

    /* never update if we're running the GPS simulator */
#if defined(GPS_DEVICE_SIMULATOR)
    if (gpsSimulator) {
        logDEBUG(LOGSRC,"System clock out-of-sync: %ld [delta %ld sec]", fixtime, d);
        return utFalse;
    }
#endif
    
    /* update clock */
    logDEBUG(LOGSRC,"System clock out-of-sync: %ld [delta %ld sec]", fixtime, d);
    utcSetTimeSec(fixtime);
    return utTrue;

}

// ----------------------------------------------------------------------------

/* parse YMD/HMS into UTC Epoch seconds */
static UInt32 _gpsGetUTCSeconds(UInt32 dmy, UInt32 hms)
{

    /* time of day [TOD] */
    int    HH  = (int)((hms / 10000L) % 100L);
    int    MM  = (int)((hms / 100L) % 100L);
    int    SS  = (int)(hms % 100L);
    UInt32 TOD = (HH * 3600L) + (MM * 60L) + SS;

    /* current UTC day */
    long DAY = 0L;
    if (dmy) {
        int    yy  = (int)(dmy % 100L) + 2000;
        int    mm  = (int)((dmy / 100L) % 100L);
        int    dd  = (int)((dmy / 10000L) % 100L);
        long   yr  = ((long)yy * 1000L) + (long)(((mm - 3) * 1000) / 12);
        DAY        = ((367L * yr + 625L) / 1000L) - (2L * (yr / 1000L))
                     + (yr / 4000L) - (yr / 100000L) + (yr / 400000L)
                     + (long)dd - 719469L;
    } else {
        // we don't have the day, so we need to figure out as close as we can what it should be.
        UInt32 utc = utcGetTimeSec();
        if (utc < MIN_CLOCK_TIME) {
            // the system clock time is not valid
            logWARNING(LOGSRC,"Current clock time is prior to minimum time! [%lu]", utc);
            DAY = 0L;
        } else {
            UInt32 tod = utc % DAY_SECONDS(1);
            DAY        = utc / DAY_SECONDS(1);
            Int32  dif = tod - TOD; // difference should be small (ie. < 1 hour)
            if (labs(dif) > HOUR_SECONDS(12)) { // 12 to 18 hours
                // > 12 hour difference, assume we've crossed a day boundary
                if (tod > TOD) {
                    // tod > TOD likely represents the next day
                    DAY++;
                } else {
                    // tod < TOD likely represents the previous day
                    DAY--;
                }
            }
        }
    }
    
    /* return UTC seconds */
    UInt32 sec = DAY_SECONDS(DAY) + TOD;
    return sec;
    
}

/* parse latitude */
static double getLatitude(const char *s, const char *d)
{
    double _lat = strParseDouble(s, 99999.0);
    if (_lat < 99999.0) {
        double lat = (double)((long)_lat / 100L); // _lat is always positive here
        lat += (_lat - (lat * 100.0)) / 60.0;
        return !strcmp(d,"S")? -lat : lat;
    } else {
        return 90.0; // invalid latitude
    }
}

/* parse longitude */
static double getLongitude(const char *s, const char *d)
{
    double _lon = strParseDouble(s, 99999.0);
    if (_lon < 99999.0) {
        double lon = (double)((long)_lon / 100L); // _lon is always positive here
        lon += (_lon - (lon * 100.0)) / 60.0;
        return !strcmp(d,"W")? -lon : lon;
    } else {
        return 180.0; // invalid longitude
    }
}

/* read a single line from the GPS receiver */
static int _gpsReadLine(ComPort_t *com, char *data, int dataSize, long timeoutMS)
{
#if defined(GPS_DEVICE_SIMULATOR)
#define SIM_LAT(X) (  37.0 + (X)) //  41
#define SIM_LON(X) (-140.0 - (X)) // -87
    if (gpsSimulator) {
        static int simCount = 0;
        threadSleepMS(1000L);
        double LL[][2] = {
//#ifdef NOT_DEF
            { SIM_LAT(0.10000), SIM_LON(0.10000) },
            { SIM_LAT(0.17000), SIM_LON(0.17000) },
            { SIM_LAT(0.18000), SIM_LON(0.18000) },
            { SIM_LAT(0.19000), SIM_LON(0.19000) },
            { SIM_LAT(0.20000), SIM_LON(0.20000) },
            { SIM_LAT(0.21000), SIM_LON(0.21000) },
            { SIM_LAT(0.30000), SIM_LON(0.30000) },
            { SIM_LAT(0.40000), SIM_LON(0.40000) },
            { SIM_LAT(0.41000), SIM_LON(0.41000) },
            { SIM_LAT(0.42000), SIM_LON(0.42000) },
            { SIM_LAT(0.43000), SIM_LON(0.43000) },
//#endif
            { SIM_LAT(0.50089), SIM_LON(0.56954) },
        };
        int LLCount = sizeof(LL) / sizeof(LL[0]);
        simCount++;
        char *d = data;
#if defined(TARGET_WINCE)
        SYSTEMTIME st;
        GetSystemTime(&st); // UTC
        int hr  = st.wHour, mn = st.wMinute, sc = st.wSecond;
        int dy  = st.wDay, mo = st.wMonth, yr = st.wYear % 100;
#else
        UInt32 tsc = utcGetTimeSec(); // - HOUR_SECONDS(8); // simulator
        struct tm *tmp = gmtime(&tsc);
        int hr  = tmp->tm_hour, mn = tmp->tm_min, sc = tmp->tm_sec;
        int dy  = tmp->tm_mday, mo = tmp->tm_mon + 1, yr = tmp->tm_year % 100;
#endif
        int kts = ((simCount % 20) <= 10)? 20 : 0; // knots
        int x   = ((simCount/1) % LLCount);
        double lat = ((100.0 * (long)LL[x][0]) + (60.0 * (LL[x][0] - (long)LL[x][0])));
        double lon = ((100.0 * (long)LL[x][1]) + (60.0 * (LL[x][1] - (long)LL[x][1])));
        if (lon < 0.0) { lon = -lon; }
        sprintf(d, "$GPRMC,");                      d += strlen(d);
        sprintf(d, "%02d%02d%02d", hr, mn, sc);     d += strlen(d);
        sprintf(d, ",A,");                          d += strlen(d);
        sprintf(d, "%f,N,", (float)lat);                   d += strlen(d);
        sprintf(d, "%f,W,", (float)lon);                   d += strlen(d);
        sprintf(d, "%d", kts);                      d += strlen(d);
        sprintf(d, ",108.52,");                     d += strlen(d);
        sprintf(d, "%02d%02d%02d", dy, mo, yr);     d += strlen(d);
        sprintf(d, ",,");                           d += strlen(d);
        ChecksumXOR_t cksum = 0x00;
        cksumCalcCharXOR(data+1,&cksum);
        sprintf(d, "*%02X", (int)cksum);            d += strlen(d);
        //logDEBUG(LOGSRC,"GPS SIM[%d]: %s", simCount, data);
        return (d - data);
    }
#endif
    return comPortReadLine(com, data, dataSize, timeoutMS);
}

/* read GPS fix */
// If 'timeoutMS' is 0L, this function does not return
static int _gpsReadGPSFix(UInt32 timeoutMS)
{
    ComPort_t *com = &gpsComPort;
    int dataLen = 0;
    char data[256];
    TimerSec_t startTimer = utcGetTimer();
    
    /* timeout */
    // This function will exit when one of the following occurs:
    //   - A valid GPRMC record is found (if timeout is in effect)
    //   - A timeout occurs
    //   - An error occurs
    UInt32 timeoutSec = (timeoutMS > 0L)? ((timeoutMS + 999L) / 1000L) : 0L;

    /* flush everything currently in the comport buffers */
    // (Try to make sure this GPS device sends only the '$GPRMC' sentence.)
    comPortFlush(com, 0L); // '0' means don't do any reading, just use 'tcflush'

    /* read/parse NMEA-0183 data */
    long readTimeoutMS  = 5000L;
    long accumTimeoutMS = 0L;
    while (utTrue) {

#if defined(GPS_THREAD)
        /* thread mode */
        if (!gpsRunThread) {
            // this thread has been requested to stop
            return 0; // stop thread
        }
#endif

        /* timeout mode */
        if ((timeoutSec > 0L) && utcIsTimerExpired(startTimer,timeoutSec)) {
            // timeout
            return 0; // timeout
        }

        /* have we read a "$GPRMC" record in the last GPS_EVENT_INTERVAL seconds? */
#if defined(GPS_THREAD)
        // [wait a few seconds before we start checking GPS errors (ie. skip first few passes)]
        if (utcIsTimerExpired(startTimer,60L)) {
            // If we've NEVER received a $GPRMC record, then 'gpsLastSampleTimer == 0L'.
            UInt32 gpsLostInterval = GPS_EVENT_INTERVAL;
            if (utcIsTimerExpired(gpsLastSampleTimer,gpsLostInterval)) {
                // We haven't receive *ANY* "$GPRMC" record in over GPS_EVENT_INTERVAL seconds!
                // One of the following may be the cause:
                //  - The GPS module is not attached.
                //  - The GPS module went to sleep (WindowsCE)
                //  - The GPS module died (or was disconnected).
                //  - The serial port connection is in a strange state.
                // [Note: a disconnected GPS antenna will NOT cause control to reach here.]
                // The best we can do is reopen the serial port and try again.
                if (utcIsTimerExpired(gpsLastLostErrorTimer,600L)) {
                    if (gpsLastSampleTimer == 0L) {
                        logERROR(LOGSRC,"No GPS communication: %lu", (gpsRestartCount + 1L));
                    } else {
                        logERROR(LOGSRC,"Lost GPS communication: %lu", (gpsRestartCount + 1L));
                    }
                    gpsLastLostErrorTimer = utcGetTimer();
                }
                return -1; // error
            }
        }
#endif

        /* read data */
        dataLen = _gpsReadLine(com, data, sizeof(data), readTimeoutMS);
        if (dataLen < 0) {
            // com port closed? (not likely on the GumStix)
            if (utcIsTimerExpired(gpsLastReadErrorTimer,600L)) {
                gpsLastReadErrorTimer = utcGetTimer();
                logWARNING(LOGSRC,"GPS read error: %lu", (gpsRestartCount + 1L));
            }
            return -1; // error
        } else
        if (dataLen == 0) {
            if (COMERR_IsTimeout(com)) {
                // TIMEOUT? (if the GPS module is in place, this should never occur)
                // This indicates that we have lost communication with the GPS.
                accumTimeoutMS += readTimeoutMS;
                continue;
            } else {
                // zero length record
                continue;
            }
        }

        /* we've read at least something, reset timeout */
        accumTimeoutMS = 0L;

        /* valid data? */
        if (data[0] != '$') {
            // we must have started in the middle of the sentence
            continue; // try again
        }
        
        /* DEBUG: display data */
#if defined(TARGET_WINCE)
        if (gpsPortDebug) {
            logDEBUG(LOGSRC,"%s", data);
        }
#else
        //logDEBUG(LOGSRC,"%s", data);
#endif

#if defined(TARGET_WINCE)
        // skip handling of specific GPS receiver types
#else
        /* unexpected sentences */
        if (strStartsWith(data, "$PG") ||    // <-- Garmin sentence
            strStartsWith(data, "$GPGSV")) { // <-- Satellite info sentence
            // reconfigure Garmin device
            //logINFO(LOGSRC,"Unexpected sentence: %s", data);
            const char *receiver = propGetString(PROP_CFG_GPS_MODEL,"");
            if (strEqualsIgnoreCase(receiver,GPS_RECEIVER_GARMIN)) {
                // If the serial port is configured for RX-only, then this section should
                // be commented out.  Otherwise the Garmin config strings will be continually
                // sent and may slow down communication with the receiver.
                _gpsConfigGarmin(com);
            } else {
                // don't know how to trim out these sentences
            }
            continue;
        }
#endif

        /* NMEA-0183 sentence */
        if (strStartsWith(data, "$GP")) {

            // http://www.scientificcomponent.com/nmea0183.htm
            // http://home.mira.net/~gnb/gps/nmea.html
            // $GPGGA - Global Positioning System Fix Data
            //   $GPGGA|015402.240|0000.0000|N|00000.0000|E|0|00|50.0|0.0|M|18.0|M|0.0|0000*4B|
            //   $GPGGA|025425.494|3509.0743|N|11907.6314|W|1|04|2.3|530.3|M|-21.9|M|0.0|0000*4D|
            //   $GPGGA,    1     ,    2    ,3,     4    ,5,6,7 , 8 ,  9 ,10,  11,12,13 , 14
            //      1   UTC time of position HHMMSS
            //      2   current latitude in ddmm.mm format
            //      3   latitude hemisphere ("N" = northern, "S" = southern)
            //      4   current longitude in dddmm.mm format
            //      5   longitude hemisphere ("E" = eastern, "W" = western)
            //      6   (0=no fix, 1=GPS, 2=DGPS, 3=PPS?, 6=dead-reckoning)
            //      7   number of satellites (00-12)
            //      8   Horizontal Dilution of Precision
            //      9   Height above/below mean geoid (above mean sea level, not WGS-84 ellipsoid height)
            //     10   Unit of height, always 'M' meters
            //     11   Geoidal separation (add to #9 to get WGS-84 ellipsoid height)
            //     12   Unit of Geoidal separation (meters)
            //     13   Age of differential GPS
            //     14   Differential reference station ID (always '0000')
            // $GPGLL - Geographic Position, Latitude/Longitude
            //   $GPGLL|36000.0000|N|72000.0000|E|015402.240|V*17|
            //   $GPGLL|3209.0943|N|11907.9313|W|025426.493|A*2F|
            //   $GPGLL,    1    ,2,     3    ,4,      5   ,6
            //      1    Current latitude
            //      2    North/South
            //      3    Current longitude
            //      4    East/West
            //      5    UTC in hhmmss format
            //      6    A=valid, V=invalid
            // $GPGSA - GPS DOP and Active Satellites
            //   $GPGSA|A|1|||||||||||||50.0|50.0|50.0*05|
            //   $GPGSA|A|3|16|20|13|23|||||||||4.3|2.3|3.7*36|
            //   $GPGSA,1,2, 3, 4, 5, 6,  ...    15, 16, 17
            //      1    A =Auto2D/3D, M=Forced2D/3D
            //      2    1=NoFix, 2=2D, 3=3D
            //      3    Satellites used #1
            //    ...
            //     14    Satellites used #12
            //     15    PDOP
            //     16    HDOP
            //     17    VDOP
            // $GPGSV - GPS Satellites in View
            //   $GPGSV|3|1|9|16|35|51|32|4|9|269|0|20|32|177|33|13|62|329|37*4E|
            //   $GPGSV|3|2|9|3|14|113|0|24|5|230|0|8|12|251|0|23|71|71|39*70|
            //   $GPGSV|3|3|9|131|0|0|0*43|
            // $GPRMC - Recommended Minimum Specific GPS/TRANSIT Data
            //   $GPRMC|015402.240|V|36000.0000|N|72000.0000|E|0.000000||200505||*3C|
            //   $GPRMC,025423.494,A,3709.0642,N,11907.8315,W,0.094824,108.52,200505,,*12
            //   $GPRMC,      1   ,2,    3    ,4,     5    ,6, 7      ,   8  ,  9   ,A,B*M
            //      1   UTC time of position HHMMSS
            //      2   validity of the fix ("A" = valid, "V" = invalid)
            //      3   current latitude in ddmm.mm format
            //      4   latitude hemisphere ("N" = northern, "S" = southern)
            //      5   current longitude in dddmm.mm format
            //      6   longitude hemisphere ("E" = eastern, "W" = western)
            //      7   speed in knots
            //      8   true course in degrees
            //      9   date in DDMMYY format
            //      A   magnetic variation in degrees
            //      B   direction of magnetic variation ("E" = east, "W" = west)
            //      M   checksum
            // $GPVTG - Track Made Good and Ground Speed
            //   $GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*41
            //             1  2    3  4    5  6    7  8
            //      1   True course made good over ground, degrees
            //      2   Magnetic course made good over ground, degrees
            //      3   Ground speed, N=Knots
            //      4   Ground speed, K=Kilometers per hour

            utBool validFix_GPRMC = utFalse;
            utBool validFix_GPGGA = utFalse;
            char fldData[256], *fld[22];
            memset(fld, 0, sizeof(fld));
            strncpy(fldData, data, sizeof(fldData) - 1); // local copy
            fldData[sizeof(fldData) - 1] = 0; // safety net terminator
            strParseArray(fldData, fld, 20);

            /* checksum (XOR sum of all characters between '$' and '*', exclusive) */
            if (!cksumIsValidCharXOR(&data[1],(int*)0)) {
                logWARNING(LOGSRC,"GPS record failed checksum: %s", fld[0]);
                continue; // skip this record
            }

            /* handle record type */
            // We cannot assume that the GPRMC will arrive before GPGGA, or visa-versa.
            // so we must handle either case.
            if (strEqualsIgnoreCase(fld[0],"$GPRMC")) {
#if defined(SUPPORT_TYPE_GPRMC)
                if (*fld[2] == 'A') {
                    // "A" - valid gps fix
                    
                    /* parse */
                    UInt32 hms       = strParseUInt32(fld[1], 0L);
                    UInt32 dmy       = strParseUInt32(fld[9], 0L);
                    UInt32 fixtime   = _gpsGetUTCSeconds(dmy, hms);
                    double latitude  = getLatitude (fld[3], fld[4]);
                    double longitude = getLongitude(fld[5], fld[6]);
                    double knots     = strParseDouble(fld[7], -1.0);
                    double heading   = strParseDouble(fld[8], -1.0);
                    double speedKPH  = (knots >= 0.0)? (knots * KILOMETERS_PER_KNOT) : -1.0;
                    
                    /* HDOP expired? */
                    if (utcIsTimerExpired(gpsLastHDOPTimer,60L)) {
                        gpsLastPDOP = GPS_UNDEFINED_DOP;
                        gpsLastHDOP = GPS_UNDEFINED_DOP;
                        gpsLastVDOP = GPS_UNDEFINED_DOP;
                        // do not reset timer
                    }
                    
                    /* save */
                    if (fixtime < MIN_CLOCK_TIME) {
                        
                        // The fixtime is invalid
                        char dt[32];
                        utcFormatDateTime(dt, fixtime);
                        logERROR(LOGSRC,"$GPRMC invalid fixtime: [%lu] %s UTC", fixtime, dt);
                        logERROR(LOGSRC,"GPS: %s", data);
                        
                    } else
                    if ((latitude  >=  90.0) || (latitude  <=  -90.0) ||
                        (longitude >= 180.0) || (longitude <= -180.0)   ) {
                            
                        // We have an valid record, but the lat/lon appears to be invalid!
                        logWARNING(LOGSRC,"$GPRMC invalid lat/lon: %.5lf/%.5lf", latitude, longitude);
                        
                    } else {
                        
                        GPS_LOCK {
                            // If the $GPGGA came first, we don't want to indiscriminately clear
                            // out the GPS structure before adding the $GPRMC data.
                            if ((gpsFixUnsafe.fixtype == 0)                  ||     // no GPS fix
                                (gpsFixUnsafe.fixtime < MIN_CLOCK_TIME)      ||     // uninitialized fixtime
                                (gpsFixUnsafe.fixtime > fixtime)             ||     // fixtime in the future
                                ((fixtime - gpsFixUnsafe.fixtime) > GP_EXPIRE) ) {  // expired fixtime
                                gpsClear(&gpsFixUnsafe);
                                gpsFixUnsafe.fixtype = 1; // GPS
                            } else
                            if ((gpsFixUnsafe.point.latitude  != latitude ) ||      // latitude  changed (no GGA?)
                                (gpsFixUnsafe.point.longitude != longitude)   ) {   // longitude changed (no GGA?)
                                gpsClear(&gpsFixUnsafe);
                                gpsFixUnsafe.fixtype = 1; // GPS
                            } else {
                                // I will be adding to the existing data
                            }
                            gpsFixUnsafe.ageTimer        = utcGetTimer();
                            gpsFixUnsafe.fixtime         = fixtime;
                            gpsFixUnsafe.speedKPH        = speedKPH;
                            gpsFixUnsafe.heading         = heading;
                            gpsFixUnsafe.point.latitude  = latitude;
                            gpsFixUnsafe.point.longitude = longitude;
                            gpsFixUnsafe.pdop            = gpsLastPDOP;
                            gpsFixUnsafe.hdop            = gpsLastHDOP;
                            gpsFixUnsafe.vdop            = gpsLastVDOP;
                            gpsFixUnsafe.nmea           |= NMEA0183_GPRMC;
                            if (gpsFixUnsafe.nmea & NMEA0183_GPGGA) {
                                gpsCopy(&gpsFixLast, &gpsFixUnsafe);
                            }
                        } GPS_UNLOCK
                        validFix_GPRMC = utTrue;

                        /* update system clock ($GPRMC records only!) */
                        gpsUpdateSystemClock(fixtime + 2L);

                    }
                            
                    /* count valid record type */
                    SAMPLE_LOCK { // locked to prevent external access
                        gpsSampleCount_A++;
                        if (gpsLastSampleTimer == 0L) {
                            logINFO(LOGSRC,"First $GPRMC record (A) @%lu", fixtime);
                        } 
                        gpsLastSampleTimer = utcGetTimer();
                        if (validFix_GPRMC) { gpsLastValidTimer = utcGetTimer(); }
                    } SAMPLE_UNLOCK

                } else {
                    // "V" - invalid gps fix
                        
                    /* count invalid record type */
                    SAMPLE_LOCK { // locked to prevent external access
                        gpsSampleCount_V++;
                        if (gpsLastSampleTimer == 0L) {
                            logINFO(LOGSRC,"First $GPRMC record (V) @%lu", utcGetTimeSec());
                        } 
                        gpsLastSampleTimer = utcGetTimer();
                    } SAMPLE_UNLOCK

                    // No valid fix aquired

                }
#endif
            } else
            if (strEqualsIgnoreCase(fld[0],"$GPGGA")) {
#if defined(SUPPORT_TYPE_GPGGA)
                if (*fld[6] != '0') {
                    // "1" = GPS
                    // "2" = DGPS

                    /* parse */
                    UInt32 hms       = strParseUInt32(fld[1], 0L);
                    UInt32 dmy       = 0L; // we don't know the day
                    UInt32 fixtime   = _gpsGetUTCSeconds(dmy, hms);
                    double latitude  = getLatitude (fld[2], fld[3]);
                    double longitude = getLongitude(fld[4], fld[5]);
                    gpsLastHDOP      = strParseDouble(fld[8], GPS_UNDEFINED_DOP);
                    int    fixtype   = (int)strParseUInt32(fld[6], 1L); // 1=GPS, 2=DGPS, 3=PPS?, ...
                    double altitude  = strParseDouble(fld[9], 0.0);

                    /* PDOP/VDOP expired? */
                    if (utcIsTimerExpired(gpsLastHDOPTimer,60L)) {
                        gpsLastPDOP = GPS_UNDEFINED_DOP;
                        gpsLastVDOP = GPS_UNDEFINED_DOP;
                    }
                    gpsLastHDOPTimer = utcGetTimer(); // reset timer

                    /* save */
                    if (fixtime < MIN_CLOCK_TIME) {
                        // The fixtime is invalid
                        // (this can occur if the system clock time hasn't yet been updated)
                        char dt[32];
                        utcFormatDateTime(dt, fixtime);
                        logINFO(LOGSRC,"$GPGGA invalid fixtime: [%lu] %s UTC", fixtime, dt);
                        logINFO(LOGSRC,"GPS: %s", data);
                    } else
                    if ((latitude  >=  90.0) || (latitude  <=  -90.0) ||
                        (longitude >= 180.0) || (longitude <= -180.0)   ) {
                        // We have an valid record, but the lat/lon appears to be invalid!
                        logWARNING(LOGSRC,"$GPGGA invalid lat/lon: %.5lf/%.5lf", latitude, longitude);
                    } else {
                        GPS_LOCK {
                            // If the $GPRMC came first, we don't want to indiscriminately clear
                            // out the GPS structure before adding the $GPGGA data.
                            if ((gpsFixUnsafe.fixtype == 0)                  ||      // no GPS fix
                                (gpsFixUnsafe.fixtime < MIN_CLOCK_TIME)      ||      // uninitialized fixtime
                                (gpsFixUnsafe.fixtime > fixtime)             ||      // fixtime in the future
                                ((fixtime - gpsFixUnsafe.fixtime) > GP_EXPIRE) ) {   // expired fixtime
                                gpsClear(&gpsFixUnsafe);
                            } else
                            if ((gpsFixUnsafe.point.latitude  != latitude ) ||      // latitude  changed (no GGA?)
                                (gpsFixUnsafe.point.longitude != longitude)   ) {   // longitude changed (no GGA?)
                                //gpsClear(&gpsFixUnsafe);
                            } else {
                                // I will be adding to the existing data
                            }
                            gpsFixUnsafe.ageTimer        = utcGetTimer();
                            gpsFixUnsafe.fixtime         = fixtime;
                            gpsFixUnsafe.altitude        = altitude;
                            gpsFixUnsafe.point.latitude  = latitude;
                            gpsFixUnsafe.point.longitude = longitude;
                            gpsFixUnsafe.fixtype         = fixtype;
                            gpsFixUnsafe.pdop            = gpsLastPDOP;
                            gpsFixUnsafe.hdop            = gpsLastHDOP;
                            gpsFixUnsafe.vdop            = gpsLastVDOP;
                            gpsFixUnsafe.nmea           |= NMEA0183_GPGGA;
                            if (gpsFixUnsafe.nmea & NMEA0183_GPRMC) {
                                gpsCopy(&gpsFixLast, &gpsFixUnsafe);
                            }
                        } GPS_UNLOCK
                        validFix_GPGGA = utTrue;
                    }
                            
                    /* count valid record type */
                    SAMPLE_LOCK { // locked to prevent external access to 'gpsLastValidTimer'
                        if (validFix_GPGGA) { gpsLastValidTimer = utcGetTimer(); }
                    } SAMPLE_UNLOCK

                }
#endif
            } else
            if (strEqualsIgnoreCase(fld[0],"$GPGSA")) {
#if defined(SUPPORT_TYPE_GPGSA)
                if ((*fld[2] != '2') || (*fld[2] != '3')) {
                    // "2" = 2D
                    // "3" = 3D

                    /* parse */
                    gpsLastPDOP = strParseDouble(fld[15], GPS_UNDEFINED_DOP);
                    gpsLastHDOP = strParseDouble(fld[16], GPS_UNDEFINED_DOP);
                    gpsLastVDOP = strParseDouble(fld[17], GPS_UNDEFINED_DOP);
                    gpsLastHDOPTimer = utcGetTimer();
                    
                    /* save */
                    GPS_LOCK {
                        gpsFixUnsafe.pdop  = gpsLastPDOP;
                        gpsFixUnsafe.hdop  = gpsLastHDOP;
                        gpsFixUnsafe.vdop  = gpsLastVDOP;
                        gpsFixUnsafe.nmea |= NMEA0183_GPGSA;
                    } GPS_UNLOCK
                    
                }
#endif
            } else {
                
                // unsupported record type
                
            }

            /* valid fix? */
            // return now if we're concerned about timeouts and we have a valid fix
            if ((timeoutSec > 0L) && validFix_GPRMC) {
                return 1; // ok
            }

        } else {

            // sentence does not start with "$GP..."

        }
             
    } // while - read gps data
    
    return 0; // timeout
    
}

// ----------------------------------------------------------------------------

#if defined(GPS_THREAD)

/* where the thread spends its time */
static void _gpsThreadRunnable(void *arg)
{
    const char *gpsPortName = propGetString(PROP_CFG_GPS_PORT, DEFAULT_GPS_PORT);

    /* simulator loop */
#if defined(GPS_DEVICE_SIMULATOR)
    gpsSimulator = strEqualsIgnoreCase(gpsPortName, GPS_SIMULATOR_PORT);
    if (gpsSimulator) {
        logWARNING(LOGSRC,"Starting GPS simulator thread");
        while (gpsRunThread) {
            _gpsReadGPSFix(0L); // no timeouts
        }
        logERROR(LOGSRC,"Stopping GPS simulator thread");
        threadExit();
        return;
    }
#endif

    /* non-simulator loop */
    TimerSec_t gpsLastOpenErrorTimer = 0L;
    while (gpsRunThread) {
        
        /* power-conservation */
        // The HP hw6945 turns off the GPS receiver when the comport is closed.  To conserve
        // power, we may want to wait here until the next 'gpsAquire(...)' is called.  This is
        // only checked if the GPS sample interval is fairly long (ie. greater than 2 minutes).
        // The trade-off is that with infrequent GPS sampling, arrivals/departures, etc, may
        // be delayed, or may even be missed entirely.
        UInt32 intervalSec        = propGetUInt32(PROP_GPS_SAMPLE_RATE, 15L);
        utBool powerSaveMode      = (intervalSec >= POWER_SAVE_THRESHOLD)? utTrue : utFalse;
        UInt32 powerSaveTimeoutMS = 0L;
        AQUIRE_LOCK {
            if (powerSaveMode) {
                // We're only sampling the GPS every POWER_SAVE_THRESHOLD seconds, or more.
                // Wait here unit 'gpsAquire(...)' tells us to continue
                logDEBUG(LOGSRC,"PowerSave - waiting for 'gpsAquire(...)' ...");
                while (!gpsAquireRequest && gpsRunThread) {
                    // wait for condition (ie. another thread requesting a GPS fix)
                    AQUIRE_WAIT
                }
                powerSaveTimeoutMS = gpsAquireTimeoutMS;
            }
            gpsAquireRequest = utFalse;
        } AQUIRE_UNLOCK
        // We are now either NOT in powerSaveMode, or 'gpsAquire(...)' has been called.
        
        /* exit now if this thread has been told to quit */
        if (!gpsRunThread) {
            break;
        }

        /* open port to GPS */
        if (!_gpsOpen()) {
            // Open failed.  Wait, then retry open.
            // This should be a critical error.
            if (utcIsTimerExpired(gpsLastOpenErrorTimer,900L)) {
                gpsPortName = propGetString(PROP_CFG_GPS_PORT, DEFAULT_GPS_PORT); // refresh name
                gpsLastOpenErrorTimer = utcGetTimer();
                if (!comPortIsValidName(gpsPortName)) {
                    logERROR(LOGSRC,"GPS port invalid: %s", gpsPortName);
                } else {
                    logCRITICAL(LOGSRC,"Unable to open GPS port: %s", gpsPortName); // strerror(errno));
                }
            }
            threadSleepMS(5000L);
            continue;
        }

        // The HP hw6945 GPS receiver should be powering up from a cold-start at this point.
        // If the device always keeps the GPS hot, then we will just be able to aquire a
        // fix much sooner.

        /* read GPS (forever, or until error) */
        // We want to make sure we stay in '_gpsReadGPSFix' long enough to get at least one
        // GPS fix (assuming that the receiver may be coming up from a cold-start).  If the 
        // GPS receiver is already hot, then we'll just get a fix much sooner.
        UInt32 timeoutMS = 0L;
        if (powerSaveMode) {
            // If we are in power-save mode, then 'gpsAquire(...)' was called and is waiting
            // for a valid GPS fix (hopefully with a reasonable timeout value).  We will wait
            // here for a GPS fix with the same timeout value.
            timeoutMS = powerSaveTimeoutMS; // from 'gpsAquireTimeoutMS'
            UInt32 MIN_TIMOUT = 90L * 1000L; // 90 seconds (should be enough for a cold GPS start)
            if (timeoutMS < MIN_TIMOUT) {
                timeoutMS = MIN_TIMOUT;
            }
        }
        int gpsErr = _gpsReadGPSFix(timeoutMS);
        if (gpsErr <= 0) {
            // (gpsErr <  0) we've received an error, increment restart counter
            // (gpsErr == 0) powerSaveMode timeout, increment restart counter
            SAMPLE_LOCK {
                gpsRestartCount++;
                logDEBUG(LOGSRC,"Error/Timeout - closing GPS port [%lu]", gpsRestartCount);
            } SAMPLE_UNLOCK
        } else {
            // (gpsErr >  0) powerSaveMode GPS fix received
            logDEBUG(LOGSRC,"PowerSave - closing GPS port ...");
        }

        /*close GPS port */
        _gpsClose();
        
    } //while (gpsRunThread)
    
    /* once this thread stops, it isn't starting again */
    // The following resources should be released:
    //  - gpsComPort
    //  - gpsMutex
    //  - gpsSampleMutex
    //  - gpsAquireMutex
    //  - gpsAquireCond
    //  - And everything in the supported GPS modules
    //  - ???
    logERROR(LOGSRC,"GPS thread is terminating ...");
    threadExit();    

}

/* indicate thread should stop */
static void _gpsStopThread(void *arg)
{
    gpsRunThread = utFalse;
    AQUIRE_LOCK {
        // nudge thread
        AQUIRE_NOTIFY
    } AQUIRE_UNLOCK
    _gpsClose();
}

/* start the thread */
static utBool _gpsStartThread()
{
    /* create thread */
    gpsRunThread = utTrue; // must set BEFORE we start the thread!
    if (threadCreate(&gpsThread,&_gpsThreadRunnable,0,"GPS") == 0) {
        // thread started successfully
        threadAddThreadStopFtn(&_gpsStopThread,0);
    } else {
        logCRITICAL(LOGSRC,"Unable to start GPS thread");
        gpsRunThread = utFalse;
    }
    return gpsRunThread;
}

#endif // defined(GPS_THREAD)

// ----------------------------------------------------------------------------

/* GPS module initialization */
static utBool _gpsDidInit = utFalse;
void gpsInitialize(eventAddFtn_t queueEvent)
{

    /* already initialized? */
    if (_gpsDidInit) {
        return;
    }
    _gpsDidInit = utTrue;

    /* init gps comport */
    comPortInitStruct(&gpsComPort);

    /* clear gps struct */
    gpsClear(&gpsFixLast);
    gpsClear(&gpsFixUnsafe);
    
#if defined(GPS_THREAD)
    /* create mutex's */
    threadMutexInit(&gpsMutex);
    threadMutexInit(&gpsSampleMutex);
    threadMutexInit(&gpsAquireMutex);
    threadConditionInit(&gpsAquireCond);
#endif

    /* GPS module initialization */
    gpsModuleInitialize(queueEvent);

#if defined(GPS_THREAD)
    /* start thread */
    _gpsStartThread();
#endif

}

// ----------------------------------------------------------------------------

/* set flag indicating GPS fix is stale (or not) */
void gpsSetFixStale(utBool stale)
{
    // this value is currently only set in 'mainloop.c'
    gpsIsStale = stale;
}

/* return true if current GPS fix is stale */
utBool gpsIsFixStale()
{
    // Note: this may be accessed from multiple threads, but since this is not
    // a critical value, no locking is performed.
    return gpsIsStale;
}

// ----------------------------------------------------------------------------

/* check minimum values in GPS record */
GPS_t *gpsCheckMinimums(GPS_t *gps)
{
    if (gps) {
        // check minimum speed 
        double minSpeedKPH = propGetDouble(PROP_GPS_MIN_SPEED, 7.0);
        if (gps->speedKPH < minSpeedKPH) {
            // the reported speed does not meet our minimum requirement
            // mark the speed as 'not moving'
            gps->speedKPH = 0.0;
            gps->heading  = 0.0; // clear the heading when the speed is '0'
        } 
    }
    return gps;
}

// ----------------------------------------------------------------------------

/* aquire GPS fix */
GPS_t *gpsAquire(GPS_t *gps, UInt32 timeoutMS)
{

    /* null gps pointer specified */
    if (!gps) {
        return (GPS_t*)0;
    }
    
    /* clear gps point */
    gpsClear(gps);

#if defined(GPS_THREAD)

    /* no timeout, return last fix */
    if (timeoutMS <= 0L) {
        // no timeout specified, just return the latest fix that we have (if any)
        // the caller can determine if he wants to use the fix
        return gpsGetLastGPS(gps, -1);
    }
    
    /* indicate to GPS thread that we want a fix */
    // This is only necessary if the gps interval is long and the gps thread is
    // waiting to be told to make a gps aquisition.
    AQUIRE_LOCK {
        gpsAquireTimeoutMS = timeoutMS; // this value is > 0
        gpsAquireRequest   = utTrue;
        AQUIRE_NOTIFY
    } AQUIRE_UNLOCK
    // at this point, the gps thread should be working on getting a fix

    /* wait until a fix is available */
    UInt32 accumTimeoutMS = 0L;
    for (;accumTimeoutMS < timeoutMS;) {
        
        /* get latest fix */
        GPS_t *g = gpsGetLastGPS(gps, -1);
        if (g && (utcGetTimerAgeSec(g->ageTimer) <= 7L)) {
            // The latest fix occurred within the last 7 seconds.
            return g;
        }
        
        /* wait for next fix */
        UInt32 tmo = timeoutMS - accumTimeoutMS;
        if (tmo > 1000L) { tmo = 1000L; }
        threadSleepMS(tmo);
        accumTimeoutMS += tmo;

    }

    /* no fix (timeout?) */
    return (GPS_t*)0;

#else

    /* timeout mode: block while reading GPS fix */
    UInt32 tmo = (timeoutMS < 3000L)? 3000L : timeoutMS;
#if defined(GPS_DEVICE_SIMULATOR)
    const char *gpsPortName = propGetString(PROP_CFG_GPS_PORT, DEFAULT_GPS_PORT);
    gpsSimulator = strEqualsIgnoreCase(gpsPortName, GPS_SIMULATOR_PORT);
    if (gpsSimulator) {
        int rtn = _gpsReadGPSFix(tmo);
        if (rtn > 0) {
            // valid fix aquired
            return gpsGetLastGPS(gps, 15);
        } else {
            // timeout
            return (GPS_t*)0;
        }
    } else
#endif
    if (comPortIsOpen(&gpsComPort) || _gpsOpen()) {
        int rtn = _gpsReadGPSFix(tmo);
        _gpsClose(); // always close the port when runnin in non-thread mode
        if (rtn > 0) {
            // valid fix aquired (at least $GPRMC)
            GPS_t *g = gpsGetLastGPS(gps, 15);
            return g;
        } else
        if (rtn < 0) {
            // error
            return (GPS_t*)0;
        } else {
            // timeout 
            return (GPS_t*)0;
        }
    } else {
        // unable to open GPS port
        return (GPS_t*)0;
    }

#endif // defined(GPS_THREAD)

}

/* get last aquired GPS fix */
GPS_t *gpsGetLastGPS(GPS_t *gps, Int16 maxAgeSec)
{
    // no blocking
    if (gps) {
        
        /* get latest fix */
        GPS_LOCK {
            // gpsFixLast   - contains the most recent fix that has all the GPS components
            // gpsFixUnsafe - contains the fix-in-progress
            if (gpsFixUnsafe.fixtime < (gpsFixLast.fixtime + GP_EXPIRE)) {
                // The last fix is within 5 seconds of the new fix-in-process.
                // During normal GPS aquisition, the fix times will likely be the same.
                // If not the same, then the fix-in-process may have just started on 
                // the next sample.
                memcpy(gps, &gpsFixLast, sizeof(GPS_t));
            } else
            if (gpsFixUnsafe.nmea & NMEA0183_GPRMC) {
                // The last fix ('gpsFixLast') is stale.
                // The fix-in-process at least has the GPRMC
                // One of the following may have occured:
                //  - We stopped getting GPGGA records (or don't get them).
                //  - We just started getting valid GPS fixes again and we happen to have been 
                //    called when we are between GPRMC/GPGGA records (unlikely, but possible).
                //logDEBUG(LOGSRC,"We don't have a valid $GPGGA record"); 
                memcpy(gps, &gpsFixUnsafe, sizeof(GPS_t));
            } else {
                // The fix-in-process doesn't have a GPRMC.
                // The last fix is stale, but return it anyway.
                memcpy(gps, &gpsFixLast, sizeof(GPS_t)); 
                // fix may still be invalid
            }
        } GPS_UNLOCK
        
        /* check fix */
        if (!gpsIsValid(gps)) {
            //logINFO(LOGSRC,"Point is invalid ...");
            return (GPS_t*)0; // GPSPoint is invalid
        } else
        if ((maxAgeSec > 0) && (utcGetTimerAgeSec(gps->ageTimer) > maxAgeSec)) {
            //logINFO(LOGSRC,"Point is stale ... [%ld]", utcGetTimerAgeSec(gps->ageTimer));
            return (GPS_t*)0; // GPSPoint is stale
        } else {
            return gpsCheckMinimums(gps);
        }
        
    } else {
        
        /* no place to put fix */
        return (GPS_t*)0;
        
    }
}

// ----------------------------------------------------------------------------

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
//  Main GPS aquisition/process loop.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/02/09  Martin D. Flynn
//     -Improved "stale" GPS fix checking
//  2006/04/10  Martin D. Flynn
//     -Send event on first GPS fix aquisition
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "custom/log.h"
#include "custom/startup.h"
#include "custom/transport.h"
#include "custom/gps.h"
#include "custom/gpsmods.h"

#include "tools/stdtypes.h"
#include "tools/utctools.h"
#include "tools/gpstools.h"
#include "tools/strtools.h"

#include "modules/motion.h"
#include "modules/odometer.h"

#include "base/propman.h"
#include "base/statcode.h"
#include "base/mainloop.h"

#include "base/accting.h"
#include "base/protocol.h"
#include "base/packet.h"
#include "base/events.h"
// ----------------------------------------------------------------------------

// comment to disable the ability to run the main-loop in a separate thread
#define MAIN_THREAD

// ----------------------------------------------------------------------------

static TimerSec_t               lastGPSAquisitionTimer = (TimerSec_t)0L;
static TimerSec_t               lastModuleCheckTimer = (TimerSec_t)0L;
static GPS_t                    lastValidGPSFix; // needs to be initialized

static TimerSec_t               gpsStaleTimer = (TimerSec_t)0L;

static eventAddFtn_t            ftnQueueEvent = 0;

static PacketEncoding_t         defaultEncoding = 0;

// ----------------------------------------------------------------------------

static utBool                   mainRunThread = utTrue;
#ifdef MAIN_THREAD
#include "tools/threads.h"
static threadThread_t           mainThread;
#endif

// ----------------------------------------------------------------------------

#define STANDARD_LOOP_DELAY     1000L               // millis
#define FAST_LOOP_DELAY         20L                 // millis
#define LOOP_DELAY_INCREMENT    30L                 // millis

// ----------------------------------------------------------------------------

/* main loop module initialization */
static utBool didInitialize = utFalse;
void mainLoopInitialize(eventAddFtn_t queueEvent)
{
    
    /* save pointer to AddEventPacket function */
    ftnQueueEvent = queueEvent;

    /* last valid GPS location */
    gpsClear(&lastValidGPSFix);
    lastGPSAquisitionTimer = (TimerSec_t)0L;
    lastModuleCheckTimer = (TimerSec_t)0L;
    gpsStaleTimer = (TimerSec_t)0L;
    gpsSetFixStale(utFalse);

    /* module initialization */
    // GPS initialization first
    gpsInitialize(ftnQueueEvent); // threaded
    // this may start many threads

    /* primary transport initialization */

    /* primary protocol/transport initialization */
    protocolInitialize(0, XPORT_INIT_PRIMARY());
    /* did initialize */
    didInitialize = utTrue;
    
}

// ----------------------------------------------------------------------------

/* add a motion event to the event queue */
static void _queueMotionEvent(PacketPriority_t priority, StatusCode_t code, const GPS_t *gps)
{
    if (gps && ftnQueueEvent) {
        Event_t evRcd;
        evSetEventDefaults(&evRcd, code, 0L, gps);
        (*ftnQueueEvent)(priority, DEFAULT_EVENT_FORMAT, &evRcd);
    }
}

// ----------------------------------------------------------------------------

#ifdef MAIN_THREAD
/* indicate main thread should stop */
static void _mainRunLoopStop(void *arg)
{
    mainRunThread = utFalse;
}
#endif

// GPS aquisition & data transmission loop
static void _mainLoopRun(void *arg);
utBool mainLoopRun(PacketEncoding_t dftEncoding, utBool runInThread)
{
    
    /* initialize */
    defaultEncoding = dftEncoding;

    /* start main loop */
    mainRunThread = utTrue;
#ifdef MAIN_THREAD
    if (runInThread) {
        if (threadCreate(&mainThread,&_mainLoopRun,0,"MainLoop") == 0) {
            // thread started successfully
            threadAddThreadStopFtn(&_mainRunLoopStop,0);
            return utTrue;
        } else {
            logCRITICAL(LOGSRC,"Unable to create main thread!!");
            mainRunThread = utFalse;
            return utFalse;
        }
    } else {
        /* main loop */
        _mainLoopRun((void*)0);
        // does not return (unless requested to stop)
        return utFalse;
    }
#else
    /* main loop */
    _mainLoopRun((void*)0);
    // does not return (unless requested to stop)
    return utFalse;
#endif

}

// GPS aquisition & data transmission loop
static void _mainLoopRun(void *arg)
{

    /* loop */
    long loopDelayMS = STANDARD_LOOP_DELAY;
    for (;mainRunThread;) {
        
        /* aquire GPS */
        UInt32 gpsInterval = propGetUInt32(PROP_GPS_SAMPLE_RATE, 15L);
        if (utcIsTimerExpired(lastGPSAquisitionTimer,gpsInterval)) {
            // aquire GPS fix 
            UInt32 gpsAquireTimeoutSec = propGetUInt32(PROP_GPS_AQUIRE_WAIT, 0L);
            GPS_t newFix, *gps = gpsAquire(&newFix, gpsAquireTimeoutSec);
            if (gps && gpsPointIsValid(&(gps->point)) && (lastValidGPSFix.fixtime != gps->fixtime)) {
                // We've received a new valid GPS fix
                // NOTES: 
                // - PROP_GPS_ACCURACY is not currently taken into account
                // first GPS fix?
                if (lastValidGPSFix.fixtime == 0L) {
                    // Send 'Initialization' event on first fix after boot-up
                    logINFO(LOGSRC,"First GPS fix: %.5lf/%.5lf", newFix.point.latitude, newFix.point.longitude);
                    _queueMotionEvent(PRIORITY_NORMAL, STATUS_INITIALIZED, &newFix);
                }
                // check GPS rules
                lastGPSAquisitionTimer = utcGetTimer();
                lastModuleCheckTimer = lastGPSAquisitionTimer;
                gpsModuleCheckEvents(&lastValidGPSFix, &newFix);
                gpsCopy(&lastValidGPSFix, &newFix);
                // was GPS fix previously stale?
                if (gpsIsFixStale()) {
                    // GPS is no longer stale
                    logDEBUG(LOGSRC,"GPS fix is now up to date ...");
                    gpsSetFixStale(utFalse);
                    gpsStaleTimer = (TimerSec_t)0L;
                } else {
                    // still not stale 
                }
            } else {
                // This fix was invalid, check for stale
                //logDEBUG(LOGSRC,"No new/valid fix ...");
                if (!gpsIsFixStale()) {
                    // We've not received a valid GPS fix, however the last GPS fix (if any)
                    // is not yet considered "stale".
                    UInt32 gpsExpireInterval = propGetUInt32(PROP_GPS_EXPIRATION, 360L);
                    if (gpsExpireInterval <= 0L) {
                        // The GPS fix is never considered "stale"
                    } else
                    if (gpsPointIsValid(&(lastValidGPSFix.point))) {
                        // We have previously received at least 1 valid GPS fix.  Set the timer to
                        // the last valid fix, and compare the age of the fix to the GPS expiration
                        // interval.
                        gpsStaleTimer = lastValidGPSFix.ageTimer;
                        if (utcIsTimerExpired(gpsStaleTimer,gpsExpireInterval)) {
                            // The timer has expired, we're now "stale"
                            // Likely causes: (most likely to least likely)
                            //   1) GPS antenna is obstructed (garage, building, etc.)
                            //   2) GPS antenna has been disconnected/damaged.
                            //   3) GPS receiver communication link has been disconnected.
                            //   4) GPS receiver has become defective.
                            // The last 2 can be ruled out by checking to see if we've received anything
                            // at all from the GPS receiver, even an invalid (type 'V') record.
                            gpsSetFixStale(utTrue);
                        }
                    } else
                    if (gpsStaleTimer <= 0L) {
                        // We've never received a valid GPS fix, and this is our first invalid fix.
                        // This is a likely ocurrance when the system has just been powered up,
                        // since the GPS receiver may not have had enough time to aquire a fix.
                        // Start the GPS expiration timer.  The interval "PROP_GPS_EXPIRATION" should
                        // be at least long enough to allow the GPS receiver to make a valid 
                        // aquisition after a cold-start.
                        gpsStaleTimer = utcGetTimer();
                        // If a valid fix is not aquired within the expiration interval, then the
                        // GPS receiver will be considered stale.
                    } else
                    if (utcIsTimerExpired(gpsStaleTimer,gpsExpireInterval)) {
                        // We've never received a valid GPS fix, and now the timer has expired.
                        // Likely causes: (most likely to least likely)
                        //   1) Device restarted while GPS antenna is obstructed (garage, building, etc.)
                        //   2) GPS antenna was never attached.
                        //   3) GPS receiver was never attached.
                        //   4) GPS receiver serial port was improperly specified.
                        // The last 2 can be ruled out by checking to see if we've received anything
                        // at all from the GPS receiver, even an invalid (type 'V') record.
                        gpsSetFixStale(utTrue);
                    }
                    // is GPS fix now considered "stale"?
                    if (gpsIsFixStale()) {
                        // GPS fix expired, now "stale"
                        // Client needs to decide what to do in this case
                        // Possible actions:
                        //   1) Queue up a ERROR_GPS_EXPIRED error
                        logDEBUG(LOGSRC,"****** GPS fix is expired ... ******");
                        // ('protocol.c' now sends this error if GPS fix is stale - see 'gpsIsFixStale')
                    } else {
                        // not yet stale
                    }
                } else {
                    // GPS fix is still stale.
                }
                // make sure we check GPS rules periodically even if we don't have a fix
                if (utcIsTimerExpired(lastModuleCheckTimer,2L*gpsInterval)) {
                    lastModuleCheckTimer = utcGetTimer();
                    gpsModuleCheckEvents(&lastValidGPSFix, (GPS_t*)0);
                }
            }
        }
        
        // -----------------
        // misc housekeeping items should go here
        startupMainLoopCallback();
        
        // -----------------
        // time to transmit? (we have data and/or minimum times have expired)
        // If the protocol transport is NOT running in a separate thread, this
        // function will block until the connection is closed.
        protocolTransport(0,defaultEncoding);
        // -----------------
        // short loop delay
        threadSleepMS(loopDelayMS);
        if (loopDelayMS < STANDARD_LOOP_DELAY) {
            loopDelayMS += LOOP_DELAY_INCREMENT;
            if (loopDelayMS > STANDARD_LOOP_DELAY) {
                loopDelayMS = STANDARD_LOOP_DELAY;
            }
        }

    }
    
    // only reaches here when thread terminates
    // Note: this _may_ be the *main* thread
    logERROR(LOGSRC,"MainLoop thread is terminating ...");
    threadExit();
    
}

// ----------------------------------------------------------------------------

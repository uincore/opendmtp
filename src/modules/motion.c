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
//  GPS fix checking for motion events.
//  Examines changes in GPS fix data and generates motion events accordingly.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/23  Martin D. Flynn
//     -Init dormant timer at first non-motion GPS fix
//  2006/02/13  Martin D. Flynn
//     -Changed stop event checking to include an option to have the stop event
//      time set to the time the vehicle actually stopped.  This also effects the
//      behaviour of the in-motion message to sending in-motion events iff the 
//      vehicle is actually moving at the time the event is generated.  (See
//      property PROP_MOTION_STOP_TYPE).
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
//  2007/03/11  Martin D. Flynn
//     -Modified 'stop' checking to allow for detection of other 'stop' indicators,
//      such as an indication that the ignition has been turned off.
//  2007/04/28  Martin D. Flynn
//     -Added a 5kph setback to the excess-speed detection and event generation.
//      Example: If a 100 kph excess speed is triggered, the vehicle must slow to
//      below 95 kph to reset the excess speed indicator.
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom/log.h"
#include "custom/gps.h"

#include "tools/stdtypes.h"
#include "tools/gpstools.h"
#include "tools/utctools.h"

#include "base/propman.h"
#include "base/statcode.h"
#include "base/events.h"

#include "modules/motion.h"

// ----------------------------------------------------------------------------

#define MOTION_START_PRIORITY       PRIORITY_NORMAL
#define MOTION_STOP_PRIORITY        PRIORITY_NORMAL
#define IN_MOTION_PRIORITY          PRIORITY_LOW
#define DORMANT_PRIORITY            PRIORITY_LOW
#define EXCESS_SPEED_PRIORITY       PRIORITY_NORMAL
#define MOVING_PRIORITY             PRIORITY_NORMAL

// ----------------------------------------------------------------------------

#if defined(DEBUG_COMPILE)
// no minimum when compiling for testing purposes
#define MIN_IN_MOTION_INTERVAL      0L                  // seconds
#define MIN_DORMANT_INTERVAL        0L                  // seconds

#elif defined(TRANSPORT_MEDIA_NONE)
// no minimum when sending to a file
#define MIN_IN_MOTION_INTERVAL      0L                  // seconds
#define MIN_DORMANT_INTERVAL        0L                  // seconds

#elif defined(TRANSPORT_MEDIA_FILE)
// no minimum when sending to a file
#define MIN_IN_MOTION_INTERVAL      0L                  // seconds
#define MIN_DORMANT_INTERVAL        0L                  // seconds

#elif defined(TRANSPORT_MEDIA_SERIAL)
// small minimum when sending to bluetooth
#define MIN_IN_MOTION_INTERVAL      20L                 // seconds
#define MIN_DORMANT_INTERVAL        20L                 // seconds

#else
// set default minimum when sending to server
#define MIN_IN_MOTION_INTERVAL      60L                 // seconds
#define MIN_DORMANT_INTERVAL        MINUTE_SECONDS(5)   // seconds

#endif

#define EXCESS_SPEED_SETBACK        5.0                 // kph

// ----------------------------------------------------------------------------

static utBool               motionInit  = utFalse;

static GPS_t                lastMotionFix;              // needs to be initialized
static GPS_t                lastStoppedFix;             // needs to be initialized

static utBool               isInMotion                  = utFalse;
static utBool               isExceedingSpeed            = utFalse;

static TimerSec_t           lastStoppedTimer            = 0L;

static TimerSec_t           lastInMotionMessageTimer    = 0L;

static TimerSec_t           lastMovingMessageTimer      = 0L;

static TimerSec_t           lastDormantMessageTimer     = 0L;
static UInt32               dormantCount                = 0L;

static eventAddFtn_t        ftnQueueEvent = 0;

static threadMutex_t        motionMutex;
#define MOTION_LOCK         MUTEX_LOCK(&motionMutex);
#define MOTION_UNLOCK       MUTEX_UNLOCK(&motionMutex);

// ----------------------------------------------------------------------------

/* initialize motion module */
void motionInitialize(eventAddFtn_t queueEvent)
{
    
    /* init motion */
    if (!motionInit) {
        motionInit = utTrue;
        
        /* event handler */
        ftnQueueEvent = queueEvent;
    
        /* clear GPS points */
        gpsClear(&lastMotionFix);
        gpsClear(&lastStoppedFix);
    
        /* init motion mutex */
        threadMutexInit(&motionMutex);
        
    }

}

/* reset moving message timer */
void motionResetMovingMessageTimer()
{
    MOTION_LOCK {
        lastMovingMessageTimer = 0L;
    } MOTION_UNLOCK
}

// ----------------------------------------------------------------------------

/* add a motion event to the event queue */
static void _queueMotionEvent(PacketPriority_t priority, StatusCode_t code, UInt32 timestamp, const GPS_t *gps)
{
    
    /* make sure we have a GPS fix */
    GPS_t gpsFix;
    const GPS_t *gpsf = gps? gps : gpsGetLastGPS(&gpsFix, 0);
    
    /* init event */
    Event_t evRcd;
    evSetEventDefaults(&evRcd, code, timestamp, gpsf);
    
    /* queue event */
    if (ftnQueueEvent) {
        (*ftnQueueEvent)(priority, DEFAULT_EVENT_FORMAT, &evRcd);
    }

}

// ----------------------------------------------------------------------------

/* stop motion */
static void _motionStop(UInt32 nowTime, const GPS_t *newFix)
{
    // NOTE: 'isInMotion' is assumed to be true when this function is called!
    // ('isInMotion' implies that we are tracking start/stop)

    /* set to officially NOT moving */
    isInMotion = utFalse;
    
    /* send 'stop' event */
    UInt16 defStopType = (UInt16)propGetUInt32(PROP_MOTION_STOP_TYPE, 0); // 0=after_delay, 1=when_stopped
    UInt32 stoppedTime = nowTime;
    const GPS_t *stoppedGPS = (GPS_t*)0;
    if (defStopType == MOTION_STOP_AFTER_DELAY) {
        // send 'stop' message with latest gps fix.
        stoppedTime = nowTime;
        stoppedGPS  = newFix;
    } else
    if (defStopType == MOTION_STOP_WHEN_STOPPED) {
        // send 'stop' message with the last gps at the time we actually detected the stop.
        stoppedTime = lastStoppedTimer? TIMER_TO_UTC(lastStoppedTimer) : nowTime;
        stoppedGPS  = gpsIsValid(&lastStoppedFix)? &lastStoppedFix : newFix;
    } else {
        // default: send 'stop' message with latest gps fix.
        stoppedTime = nowTime;
        stoppedGPS  = newFix;
    }
    _queueMotionEvent(MOTION_STOP_PRIORITY, STATUS_MOTION_STOP, stoppedTime, stoppedGPS);

    /* reset stopped fix */
    gpsClear(&lastStoppedFix);
    lastStoppedTimer = (TimerSec_t)0L;
    
}

/* check new GPS fix for various motion events */
static void _motionCheckGPS(const GPS_t *oldFix, const GPS_t *newFix)
{
    
    /* 'start' definition */
    // defStartType:
    //   0 - check GPS speed (kph)
    //   1 - check GPS distance (meters)
    //   2 - check OBC speed (kph) - if available
    UInt16 defStartType = (UInt16)propGetUInt32(PROP_MOTION_START_TYPE, 0);
    double defMotionStart = propGetDouble(PROP_MOTION_START, 0.0); // kph/meters
    
    /* speed */
    double speedKPH = 0.0;
    if (newFix) {
        speedKPH = newFix->speedKPH;
    }

    /* check motion start/stop */
    //   - send start/stop/in-motion event
    // PROP_GPS_MIN_SPEED should already be accounted for
    UInt32 nowTime = utcGetTimeSec();
    utBool isCurrentlyMoving = utFalse;
    if (defMotionStart > 0.0) { // kph/meters
        
        /* check for first GPS motion fix */
        if (!gpsIsValid(&lastMotionFix)) {
            // first GPS fix
            gpsCopy(&lastMotionFix, newFix);
        }

        /* check for currently moving */
        if (defStartType == MOTION_START_GPS_METERS) { // GPS distance
            // check GPS distance (meters)
            if (gpsIsValid(newFix) && gpsIsValid(&lastMotionFix)) { // validate
                double deltaMeters = gpsMetersToPoint(&(newFix->point), &(lastMotionFix.point));
                if (deltaMeters >= defMotionStart) {
                    isCurrentlyMoving = utTrue;
                }
            }
        } else
        if (speedKPH >= defMotionStart) { // (speedKPH > 0.0)
            isCurrentlyMoving = utTrue;
        }

        /* check for currently moving */
        if (isCurrentlyMoving) {

            // I am moving
            lastStoppedTimer = (TimerSec_t)0L; // reset stopped time
            gpsClear(&lastStoppedFix); // we're moving, clear this fix
            gpsCopy(&lastMotionFix, newFix); // save new motion fix
            if (!isInMotion) {
                // I wasn't moving before, but now I am
                isInMotion = utTrue;
                lastInMotionMessageTimer = utcGetTimer(); // start "in-motion" timer
                // send 'start' event
                _queueMotionEvent(MOTION_START_PRIORITY, STATUS_MOTION_START, nowTime, newFix);
            } else {
                // continued in-motion
            }

        } else {
            
            // I'm not moving
            if (isInMotion) {
                // I was moving before, but now I'm not.
                if (lastStoppedTimer <= 0L) {
                    // start "stopped" timer (first non-moving sample)
                    lastStoppedTimer = utcGetTimer();
                    gpsCopy(&lastStoppedFix, newFix);  // first event at 'stop'
                    // this will be reset again if we start moving before the time expires
                }
                // Check to see if my 'stop' timer has expired
                UInt16 defMotionStop = (UInt16)propGetUInt32(PROP_MOTION_STOP,0L); // seconds
                utBool officiallyStopped = utcIsTimerExpired(lastStoppedTimer,defMotionStop);
                // Note: also check for other 'stop' indicators here (ie. ignition off)
                if (officiallyStopped) {
                    // time expired, we are now officially NOT moving
                    gpsCopy(&lastMotionFix, newFix); // save new motion fix
                    _motionStop(nowTime, newFix);
                } else {
                    // I've not yet officially 'stopped' (I may be at a stop sign/light)
                }
            } else {
                // still not moving 
            }
        }
        
    } else {
        
        // this is necessary in case "start/stop" events were turned off while moving
        isInMotion = utFalse;
        // default definition for motion (may not be used by all clients)
        isCurrentlyMoving = (speedKPH >= 2.0)? utTrue : utFalse;
        
    }

    /* check in-motion/dormant */
    //   - send "in-motion" events while moving (between start/stop events)
    //   - send "dormant" events while not moving
    if (isInMotion) {

        // moving (between start/stop) ['isCurrentlyMoving' may be false]
        UInt32 defMotionInterval = propGetUInt32(PROP_MOTION_IN_MOTION, 0L);
        if (defMotionInterval > 0L) {
            // In-motion interval has been defined - we want in-motion events.
            if ((defMotionInterval < MIN_IN_MOTION_INTERVAL) && !isDebugMode()) {
                // in-motion interval is too small, reset to minimum value
                defMotionInterval = MIN_IN_MOTION_INTERVAL;
                //propSetUInt32(PROP_MOTION_IN_MOTION, defMotionInterval);
            }
            UInt16 defStopType = (UInt16)propGetUInt32(PROP_MOTION_STOP_TYPE, 0); // 0=after_delay, 1=when_stopped
            if ((defStopType == MOTION_STOP_WHEN_STOPPED) && !isCurrentlyMoving) {
                // 'defStopType' indicates that in-motion messages are to be generated iff actually moving, 
                // and we are NOT currently moving.  Thus we suspend in-motion events.
            } else
            if (utcIsTimerExpired(lastInMotionMessageTimer,defMotionInterval)) {
                // we're moving, and the in-motion interval has expired
                lastInMotionMessageTimer = utcGetTimer();
                // send an in-motion message
                _queueMotionEvent(IN_MOTION_PRIORITY, STATUS_MOTION_IN_MOTION, nowTime, newFix);
            } else {
                // we are considered moving, but the in-motion interval has not expired
            }
        }
        lastDormantMessageTimer = (TimerSec_t)0L;
        dormantCount = 0L;
        
    } else
    { // if (defMotionStart > 0.0) <-- this would check dormant only if not tracking start/stop
        // we're not moving (or we're not tracking start/stop), check dormant
        // Note: if tracking start/stop is off (ie. PROP_MOTION_START is '0'), then
        // 'dormant' checking will be in effect.  To turn off dormant checking as
        // well, PROP_MOTION_DORMANT_INTRVL must also be set to '0'.
        
        // check dormant interval
        UInt32 defDormantInterval = propGetUInt32(PROP_MOTION_DORMANT_INTRVL, 0L);
        if (defDormantInterval > 0L) {
            if ((defDormantInterval < MIN_DORMANT_INTERVAL) & !isDebugMode()) { 
                defDormantInterval = MIN_DORMANT_INTERVAL;
                //propSetUInt32(PROP_MOTION_DORMANT_INTRVL, defDormantInterval);
            }
            UInt32 maxDormantCount = propGetUInt32(PROP_MOTION_DORMANT_COUNT, 0L);
            if ((maxDormantCount <= 0L) || (dormantCount < maxDormantCount)) {
                if (lastDormantMessageTimer <= 0L) {
                    // initialize dormant timer
                    lastDormantMessageTimer = utcGetTimer();
                    dormantCount = 0L;
                } else
                if (utcIsTimerExpired(lastDormantMessageTimer,defDormantInterval)) {
                    lastDormantMessageTimer = utcGetTimer();
                    // send dormant message
                    _queueMotionEvent(DORMANT_PRIORITY, STATUS_MOTION_DORMANT, nowTime, newFix);
                    dormantCount++;
                }
            }
        }
        
    }
    
    /* check excessive speed */
    double defMaxSpeedKPH = propGetDouble(PROP_MOTION_EXCESS_SPEED, 0.0); // kph
    if (defMaxSpeedKPH > 0.0) {
        
        // maxSpeed is defined
        utBool isCurrentlyExceedingSpeed = (speedKPH >= defMaxSpeedKPH)? utTrue : utFalse;
        if (isCurrentlyExceedingSpeed) {
            // I'm currently exceeding maxSpeed
            if (!isExceedingSpeed) {
                // I wasn't exceeding maxSpeed before, but now I am
                isExceedingSpeed = utTrue;
                _queueMotionEvent(EXCESS_SPEED_PRIORITY, STATUS_MOTION_EXCESS_SPEED, nowTime, newFix);
            } else {
                // I'm still exceeding maxSpeed
            }
        } else {
            // I'm currently not exceeding maxSpeed
            if (isExceedingSpeed) {
                // I was exceeding maxSpeed before, but now I'm not
                double speedSetbackKPH = (defMaxSpeedKPH > EXCESS_SPEED_SETBACK)? 
                    (defMaxSpeedKPH - EXCESS_SPEED_SETBACK) : 
                    defMaxSpeedKPH;
                if (speedKPH < speedSetbackKPH) {
                    // I've slowed down enough to reset the excess-speed flag
                    isExceedingSpeed = utFalse;
                } else {
                    // not quite slow enough to reset the excess-speed flag
                }
            } else {
                // I'm still not exceeding maxSpeed
            }
        }
        
    } else {
        
        // this is necessary in case "speeding" events were turned off while speeding
        if (isExceedingSpeed) {
            isExceedingSpeed = utFalse;
        }
        
    }

#if defined(TRANSPORT_MEDIA_SERIAL) || defined(SECONDARY_SERIAL_TRANSPORT)
// enable for Bluetooth transport only
    /* simple moving check */
    if (isCurrentlyMoving) {
        // we're moving, check interval
        UInt32 movingInterval = propGetUInt32(PROP_MOTION_MOVING_INTRVL,0L);
        if (movingInterval > 0L) {
            if (utcIsTimerExpired(lastMovingMessageTimer,movingInterval)) {
                // moving timer expired
                lastMovingMessageTimer = utcGetTimer();
                _queueMotionEvent(MOVING_PRIORITY, STATUS_MOTION_MOVING, nowTime, newFix);
            }
        }
    }
#endif

}

/* check new GPS fix for various motion events */
void motionCheckGPS(const GPS_t *oldFix, const GPS_t *newFix)
{
    MOTION_LOCK {
        _motionCheckGPS(oldFix, newFix);
    } MOTION_UNLOCK
}

// ----------------------------------------------------------------------------

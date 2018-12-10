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
//  Main entry point and custom startup initialization
//  Provides customized startup initialization.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/12  Martin D. Flynn
//     -Integrated syslog support
//  2006/01/27  Martin D. Flynn
//     -Changed '-debug' to send output logs to console
//     -Fixed bug where "TRANSPORT_MEDIA_SOCK" should have been "TRANSPORT_MEDIA_SOCKET"
//  2006/02/12  Martin D. Flynn
//     -Changed to accomodate new return type from "gpsGetDiagnostics".
//     -Changed to save properties back to 'propertyCache', instead of 'propertyFile'.
//  2006/04/10  Martin D. Flynn
//     -Changed Socket property 'PROP_MOTION_IN_MOTION' default to 15 minutes.
//  2006/06/07  Martin D. Flynn
//     -Relaxed TRANSPORT_MEDIA_SOCKET connection settings
//  2007/01/28  Martin D. Flynn
//     -Many changes to facilitate WindowsCE port
//  2007/04/28  Martin D. FLynn
//     -Don't queue events if either PROP_COMM_HOST or PROP_COMM_PORT are undefined.
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "custom/log.h"
#include "custom/gps.h"
#include "custom/gpsmods.h"
#include "custom/startup.h"
#include "custom/transport.h"

#include "modules/motion.h"
#include "modules/odometer.h"

#if defined(ENABLE_GEOZONE)
#  include "modules/geozone.h"
#endif

#include "tools/stdtypes.h"
#include "tools/utctools.h"
#include "tools/strtools.h"
#include "tools/bintools.h"
#include "tools/threads.h"
#include "tools/io.h"
#include "tools/comport.h"

#include "base/cerrors.h"
#include "base/propman.h"
#include "base/statcode.h"
#include "base/mainloop.h"

#include "base/accting.h"
#include "base/events.h"
#include "base/protocol.h"
#if defined(ENBALE_UPLOAD)
#  include "base/upload.h"
#endif

#if defined(TARGET_WINCE)
#  include <aygshell.h>
#  include "custom/wince/wceos.h"
#else
// TODO: should separate this into gumstix/linux/cygwin
#  include "custom/linux/os.h"
#endif

// ----------------------------------------------------------------------------
// Version string
// Examples:
//   OpenDMTP_FILE.1.0.3
//   OpenDMTP_SOCK.1.0.3

#define DMTP_NAME                   APPLICATION_NAME

#if   defined(TRANSPORT_MEDIA_SOCKET)
#  define DMTP_TYPE                 "SOCK"  // - Socket communication
#elif defined(TRANSPORT_MEDIA_SERIAL)
#  define DMTP_TYPE                 "SER"   // - BlueTooth communication
#elif defined(TRANSPORT_MEDIA_GPRS)
#  define DMTP_TYPE                 "GPRS"  // - GPRS communication
#elif defined(TRANSPORT_MEDIA_FILE)
#  define DMTP_TYPE                 "FILE"  // - File event capture
#endif

#if !defined(DMTP_TYPE)
#  error DMTP_TYPE not defined.
#endif

#define DMTP_NAME_TYPE_VERSION      DMTP_NAME "_" DMTP_TYPE "." RELEASE_VERSION

const char APP_VERSION[] = { "$$VERSION:" DMTP_NAME_TYPE_VERSION "[" APPLICATION_FEATURES "]" };

// ----------------------------------------------------------------------------

// GPRS and SOCKET connect host:port
#define DEFAULT_COMM_HOST           "localhost"
#define DEFAULT_COMM_PORT           "31000" // this default may change at any time

// ----------------------------------------------------------------------------

#if defined(PROPERTY_FILE)
/* property file names */
static char propertyFile[80]  = { PROPERTY_FILE  };
static char propertyCache[90] = { PROPERTY_CACHE };
#if defined(PROPERTY_SAVE_INTERVAL)
#define FIRST_PROPERTY_SAVE_INTERVAL    20L
static TimerSec_t   lastSavePropertyTimer = 0L;
static UInt32       lastSavePropertyInterval = FIRST_PROPERTY_SAVE_INTERVAL;
#endif
#endif

/* server host:port defined */
#if defined(TRANSPORT_MEDIA_SOCKET) || defined(TRANSPORT_MEDIA_GPRS)
static utBool hasServerHostPort = utTrue;
#endif

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

// define CUSTOM_EVENT_PACKETS to enable custom event packet
//#define CUSTOM_EVENT_PACKETS
#if defined(CUSTOM_EVENT_PACKETS) // {
// Custom defined event packets
#endif // } defined(CUSTOM_EVENT_PACKETS)

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

/* add the specified event to the queue */
static utBool _customAddSingleEvent(PacketPriority_t priority, ClientPacketType_t pktType, Event_t *er)
{

    /* no event? */
    if (!er) {
        return utFalse;
    }

    /* add event packet */
    Packet_t pkt;
    utBool didAdd = evAddEventPacket(&pkt, priority, pktType, er);
    UInt32 pktSeq = pkt.sequence;
    /* display event */
    logDEBUG(LOGSRC,"$%04lX:%lu,%04X,%.4lf/%.4lf:%ld,%.1lf,%s,%s,%04lX", 
        pktType, er->timestamp[0], er->statusCode, 
        er->gpsPoint[0].latitude, er->gpsPoint[0].longitude, er->gpsQuality, 
        er->odometerKM, 
        er->entity[0], er->entity[1],
        pktSeq);

    /* return success */
    return didAdd;

}

/* add the specified event to the queue */
static utBool _customAddEventFtn(PacketPriority_t priority, ClientPacketType_t pktType, Event_t *er)
{

    /* no event? */
    if (!er) {
        return utFalse;
    }
    
    /* special event handling */
    // perform any special event handling here

    /* skip out if priority is PRIORITY_NONE */
    if (priority == PRIORITY_NONE) {
        // normally this will never occur, however, the client may choose to set the
        // priority on a special event to PRIORITY_NONE in order to be able to handle
        // it above and discard it here if necessary.
        return utFalse;
    }

    /* skip if the packet type is not a standard event packet */
    if (!pktIsEventPacket(pktType)) {
        // generally, this will only occur in an HOS environment
        return utFalse; // not a vehicle telemetry event
    }
    /* don't queue events if we don't have anywhere to send the data */
#if defined(TRANSPORT_MEDIA_SOCKET) || defined(TRANSPORT_MEDIA_GPRS)
    if (!hasServerHostPort) {
        return utFalse; // host:port not defined, don't queue event
    }
#endif // defined(TRANSPORT_MEDIA_SOCKET) || defined(TRANSPORT_MEDIA_GPRS)

    /* promote remaining priorities to PRIORITY_HIGH for file and serial transport */
#if defined(TRANSPORT_MEDIA_FILE) || defined(TRANSPORT_MEDIA_SERIAL)
    priority = PRIORITY_HIGH;
#endif

    /* add single event */
    return _customAddSingleEvent(priority, pktType, er);

}

// ----------------------------------------------------------------------------

/* property refresh, prior to a 'Get' */
static void _propertyPreGET(PropertyRefresh_t mode, Key_t key, UInt8 *args, int argLen)
{
    if (mode & PROP_REFRESH_GET) {
        switch (key) {
            case PROP_STATE_TIME: {
                // update property with clock time
                propSetUInt32(PROP_STATE_TIME, utcGetTimeSec());
            } break;
            case PROP_STATE_GPS: {
                // update property with current GPS location
                GPS_t lastGPS;
                GPSOdometer_t gpsOdom;
                memcpy((GPSShort_t*)&gpsOdom, (GPSShort_t*)gpsGetLastGPS(&lastGPS,-1), sizeof(GPSShort_t));
                gpsOdom.meters = (UInt32)(odomGetDeviceDistanceMeters() + 0.5); // ROUND(X?)
                propSetGPS(PROP_STATE_GPS, &gpsOdom);
            } break;
            case PROP_STATE_GPS_DIAGNOSTIC: {
                // return GPS diagnostics (ie. GPS module health)
                int i;
                UInt32 *gpsStats = (UInt32*)gpsGetDiagnostics((GPSDiagnostics_t*)0);
                for (i = 0; i < (sizeof(GPSDiagnostics_t)/sizeof(UInt32)); i++) {
                    propSetUInt32AtIndex(PROP_STATE_GPS_DIAGNOSTIC, i, gpsStats[i]);
                }
            } break;
            case PROP_STATE_QUEUED_EVENTS: {
                // update property with number of events
                Int32 evQueCnt = 0L, evTotCnt = 0L;
                evQueCnt = evGetPacketCount();
                evTotCnt = evGetTotalPacketCount();
                propSetUInt32AtIndex(PROP_STATE_QUEUED_EVENTS, 0, (UInt32)evQueCnt);
                propSetUInt32AtIndex(PROP_STATE_QUEUED_EVENTS, 1, (UInt32)evTotCnt);
            } break;
            case PROP_GEOF_COUNT: {
                // update property with number of GeoZone entries
#if defined(ENABLE_GEOZONE)
                propSetUInt32(PROP_GEOF_COUNT, (UInt32)geozGetGeoZoneCount());
#endif
            } break;
        }
    } 
}

/* property update, after a 'Set' */
static void _propertyPostSET(PropertyRefresh_t mode, Key_t key, UInt8 *args, int argLen)
{
    if (mode & PROP_REFRESH_SET) {
        switch (key) {
            case PROP_STATE_DEVICE_ID: {
                // change host/device name
                const char *s = propGetDeviceID(0); // propGetString(PROP_STATE_DEVICE_ID,"");
                if (!s || !*s) { 
                    s = DEVICE_ID; 
                    if (!s || !*s) {
                        s = propGetString(PROP_STATE_SERIAL, "");
                    }
                }
#if !defined(SECONDARY_SERIAL_TRANSPORT)
                // set bluetooth broadcast name
                osSetHostname(s);
#endif
                // save properties to save new ID?
            } break;
#if defined(SECONDARY_SERIAL_TRANSPORT)
            case PROP_STATE_DEVICE_BT: {
                // change bluetooth broadcast name
                const char *s = propGetDeviceID(1); // propGetString(PROP_STATE_DEVICE_BT,"");
                if (!s || !*s) { 
                    s = DEVICE_ID; 
                    if (!s || !*s) {
                        s = propGetString(PROP_STATE_SERIAL, "");
                    }
                }
                // set bluetooth broadcast name
                osSetHostname(s);
                // save properties to save new ID?
            } break;
#endif
        }
    }
}

// ----------------------------------------------------------------------------

/* status "ping" */
CommandError_t startupPingStatus(PacketPriority_t priority, StatusCode_t code, int ndx)
{
    ClientPacketType_t pktType = DEFAULT_EVENT_FORMAT;
    
    /* initialize an event in anticipation that the status code is valid */
    GPS_t gps;
    Event_t evRcd;
    evSetEventDefaults(&evRcd, code, 0L, gpsGetLastGPS(&gps,-1));

    /* check specified status code */
    if ((code == STATUS_LOCATION) || (code == STATUS_WAYMARK) || (code == STATUS_QUERY)) {
        if (ndx > 0) {
            // index was specified and was greater than '0'
            return COMMAND_INDEX;
        } else {
            // send current location
            _customAddEventFtn(priority, pktType, &evRcd);
            return COMMAND_OK;
        }
    } else
    if ((code >= STATUS_ODOM_0) && (code <= STATUS_ODOM_7)) {
        int odoNdx = code - STATUS_ODOM_0; // odometer index
        if ((ndx >= 0) && (ndx != odoNdx)) {
            // index was specified, but is not equal to the odomenter index
            return COMMAND_INDEX;
        } else {
            // send current odometer value
            // 'evRcd.odometerKM' is already be set above (in 'evSetEventDefaults')
            if (odoNdx == 0) {
                // vehicle distance (may need special handling
                evRcd.distanceKM = odomGetDeviceDistanceMeters() / 1000.0;
            } else {
                // driver, etc
                evRcd.distanceKM = odomGetDistanceMetersAtIndex(odoNdx) / 1000.0;
            }
            _customAddEventFtn(priority, pktType, &evRcd);
            return COMMAND_OK;
        }
    } else {
        // Other candidated:
        //  STATUS_ELAPSED_xx
        return COMMAND_STATUS;
    }
    
}

// ----------------------------------------------------------------------------

/* status "ping" [see PROP_CMD_STATUS_EVENT] */
static CommandError_t _cmdSendStatus(int protoNdx, Key_t key, const UInt8 *data, int dataLen)
{
    // 'protoNdx' contains the handle to the protocol transport

    /* parse arguments */
    UInt32 code32 = 0L;
    UInt32 ndx32  = 0L;
    int flds = binScanf(data, dataLen, "%2u%1u", &code32, &ndx32);
    if (flds < 1) {
        return COMMAND_ARGUMENTS;
    }
    StatusCode_t code = (StatusCode_t)code32;
    int ndx = (flds >= 2)? (int)ndx32 : -1;
    
    /* status ping */
    return startupPingStatus(PRIORITY_HIGH, code, ndx);
     
}

/* set digital output [see PROP_CMD_SET_OUTPUT] */
static CommandError_t _cmdSetOutput(int protoNdx, Key_t key, const UInt8 *data, int dataLen)
{
    // 'protoNdx' contains the handle to the protocol transport

    /* parse arguments */
    UInt32 ndx    = 0L;
    UInt32 state  = 0L;
    UInt32 duraMS = 0L;
    int flds = binScanf(data, dataLen, "%1u%1u%4u", &ndx, &state, &duraMS);
    if (flds < 2) {
        return COMMAND_ARGUMENTS;
    } else
    if (ndx > 15) {
        return COMMAND_INDEX;
    }
    
    /* implement setting output state here */
    // currently not supported
    return COMMAND_FEATURE_NOT_SUPPORTED;

}

/* explicitly save properties [see PROP_CMD_SAVE_PROPS] */
static CommandError_t _cmdSaveProperties(int protoNdx, Key_t key, const UInt8 *data, int dataLen)
{
    // 'protoNdx' contains the handle to the protocol transport
    
    // arguments are ignored
    startupSaveProperties();
    return COMMAND_OK;
    
}

// ----------------------------------------------------------------------------

/* initialize properties */
void startupPropInitialize(utBool loadPropCache)
{

    /* init properties */
    propInitialize(utTrue);

    /* firmware */
    propInitFromString(PROP_STATE_FIRMWARE, DMTP_NAME_TYPE_VERSION);

#if defined(TRANSPORT_MEDIA_SERIAL)
    /* AccountID/DeviceID may be writable via BlueTooth transport (for OTA configuration) */
    propSetReadOnly(PROP_STATE_ACCOUNT_ID, utFalse);
    propSetReadOnly(PROP_STATE_DEVICE_ID , utFalse);
#endif

    /* property initialization */
    propSetNotifyFtn(PROP_REFRESH_GET               , &_propertyPreGET);
    propSetNotifyFtn(PROP_REFRESH_SET               , &_propertyPostSET);
    
    /* set supporting commands */
    propSetCommandFtn(PROP_CMD_SAVE_PROPS           , &_cmdSaveProperties);
    propSetCommandFtn(PROP_CMD_STATUS_EVENT         , &_cmdSendStatus);
    propSetCommandFtn(PROP_CMD_SET_OUTPUT           , &_cmdSetOutput);
    
    /* copyright */
    propSetString(PROP_STATE_COPYRIGHT              , COPYRIGHT);

    /* connection properties */
#if defined(TRANSPORT_MEDIA_FILE)
    // disable Duplex connection (not wanted when just writing to a file)
    propInitFromString(PROP_COMM_MAX_CONNECTIONS    , "1,0,0");     // no duplex connections, no quota
    propInitFromString(PROP_COMM_MIN_XMIT_DELAY     , "0");
    propInitFromString(PROP_COMM_MIN_XMIT_RATE      , "0");
    propInitFromString(PROP_COMM_MAX_DUP_EVENTS     , "0");
    propInitFromString(PROP_COMM_MAX_SIM_EVENTS     , "255");

#elif defined(TRANSPORT_MEDIA_SERIAL) // comm config
    // disable Simplex connection (not wanted when communicating over serial port)
    propInitFromString(PROP_COMM_SPEAK_FIRST        , "0");
    propInitFromString(PROP_COMM_FIRST_BRIEF        , "1");         // start with identification only
    propInitFromString(PROP_COMM_MAX_CONNECTIONS    , "1,1,0");     // no simplex connections, no quota
    propInitFromString(PROP_COMM_MIN_XMIT_DELAY     , "0");
    propInitFromString(PROP_COMM_MIN_XMIT_RATE      , "0");
    propInitFromString(PROP_COMM_MAX_XMIT_RATE      , "0");
    propInitFromString(PROP_COMM_MAX_DUP_EVENTS     , "10");
    propInitFromString(PROP_COMM_MAX_SIM_EVENTS     , "0");
    //propInitFromString(PROP_COMM_ENCODINGS        , "0x6");       // HEX,Base64

#elif defined(TRANSPORT_MEDIA_SOCKET) // comm config
    propInitFromString(PROP_COMM_MAX_CONNECTIONS    , "20,10,30");
    propInitFromString(PROP_COMM_MIN_XMIT_DELAY     , "60");        // seconds
    propInitFromString(PROP_COMM_MIN_XMIT_RATE      , "60");        // seconds
    propInitFromString(PROP_COMM_MAX_DUP_EVENTS     , "8");
    propInitFromString(PROP_COMM_MAX_SIM_EVENTS     , "4");

#elif defined(TRANSPORT_MEDIA_GPRS) // comm config
    propInitFromString(PROP_COMM_MAX_CONNECTIONS    , "6,4,60");
    propInitFromString(PROP_COMM_MIN_XMIT_DELAY     , "60");        // seconds
    propInitFromString(PROP_COMM_MIN_XMIT_RATE      , "60");        // seconds
    propInitFromString(PROP_COMM_MAX_DUP_EVENTS     , "8");
    propInitFromString(PROP_COMM_MAX_SIM_EVENTS     , "4");

#else // default values
    propInitFromString(PROP_COMM_MAX_CONNECTIONS    , "4,2,120");
    propInitFromString(PROP_COMM_MIN_XMIT_DELAY     , "90");        // seconds
    propInitFromString(PROP_COMM_MIN_XMIT_RATE      , "600");       // seconds
    propInitFromString(PROP_COMM_MAX_DUP_EVENTS     , "8");
    propInitFromString(PROP_COMM_MAX_SIM_EVENTS     , "4");
#endif

    /* motion parameters */
#if defined(TRANSPORT_MEDIA_FILE)
    propInitFromString(PROP_MOTION_EXCESS_SPEED     , "0.0");       // kph
    propInitFromString(PROP_MOTION_START            , "10.0");      // kph
    propInitFromString(PROP_MOTION_IN_MOTION        , "60");        // seconds (1 minutes)
    propInitFromString(PROP_MOTION_DORMANT_INTRVL   , "900");       // seconds (15 minutes)
    propInitFromString(PROP_MOTION_DORMANT_COUNT    , "0");         // unlimited dormant messages

#elif defined(TRANSPORT_MEDIA_SOCKET) // comm config
    propInitFromString(PROP_MOTION_EXCESS_SPEED     , "0.0");       // kph
    propInitFromString(PROP_MOTION_START            , "10.0");      // kph
    propInitFromString(PROP_MOTION_IN_MOTION        , "900");       // seconds (15 minutes)
    propInitFromString(PROP_MOTION_DORMANT_INTRVL   , "7200");      // seconds (2 hours)
    propInitFromString(PROP_MOTION_DORMANT_COUNT    , "2");         // 2 dormant messages

#else // default values
    propInitFromString(PROP_MOTION_EXCESS_SPEED     , "0.0");       // kph
    propInitFromString(PROP_MOTION_START            , "10.0");      // kph
    propInitFromString(PROP_MOTION_IN_MOTION        , "600");       // seconds (10 minutes)
    propInitFromString(PROP_MOTION_DORMANT_INTRVL   , "3600");      // seconds (1 hour)
    propInitFromString(PROP_MOTION_DORMANT_COUNT    , "2");         // 2 dormant messages
#endif

    // "propLoadProperties(...)" may also be used to load properties from aux storage
#if defined(PROPERTY_FILE)
    if (*propertyFile) {
        logDEBUG(LOGSRC,"Loading property config file: %s", propertyFile);
        propLoadProperties(propertyFile, utTrue);
    }
    if (loadPropCache && *propertyCache) {
        logDEBUG(LOGSRC,"Loading property cache file: %s", propertyCache);
        propLoadProperties(propertyCache, utFalse);
    } else {
        // delete property cache file?
        logDEBUG(LOGSRC,"Not loading property cache file");
    }
#endif

    /* Serial # */
    // If a default serial# has not already been set, get the actual device serial#
    // (typically, a default serial number will not already be defined)
    const char *serNum = propGetString(PROP_STATE_SERIAL, "");
    if (!serNum || !*serNum) {
        // Serial number not yet defined
        propInitFromString(PROP_STATE_SERIAL, osGetSerialNumberID());
        serNum = propGetString(PROP_STATE_SERIAL, "");
    }

    /* Unique ID */
    UInt16 uniqLen = 0;
    const UInt8 *uniqId = propGetBinary(PROP_STATE_UNIQUE_ID, (UInt8*)0, (UInt16*)0, &uniqLen);
    if (!uniqId || (uniqLen < MIN_UNIQUE_SIZE)) {
        // Unique ID not yet defined
        UInt8 _uniqueID[] = UNIQUE_ID;
        if (sizeof(_uniqueID) >= MIN_UNIQUE_SIZE) {
            propSetBinary(PROP_STATE_UNIQUE_ID, _uniqueID, sizeof(_uniqueID)); // changed!
            uniqId = propGetBinary(PROP_STATE_UNIQUE_ID, (UInt8*)0, (UInt16*)0, &uniqLen);
        } else {
            // leave unique-id undefined
            //logDEBUG(LOGSRC,"Leaving Unique-ID undefined ...");
        }
    }

    /* Account ID (primary) */
    const char *acctId = propGetAccountID(); // propGetString(PROP_STATE_ACCOUNT_ID,"");
    if (!acctId || !*acctId) {
        // Account ID not yet defined
        const char *_acctId = ACCOUNT_ID;
        if (_acctId && *_acctId) {
            // set default account id
            propInitFromString(PROP_STATE_ACCOUNT_ID, _acctId);
        } else {
            // leave account-id undefined
            // NOTE: Leaving the account ID undefined allows the server to utilize 
            // the Device-ID in a similar fashion as a Unique-ID for identifying
            // a device record.
        }
        acctId = propGetAccountID();  // propGetString(PROP_STATE_ACCOUNT_ID,"");
    }
    
    /* Device ID (primary) */
    const char *devId = propGetDeviceID(0); // propGetString(PROP_STATE_DEVICE_ID, "");
    if (!devId || !*devId) {
        // Device-ID not yet defined
        const char *_devId = DEVICE_ID;
        if (_devId && *_devId) {
            // set default device id (if specified)
            logDEBUG(LOGSRC,"Setting default Device: %s", DEVICE_ID);
            propInitFromString(PROP_STATE_DEVICE_ID, _devId);
        } else
        if (serNum && *serNum) {
            // set to serial number (if available)
            logDEBUG(LOGSRC,"Setting Serial# Device: %s", serNum);
            propInitFromString(PROP_STATE_DEVICE_ID, serNum);
        } else {
            // no default device id, and no serial#, make up a name
            //propInitFromString(PROP_STATE_DEVICE_ID, "device");
            // or leave undefined
        }
        devId = propGetDeviceID(0); // propGetString(PROP_STATE_DEVICE_ID,"");
    }
   
    /* Device ID (secondary) */
#if defined(SECONDARY_SERIAL_TRANSPORT)
    const char *devBt = propGetDeviceID(1); // propGetString(PROP_STATE_DEVICE_BT, "");
    if (!devBt || !*devBt) {
        devBt = devId;
    } else {
        devId = devBt;
    }
#endif

    /* reset hostname to device id */
    // This sets the bluetooth broadcast name on serial transport
    osSetHostname(devId);

    /* make sure all 'changed' flags are reset */
    propClearChanged();
    // Note that changing properties on the command line will set those properties to 'changed'.

}

/* save properties */
utBool startupSaveProperties()
{
    if (*propertyCache) {
        if (propHasChanged()) {
            logINFO(LOGSRC,"Saving properties ...");
            propSaveProperties(propertyCache, utFalse);
            return utTrue;
        } else {
            return utFalse;
        }
    } else {
        logDEBUG(LOGSRC,"No property cache! ...");
        return utFalse;
    }
}

/* save properties */
utBool startupReboot(utBool reboot)
{
    
    /* save state prior to reboot */
    startupSaveProperties();
    /* reboot */
    // may not be supported on this platform
    if (reboot) {
        osReboot();
        // should not return if reboot is supported
    }
    
    /* return reboot failed */
    return utFalse;
    
}

// ----------------------------------------------------------------------------

/* main process loop callback */
void startupMainLoopCallback()
{
    // This function gets called about once per second from the main processing loop.
    // Other monitoring functions, etc. should go here.

    /* periodic gps module call */
    gpsModulePeriodic();

    /* save changed properties */
#if defined(PROPERTY_SAVE_INTERVAL) && defined(PROPERTY_FILE)
    if (utcIsTimerExpired(lastSavePropertyTimer,lastSavePropertyInterval)) {
        lastSavePropertyTimer = utcGetTimer(); // reset
        lastSavePropertyInterval = PROPERTY_SAVE_INTERVAL;
        startupSaveProperties();
    }
#endif

    /* Expired upload */
#if defined(ENBALE_UPLOAD)
    if (uploadIsExpired()) {
        // upload did not complete in allowed time
        uploadCancel();
    }
#endif

}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Main entry point

#if defined(__DATE2__)
#  define _DATETIME_    (__TIME__ " " __DATE2__ " PST")
#else
#  define _DATETIME_    (__TIME__ " " __DATE__ " PST")
#endif

/* print the application header banner */
static void _printBanner()
{
    const char *header    = APPLICATION_DESCRIPTION;
    const char *version   = propGetString(PROP_STATE_FIRMWARE,"");
    const char *build     = _DATETIME_;
    const char *features  = APPLICATION_FEATURES + 1;
    const char *account   = propGetAccountID(); // propGetString(PROP_STATE_ACCOUNT_ID,"");
    const char *device    = propGetDeviceID(0);  // propGetString(PROP_STATE_DEVICE_ID,"");
    const char *serial    = propGetString(PROP_STATE_SERIAL,"");
    char host[64] = { 0 };
    osGetHostname(host, sizeof(host));
    logPRINTF(LOGSRC,SYSLOG_INFO, "--------------------------------------------------------");
    logPRINTF(LOGSRC,SYSLOG_INFO, "%s", header);
    logPRINTF(LOGSRC,SYSLOG_INFO, "Ver: %s [%s]", version, build);
    logPRINTF(LOGSRC,SYSLOG_INFO, "Att: %s", features);
    logPRINTF(LOGSRC,SYSLOG_INFO, "Que: %lu max events [format $%04lX]", (UInt32)EVENT_QUEUE_SIZE, (UInt32)DEFAULT_EVENT_FORMAT);
    logPRINTF(LOGSRC,SYSLOG_INFO, "Dev: %s/%s [%s:%s]", account, device, host, serial);
    logPRINTF(LOGSRC,SYSLOG_INFO, "--------------------------------------------------------");
}

/* print usage information */
static void _usage(const char *pgm)
{
    FILE *out = stderr;
    _printBanner();
    fprintf(out, "Usage: \n");
    fprintf(out, "  %s -h[elp]              - display this help and exit\n", pgm);
    fprintf(out, "  %s -v[ersion]           - display version and exit\n", pgm);
    fprintf(out, "  %s [options-1] [options-2]\n", pgm);
    fprintf(out, "  Options-1:\n");
    fprintf(out, "    [-deb[ug]]                 - Debug mode (ie. 'Debug' logging level)\n");
    fprintf(out, "    [-log <level>]             - Set logging level (log to syslog)\n");
    fprintf(out, "    [-pf[ile] <file> [save]]   - load properties from specified file\n");
    fprintf(out, "  Options-2:\n");
    fprintf(out, "    [-enc[oding] <enc>]        - Packet encoding\n");
    fprintf(out, "    [-cksum]                   - Enable ASCII checksums\n");
#if !defined(TRANSPORT_MEDIA_FILE) && !defined(TRANSPORT_MEDIA_SERIAL)
    fprintf(out, "    [-dup[lex]]                - Force all packets to be sent via duplex\n");
#endif
#if !defined(TRANSPORT_MEDIA_SERIAL) && !defined(TRANSPORT_MEDIA_FILE)
    fprintf(out, "    [-sim[plex]]               - Force all packets to be sent via simplex\n");
#endif
    fprintf(out, "    [-comlog]                  - Enable commPort data logging\n");
    fprintf(out, "    [-gps <port> [<model>]]    - GPS serial port\n");
#if defined(TRANSPORT_MEDIA_SOCKET) || defined(TRANSPORT_MEDIA_GPRS)
    fprintf(out, "    [-server <host> [<port>]]  - Server protocol host and port\n");
#endif
#if defined(TRANSPORT_MEDIA_SERIAL) && !defined(TARGET_WINCE)
    fprintf(out, "    [-serial <port> [<bps>]]   - Serial protocol comm port\n");
#endif
#if defined(TRANSPORT_MEDIA_GPRS) && !defined(TARGET_WINCE)
    fprintf(out, "    [-gprs <port> [<bps>]]     - GPRS modem port\n");
#endif
#if defined(TRANSPORT_MEDIA_FILE)
    fprintf(out, "    [-file <outfile>]          - Event data file\n");
#endif
    fprintf(out, "\n");
}

// main entry point
int startupDMTP(int argc, char *argv[], int runInThread)
{
    PacketEncoding_t dftEncoding = DEFAULT_ENCODING;
    utBool comLog = utFalse;
    utBool loadPropCache = utTrue;
    const char *portName = (char*)0;
    
    /* init argument index */
    if (!argv) { argc = 0; }
    const char *pgmName = (argc > 0)? argv[0] : "?";
    int argp = 1;

    /* initialize file/stream i/o */
    ioInitialize(); 
    
    /* init syslog */
    logInitialize(SYSLOG_NAME);
    
    /* check for debug mode and logging */
    // These are performed first to allow debug logging to occur _before_ properties
    // are loaded and initialized.
    for (; argp < argc; argp++) {
        if (strEqualsIgnoreCase(argv[argp],"-debug") || strEqualsIgnoreCase(argv[argp],"-deb")) {
            // -debug
            setDebugMode(utTrue);
            logDEBUG(LOGSRC,"Debug Mode ON ...\n");
        } else
        if (strEqualsIgnoreCase(argv[argp],"-log")) {
            // -log <level>
            argp++;
            if ((argp < argc) && ((*argv[argp] != '-') || isdigit(*(argv[argp]+1)))) {
                int level = logParseLevel(argv[argp]); // can be negative
                logSetLevel(level);
                logEnableSyslog((level >= 0)? utTrue : utFalse);
            } else {
                // log level is unchanged
            }
        } else
        if (strEqualsIgnoreCase(argv[argp], "-pfile") || strEqualsIgnoreCase(argv[argp], "-pf")) {
            // -pf[ile] <propertyFile> [<propertyCache>]
            argp++;
            if ((argp < argc) && (*argv[argp] != '-')) {
                memset(propertyFile , 0, sizeof(propertyFile));
                memset(propertyCache, 0, sizeof(propertyCache));
                strncpy(propertyFile, argv[argp], sizeof(propertyFile) - 1);
                if (((argp + 1) < argc) && (*argv[argp + 1] != '-')) {
                    argp++;
                    strncpy(propertyCache, argv[argp], sizeof(propertyCache) - 1);
                } else {
                    // set property cache
                    // (note: 'propertyCache' buffer is larger than 'propertyFile')
                    int len = strlen(propertyFile);
                    strcpy(propertyCache, propertyFile);
                    if (strEndsWith(propertyCache,".conf")) {
                        int dot = len - 5; // - strlen(".conf");
                        strcpy(&propertyCache[dot], ".dat");
                    } else {
                        strcpy(&propertyCache[len], ".dat");
                    }
                }
                logINFO(LOGSRC,"Property file set to '%s'" , propertyFile);
                logINFO(LOGSRC,"Property cache set to '%s'", propertyCache);
            } else {
                // default property file not changed
            }
        } else
        if (strEqualsIgnoreCase(argv[argp], "-nopc")) {
            // -nopc    ("no property cache")
            loadPropCache = utFalse;
        } else
        if (strEqualsIgnoreCase(argv[argp], "-reboot")) {
            // may not be supported on this platform
            osReboot(); // command-line reboot does not need "startupReboot()"
            return 1;
        } else {
            break;
        }
    }
    
    /* check for existance of 'config' directory  */
    if (!ioIsDirectory(CONFIG_DIR)) {
        if (!ioExists(CONFIG_DIR)) {
            if (ioMakeDirs(CONFIG_DIR, utFalse)) {
                logDEBUG(LOGSRC,"Created CONFIG_DIR: %s ...", CONFIG_DIR);
            } else {
                logERROR(LOGSRC,"Unable to create directory: %s", CONFIG_DIR);
                return 1; // ExitProcess(1);
            }
        } else {
            logERROR(LOGSRC,"CONFIG_DIR is NOT a directory: %s", CONFIG_DIR);
            return 1; // ExitProcess(1);
        }
    }

    /* init WSA Windows extensions */
#if defined(TARGET_WINCE)
    osInitWSAStartup();
#endif

    /* initialize properties */
    // since we need to use the property manager before the main run loop starts
    // we need to initialize the property manager here.
    startupPropInitialize(loadPropCache);
    
    /* debug header */
#if defined(DEBUG_COMPILE)
    // issue warning so it can't be missed
    // ("DEBUG_COMPILE" should not be enabled in production mode!)
    logWARNING(LOGSRC,"***** Compiled with 'DEBUG_COMPILE' on!!! *****\n");
    logPRINTF(LOGSRC,SYSLOG_WARNING, "***** Compiled with 'DEBUG_COMPILE' on!!! *****");
#endif

    /* command line arguments */
    for (; argp < argc; argp++) {
        if (strEqualsIgnoreCase(argv[argp],"-help") || strEqualsIgnoreCase(argv[argp],"-h")) {
            // -h[elp]
            _usage(pgmName);
            return 0;
        } else
        if (strStartsWithIgnoreCase(argv[argp],"-ver") || strEqualsIgnoreCase(argv[argp],"-v")) {
            // -v[ersion]
            _printBanner();
            return 0; // ExitProcess(0);
        } else
        if (strEqualsIgnoreCase(argv[argp], "-printprops") || strEqualsIgnoreCase(argv[argp],"-pp")) {
            // -printprops
            propPrintProperties(stdout, utTrue);
            return 0; // ExitProcess(0);
        } else
        if (strStartsWithIgnoreCase(argv[argp],"-enc")) {
            // -enc[oding] <encoding>
            argp++;
            if ((argp < argc) && (*argv[argp] != '-')) {
                int enc = atoi(argv[argp]);
                switch (ENCODING_VALUE(enc)) {
                    case ENCODING_BINARY:
                    case ENCODING_BASE64:
                    case ENCODING_HEX:
                    case ENCODING_CSV:
                        dftEncoding = enc;
                        logINFO(LOGSRC,"Encoding set to %d\n", dftEncoding);
                        break;
                    default:
                        logCRITICAL(LOGSRC,"Unsupported encoding ...\n");
                        _usage(pgmName);
                        return 2;
                }
            } else {
                logCRITICAL(LOGSRC,"Missing encoding ...\n");
                _usage(pgmName);
                return 2;
            }
        } else
        if (strEqualsIgnoreCase(argv[argp], "-cksum")) {
            // -cksum
            dftEncoding = ENCODING_CHECKSUM(dftEncoding);
        } else
#if !defined(TRANSPORT_MEDIA_FILE) && !defined(TRANSPORT_MEDIA_SERIAL)
        if (strEqualsIgnoreCase(argv[argp], "-duplex") || strEqualsIgnoreCase(argv[argp], "-dup")) {
            // -duplex
            // WARNING: this does not verify that duplex connections are appropriate for the transport type
            propSetUInt32(PROP_COMM_MAX_SIM_EVENTS , 0L);
            UInt32 maxDupEvents = propGetUInt32(PROP_COMM_MAX_DUP_EVENTS, 1L);
            if (maxDupEvents <= 0L) {
                // make duplex connections at-least '1'
                propSetUInt32(PROP_COMM_MAX_DUP_EVENTS , 1L);
            }
        } else
#endif
#if !defined(TRANSPORT_MEDIA_SERIAL) && !defined(TRANSPORT_MEDIA_FILE)
        if (strEqualsIgnoreCase(argv[argp], "-simplex") || strEqualsIgnoreCase(argv[argp], "-sim")) {
            // -simplex
            // WARNING: this does not verify that simplex connections are appropriate for the transport type
            propSetUInt32(PROP_COMM_MAX_DUP_EVENTS , 0L);
            UInt32 maxDupEvents = propGetUInt32(PROP_COMM_MAX_SIM_EVENTS, 1L);
            if (maxDupEvents <= 0L) {
                // make simplex connections at-least '1'
                propSetUInt32(PROP_COMM_MAX_SIM_EVENTS , 1L);
            }
        } else
#endif
        if (strEqualsIgnoreCase(argv[argp], "-comlog")) {
            // -comlog
            comLog = utTrue;
        } else
        if (strEqualsIgnoreCase(argv[argp], "-gps")) {
            // -gps <port> [<model>]
            argp++;
            if ((argp < argc) && (*argv[argp] != '-')) {
                propSetString(PROP_CFG_GPS_PORT, argv[argp]);
                // parse 'model', if specified (ie. 'garmin', etc).
                if (((argp + 1) < argc) && (*argv[argp + 1] != '-')) {
                    argp++;
                    propSetString(PROP_CFG_GPS_MODEL, argv[argp]);
                }
                if (comLog) {
                    propSetBoolean(PROP_CFG_GPS_DEBUG, utTrue);
                }
            } else {
                logCRITICAL(LOGSRC,"Missing GPS port ...\n");
                _usage(pgmName);
                return 3;
            }
        } else 
#if defined(TRANSPORT_MEDIA_SOCKET) || defined(TRANSPORT_MEDIA_GPRS)
        if (strEqualsIgnoreCase(argv[argp], "-server")) {
            // -server <host> [<port>]
            argp++;
            if ((argp < argc) && (*argv[argp] != '-')) {
                propInitFromString(PROP_COMM_HOST, argv[argp]);
                // parse 'port', if specified
                if (((argp + 1) < argc) && (*argv[argp + 1] != '-')) {
                    argp++;
                    propInitFromString(PROP_COMM_PORT, argv[argp]);
                }
            } else {
                logCRITICAL(LOGSRC,"Missing host ...\n");
                _usage(pgmName);
                return 3;
            }
        } else
#endif
#if defined(TRANSPORT_MEDIA_SERIAL)
        if (strEqualsIgnoreCase(argv[argp], "-serial")) {
            // -server <port> [<bps>]
            argp++;
            if ((argp < argc) && (*argv[argp] != '-')) {
                propSetString(PROP_CFG_XPORT_PORT, argv[argp]);
                // parse 'bps', if specified
                if (((argp + 1) < argc) && (*argv[argp + 1] != '-')) {
                    argp++;
                    propInitFromString(PROP_CFG_XPORT_BPS, argv[argp]);
                }
                // debug logging
                if (comLog) {
                    propSetBoolean(PROP_CFG_XPORT_DEBUG, utTrue);
                }
                // display config
                logINFO(LOGSRC,"Setting serial protocol port: %s [%lu]\n", 
                    propGetString(PROP_CFG_XPORT_PORT,"?"), propGetUInt32(PROP_CFG_XPORT_BPS,0L));
            } else {
                logCRITICAL(LOGSRC,"Missing serial protocol port ...\n");
                _usage(pgmName);
                return 1;
            }
        } else
#endif
#if defined(TRANSPORT_MEDIA_GPRS)
        if (strEqualsIgnoreCase(argv[argp], "-gprs")) {
            // -gprs <port> [<bps>]
            argp++;
            if ((argp < argc) && (*argv[argp] != '-')) {
                propSetString(PROP_CFG_XPORT_PORT, argv[argp]);
                // parse 'bps', if specified
                if (((argp + 1) < argc) && (*argv[argp + 1] != '-')) {
                    argp++;
                    propInitFromString(PROP_CFG_XPORT_BPS, argv[argp]);
                }
                // debug logging
                if (comLog) {
                    propSetBoolean(PROP_CFG_XPORT_DEBUG, utTrue);
                }
                // display config
                logINFO(LOGSRC,"Setting GPRS modem port: %s [%lu]\n", 
                    propGetString(PROP_CFG_XPORT_PORT,"?"), propGetUInt32(PROP_CFG_XPORT_BPS,0L));
            } else {
                logCRITICAL(LOGSRC,"Missing GPRS modem port ...\n");
                _usage(pgmName);
                return 1;
            }
        } else
#endif
#if defined(TRANSPORT_MEDIA_FILE)
        if (strEqualsIgnoreCase(argv[argp], "-file")) {
            // -file <outfile>
            argp++;
            if ((argp < argc) && (*argv[argp] != '-')) {
                propSetString(PROP_CFG_XPORT_PORT, argv[argp]);
                logINFO(LOGSRC,"Setting output event data file: %s\n", 
                    propGetString(PROP_CFG_XPORT_PORT,""));
            } else {
                logCRITICAL(LOGSRC,"Missing output data file ...\n");
                _usage(pgmName);
                return 1;
            }
        } else
#endif
#if defined(ENABLE_GEOZONE) && defined(GEOZ_INCL_FILE_UPLOAD)
        if (strEqualsIgnoreCase(argv[argp], "-geoz")) {
            // -geoz <geozFile>
            argp++;
            if ((argp < argc) && ((*argv[argp] != '-') || isdigit(*(argv[argp]+1)))) {
                const char *geoZoneFile = argv[argp];
                geozUploadGeozones(geoZoneFile);
            } else {
                logERROR(LOGSRC,"Missing geozone file ...");
                _usage(pgmName);
                return 3;
            }
            return 0; // ExitProcess(0);
        } else
#endif
        {
            logCRITICAL(LOGSRC,"Unrecognized option: %s\n", argv[argp]);
            _usage(pgmName);
            return 1;
        } 
    } // "for"
    
    /* make sure we have a specified GPS port */
    portName = propGetString(PROP_CFG_GPS_PORT, "");
    if (!portName || !*portName) {
        logCRITICAL(LOGSRC,"Missing GPS port specification ...\n");
        _usage(pgmName);
        return 1;
    }
    
    /* make sure we have a specified 'server' serial port */
#if defined(TRANSPORT_MEDIA_SERIAL)
    portName = propGetString(PROP_CFG_XPORT_PORT, "");
    if (!portName || !*portName) {
        logCRITICAL(LOGSRC,"Missing serial port specification ...\n");
        _usage(pgmName);
        return 1;
    }
#endif
#if defined(TRANSPORT_MEDIA_GPRS)
    portName = propGetString(PROP_CFG_XPORT_PORT, "");
    if (!portName || !*portName) {
        logCRITICAL(LOGSRC,"Missing GPRS port specification ...\n");
        _usage(pgmName);
        return 1;
    }
#endif
#if defined(TRANSPORT_MEDIA_FILE)
    portName = propGetString(PROP_CFG_XPORT_PORT, "");
    if (!portName || !*portName) {
        logCRITICAL(LOGSRC,"Missing output file specification ...\n");
        _usage(pgmName);
        return 1;
    }
#endif

    /* header */
    _printBanner();
    
    // ------------------------------------------------------------------------
    // ***** Startup Initialization
    
    /* do we have a host:port defined? */
#if defined(TRANSPORT_MEDIA_SOCKET) || defined(TRANSPORT_MEDIA_GPRS)
    {
        const char *sockHost = propGetString(PROP_COMM_HOST, "");
        int sockPort = (int)propGetUInt32(PROP_COMM_PORT, 0L);
        hasServerHostPort = (sockHost && *sockHost && (sockPort > 0))? utTrue : utFalse;
        if (!hasServerHostPort) {
            //                "--------------------------------------------------------"
            logWARNING(LOGSRC,"*** No host:port defined, no events will be queued! ***");
        }
    }
#endif

    /* save startup time */
    // this must be called before any timers are defined
    utcMarkStartupTime();

    /* make sure properties have been initialized */
    // This must be called before any access is made to the property manager
    //propInitialize(utFalse); // only initialize here if not already initialized

    /* event queue initializer */
    // this must be called before event packects are defined, or events are generated
    evInitialize();
    acctInitialize();
    /* custom event record */
#if defined(CUSTOM_EVENT_PACKETS)
    // init custom event formats here (add before events can be generated)
#endif

    /* thread initializer */
    // this must be called before threads are created/started
    threadInitialize();
    
    /* set threaded logging (if so configured) */
    // (should be called before any threads are started)
    // (will return false if logging doesn't support running in a separate thread)
#if !defined(TARGET_CYGWIN)
    logStartThread();
#endif

    /* main loop initialization */
    // This initializes/starts all the component threads.
    mainLoopInitialize(&_customAddEventFtn);

    /* property save interval */
#if defined(PROPERTY_SAVE_INTERVAL) && defined(PROPERTY_FILE)
    // first property save expiration in FIRST_PROPERTY_SAVE_INTERVAL seconds
    lastSavePropertyTimer = utcGetTimer();
    //startupSaveProperties(); <-- will be saved soon
#endif

    /* other inits here */
    // Add other custom initializations here

    /* main loop */
    mainLoopRun(dftEncoding, (runInThread?utTrue:utFalse)); // run in thread
    return runInThread? 0 : 4;

}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// 'main' entry point

#if !defined(TARGET_WINCE)
// WinCE uses its own entry point
int main(int argc, char *argv[]) {
    return startupDMTP(argc,argv,(int)utFalse); // don't run in thread
}
#endif

// ----------------------------------------------------------------------------

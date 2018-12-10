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
//  Property manager
//  Handles typed properties needed by the protocol.
//  Note: this module has not yet been made thread-safe.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/17  Martin D. Flynn
//     -Changed property keyname strings
//     -Added PROP_GEOF_ARRIVE_DELAY/PROP_GEOF_DEPART_DELAY properties
//  2006/01/29  Martin D. Flynn
//     -Force KVA_NONDEFAULT attributes on properties loaded from a file
//     -Changed property save to default saving with key-name (rather than key-id)
//     -Added "propClearChanged()" function for clearing property 'changed' state.
//  2006/02/16  Martin D. Flynn
//     -Removed 'SAVE' attribute from config properties that should remain static.
//  2006/04/06  Martin D. Flynn
//     -Changed communication property defaults
//  2006/05/07  Martin D. Flynn
//     -Added PROP_CMD_GEOF_ADMIN property.
//     -Added PROP_GEOF_COUNT property.
//     -Added PROP_GEOF_VERSION property.
//  2006/07/13  Martin D. Flynn
//     -Updated DFT_PROTOCOL_VERSION to reflect protocol document version change.
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
//     -Added several new properties (see 'props.h')
//     -Added 'propSetSave' function to override property 'save' attribute
//     -Replaced DFT_PROTOCOL_VERSION with PROTOCOL_VERSION (set in 'defaults.h')
//     -Changed the following properties to store odometer values in 1 meter units:
//      PROP_GPS_ACCURACY, PROP_GPS_DISTANCE_DELTA, 
//      PROP_ODOMETER_#_VALUE, PROP_ODOMETER_#_LIMIT
//     -Changed the following properties to store elapsed time values in seconds:
//      PROP_ELAPSED_#_VALUE, PROP_ELAPSED_#_LIMIT
//     -Property key lookups now use a binary search.
//  2007/04/28  Martin D. Flynn
//     -Change default PROP_COMM_HOST to an empty string ("").  While using "localhost"
//      as the default for debugging purposes, it doesn't make sense in an embedded
//      client.  This value should be explicitly defined/set in the 'props.conf' file.
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>

#include "custom/log.h"

#include "tools/strtools.h"
#include "tools/gpstools.h"
#include "tools/utctools.h"
#include "tools/bintools.h"
#include "tools/io.h"

#include "base/propman.h"

#include "base/packet.h"
// ----------------------------------------------------------------------------

#if defined(TARGET_WINCE)
// poor-mans 'rint' (WinCE doesn't seem to have it's own 'rint' function)
#define RINT(X)         (UInt32)((X) + 0.5)
#else
#define RINT(X)         rint(X)
#endif

// ----------------------------------------------------------------------------

#define PROP_LOCK       // implement property manager locking
#define PROP_UNLOCK     // implement property manager unlocking

// ----------------------------------------------------------------------------
// These are the default settings for specific property values that may need
// to be customized for a particular client application.  During client app
// initialization the appropriate calls to the following function should be made.
//    "propInitFromString(<PopKey>, <StringValue>);"
// should be called.

//#define DFT_FIRMWARE_VERSION    "DMTP_C_" RELEASE_VERSION // default firmware version
#define DFT_FIRMWARE_VERSION    APPLICATION_NAME "_C." RELEASE_VERSION

#define DFT_COMM_HOST           "" // (was "localhost")
#define DFT_COMM_PORT           "31000" // this default may change at any time

#define DFT_ACCESS_PIN          "0x3132333435363738"    // "12345678"

// 'true' to save with key name, 'false' to save with key code
#define PROP_SAVE_KEY_NAME      utTrue

// ----------------------------------------------------------------------------

#define WO                      (KVA_HIDDEN | KVA_WRITEONLY)
#define RO                      (KVA_READONLY)
#define SAVE                    (KVA_SAVE)

#define UInt32_to_Double(U,T)   ((double)(U) / pow(10.0,(double)KVT_DEC(T)))
#define Double_to_UInt32(D,T)   ((UInt32)RINT((D) * pow(10.0,(double)KVT_DEC(T))))

// ----------------------------------------------------------------------------
// Property definition table
// Notes:
//  - The values in the 'name' column are arbitrary, and are just suggestions in the
//    OpenDMTP protocol.  As such, they may change at any time without notice.  If you
//    rely on the specified property 'name' in your property files, make sure you double 
//    check your property files against the names specified here at the time you plan to 
//    upgrade your code to the newest version.

static utBool     binarySearchOK = utFalse; // false, until verified

static KeyValue_t properties[] = {

#ifdef CUSTOM_PROPERTIES
#include "custom/custprop.inc"
#endif

    //KeyCode                      Name               Type                   Attr       Ndx  Init
    // --- local serial port configuration
    { PROP_CFG_XPORT_PORT        , "cfg.xpo.port"   , KVT_STRING            , RO       ,  1,  "" },
    { PROP_CFG_XPORT_BPS         , "cfg.xpo.bps"    , KVT_UINT32            , RO       ,  1,  "" },
    { PROP_CFG_XPORT_DEBUG       , "cfg.xpo.debug"  , KVT_BOOLEAN           , RO       ,  1,  "0" },
    { PROP_CFG_GPS_PORT          , "cfg.gps.port"   , KVT_STRING            , RO       ,  1,  "" },
    { PROP_CFG_GPS_BPS           , "cfg.gps.bps"    , KVT_UINT32            , RO       ,  1,  "4800" },
    { PROP_CFG_GPS_MODEL         , "cfg.gps.model"  , KVT_STRING            , RO       ,  1,  "" },
    { PROP_CFG_GPS_DEBUG         , "cfg.gps.debug"  , KVT_BOOLEAN           , RO       ,  1,  "0" },
    { PROP_CFG_SERIAL0_PORT      , "cfg.sp0.port"   , KVT_STRING            , RO       ,  1,  "" },
    { PROP_CFG_SERIAL0_BPS       , "cfg.sp0.bps"    , KVT_UINT32            , RO       ,  1,  "" },
    { PROP_CFG_SERIAL0_DEBUG     , "cfg.sp0.debug"  , KVT_BOOLEAN           , RO       ,  1,  "0" },
    { PROP_CFG_SERIAL1_PORT      , "cfg.sp1.port"   , KVT_STRING            , RO       ,  1,  "" },
    { PROP_CFG_SERIAL1_BPS       , "cfg.sp1.bps"    , KVT_UINT32            , RO       ,  1,  "" },
    { PROP_CFG_SERIAL1_DEBUG     , "cfg.sp1.debug"  , KVT_BOOLEAN           , RO       ,  1,  "0" },
    { PROP_CFG_SERIAL2_PORT      , "cfg.sp2.port"   , KVT_STRING            , RO       ,  1,  "" },
    { PROP_CFG_SERIAL2_BPS       , "cfg.sp2.bps"    , KVT_UINT32            , RO       ,  1,  "" },
    { PROP_CFG_SERIAL2_DEBUG     , "cfg.sp2.debug"  , KVT_BOOLEAN           , RO       ,  1,  "0" },
    { PROP_CFG_SERIAL3_PORT      , "cfg.sp3.port"   , KVT_STRING            , RO       ,  1,  "" },
    { PROP_CFG_SERIAL3_BPS       , "cfg.sp3.bps"    , KVT_UINT32            , RO       ,  1,  "" },
    { PROP_CFG_SERIAL3_DEBUG     , "cfg.sp3.debug"  , KVT_BOOLEAN           , RO       ,  1,  "0" },

    // --- miscellaneous commands
    { PROP_CMD_SAVE_PROPS        , "cmd.saveprops"  , KVT_COMMAND           , WO       ,  1,  0 },
    { PROP_CMD_AUTHORIZE         , "cmd.auth"       , KVT_COMMAND           , WO       ,  1,  0 },
    { PROP_CMD_STATUS_EVENT      , "cmd.status"     , KVT_COMMAND           , WO       ,  1,  0 },
    { PROP_CMD_SET_OUTPUT        , "cmd.output"     , KVT_COMMAND           , WO       ,  1,  0 },
    { PROP_CMD_RESET             , "cmd.reset"      , KVT_COMMAND           , WO       ,  1,  0 },

    // --- retained state properties
    { PROP_STATE_PROTOCOL        , "sta.proto"      , KVT_UINT8             , RO       ,  3,  PROTOCOL_VERSION },
    { PROP_STATE_FIRMWARE        , "sta.firm"       , KVT_STRING            , RO       ,  1,  DFT_FIRMWARE_VERSION },
    { PROP_STATE_COPYRIGHT       , "sta.copyright"  , KVT_STRING            , RO       ,  1,  "" },
    { PROP_STATE_SERIAL          , "sta.serial"     , KVT_STRING            , RO       ,  1,  "" },
    { PROP_STATE_UNIQUE_ID       , "sta.uniq"       , KVT_BINARY            , RO       , 30,  "" },
    { PROP_STATE_ACCOUNT_ID      , "sta.account"    , KVT_STRING            , RO       ,  1,  "" },
    { PROP_STATE_DEVICE_ID       , "sta.device"     , KVT_STRING            , RO       ,  1,  "" },
#if defined(SECONDARY_SERIAL_TRANSPORT)
    { PROP_STATE_DEVICE_BT       , "sta.device.bt"  , KVT_STRING            ,    SAVE  ,  1,  "" },
#endif
    { PROP_STATE_USER_ID         , "sta.user"       , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_STATE_USER_TIME       , "sta.user.time"  , KVT_UINT32            , RO|SAVE  ,  1,  "0" },
    { PROP_STATE_TIME            , "sta.time"       , KVT_UINT32            , RO       ,  1,  "0" },
    { PROP_STATE_GPS             , "sta.gpsloc"     , KVT_GPS               , RO|SAVE  ,  1,  "" }, 
    { PROP_STATE_GPS_DIAGNOSTIC  , "sta.gpsdiag"    , KVT_UINT32            , RO       ,  5,  "0,0,0,0,0" }, 
    { PROP_STATE_QUEUED_EVENTS   , "sta.evtqueue"   , KVT_UINT32            , RO       ,  2,  "0,0" }, 
    { PROP_STATE_DEV_DIAGNOSTIC  , "sta.devdiag"    , KVT_UINT32            , RO|SAVE  ,  5,  "0,0,0,0,0" }, 

    // --- Communication protocol properties
    { PROP_COMM_SPEAK_FIRST      , "com.first"      , KVT_BOOLEAN           ,    SAVE  ,  1,  "1" },
    { PROP_COMM_FIRST_BRIEF      , "com.brief"      , KVT_BOOLEAN           ,    SAVE  ,  1,  "0" },
    { PROP_COMM_MAX_CONNECTIONS  , "com.maxconn"    , KVT_UINT8             ,    SAVE  ,  3,  "8,4,60" }, // total/duplex/minutes
    { PROP_COMM_MIN_XMIT_DELAY   , "com.mindelay"   , KVT_UINT16            ,    SAVE  ,  1,  "180" },
    { PROP_COMM_MIN_XMIT_RATE    , "com.minrate"    , KVT_UINT32            ,    SAVE  ,  1,  "180" },
    { PROP_COMM_MAX_XMIT_RATE    , "com.maxrate"    , KVT_UINT32            ,    SAVE  ,  1,  "3600" },
    { PROP_COMM_MAX_DUP_EVENTS   , "com.maxduplex"  , KVT_UINT8             ,    SAVE  ,  1,  "10" },
    { PROP_COMM_MAX_SIM_EVENTS   , "com.maxsimplex" , KVT_UINT8             ,    SAVE  ,  1,  "2" },

    // --- Communication connection properties
    { PROP_COMM_SETTINGS         , "com.settings"   , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_HOST             , "com.host"       , KVT_STRING            ,    SAVE  ,  1,  DFT_COMM_HOST },
    { PROP_COMM_PORT             , "com.port"       , KVT_UINT16            ,    SAVE  ,  1,  DFT_COMM_PORT },
    { PROP_COMM_DNS_1            , "com.dns1"       , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_DNS_2            , "com.dns2"       , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_CONNECTION       , "com.connection" , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_APN_NAME         , "com.apnname"    , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_APN_SERVER       , "com.apnserv"    , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_APN_USER         , "com.apnuser"    , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_APN_PASSWORD     , "com.apnpass"    , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_APN_PHONE        , "com.apnphone"   , KVT_STRING            ,    SAVE  ,  1,  "" }, // "*99***1#"
    { PROP_COMM_APN_SETTINGS     , "com.apnsett"    , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_COMM_MIN_SIGNAL       , "com.minsignal"  , KVT_INT16             ,    SAVE  ,  1,  "7" },
    { PROP_COMM_ACCESS_PIN       , "com.pin"        , KVT_BINARY            ,    SAVE  ,  8,  DFT_ACCESS_PIN },

    // --- Packet/Data format properties
    { PROP_COMM_CUSTOM_FORMATS   , "com.custfmt"    , KVT_UINT8             ,    SAVE  ,  1,  "0" },
    { PROP_COMM_ENCODINGS        , "com.encodng"    , KVT_UINT8             ,    SAVE  ,  1,  "0x7" },
    { PROP_COMM_BYTES_READ       , "com.rdcnt"      , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_COMM_BYTES_WRITTEN    , "com.wrcnt"      , KVT_UINT32            ,    SAVE  ,  1,  "0" },

    // --- GPS properties
    { PROP_GPS_SAMPLE_RATE       , "gps.smprate"    , KVT_UINT16            ,    SAVE  ,  1,  "7" },
    { PROP_GPS_AQUIRE_WAIT       , "gps.aquwait"    , KVT_UINT16            ,    SAVE  ,  1,  "0" },
    { PROP_GPS_EXPIRATION        , "gps.expire"     , KVT_UINT16            ,    SAVE  ,  1,  "300" },
    { PROP_GPS_CLOCK_DELTA       , "gps.updclock"   , KVT_BOOLEAN           ,    SAVE  ,  1,  "15" },
    { PROP_GPS_ACCURACY          , "gps.accuracy"   , KVT_UINT16            ,    SAVE  ,  1,  "0" },
    { PROP_GPS_MIN_SPEED         , "gps.minspd"     , KVT_UINT16|KVT_DEC(1) ,    SAVE  ,  1,  "8.0" },
    { PROP_GPS_DISTANCE_DELTA    , "gps.dstdelt"    , KVT_UINT32            ,    SAVE  ,  1,  "500" },

    // --- GeoZone properties
    { PROP_CMD_GEOF_ADMIN        , "gf.admin"       , KVT_COMMAND           , WO       ,  1,  0 },
    { PROP_GEOF_COUNT            , "gf.count"       , KVT_UINT16            , RO       ,  1,  "0" },
    { PROP_GEOF_VERSION          , "gf.version"     , KVT_STRING            ,    SAVE  ,  1,  "" },
    { PROP_GEOF_ARRIVE_DELAY     , "gf.arr.delay"   , KVT_UINT32            ,    SAVE  ,  1,  "30" }, 
    { PROP_GEOF_DEPART_DELAY     , "gf.dep.delay"   , KVT_UINT32            ,    SAVE  ,  1,  "10" }, 
    { PROP_GEOF_CURRENT          , "gf.current"     , KVT_UINT32            ,    SAVE  ,  1,  "0" }, 

    // --- GeoCorr properties
    { PROP_CMD_GEOC_ADMIN        , "gc.admin"       , KVT_COMMAND           , WO       ,  1,  0 },
    { PROP_GEOC_ACTIVE_ID        , "gc.active"      , KVT_UINT32            ,    SAVE  ,  1,  "0" }, 
    { PROP_GEOC_VIOLATION_INTRVL , "gc.vio.rate"    , KVT_UINT16            ,    SAVE  ,  1,  "300" }, 
    { PROP_GEOC_VIOLATION_COUNT  , "gc.vio.cnt"     , KVT_UINT16            ,    SAVE  ,  1,  "0" }, 

    // --- Motion properties
    { PROP_MOTION_START_TYPE     , "mot.start.type" , KVT_UINT8             ,    SAVE  ,  1,  "0" },
    { PROP_MOTION_START          , "mot.start"      , KVT_UINT16|KVT_DEC(1) ,    SAVE  ,  1,  "0.0" },
    { PROP_MOTION_IN_MOTION      , "mot.inmotion"   , KVT_UINT16            ,    SAVE  ,  1,  "0" },
    { PROP_MOTION_STOP           , "mot.stop"       , KVT_UINT16            ,    SAVE  ,  1,  "600" },
    { PROP_MOTION_STOP_TYPE      , "mot.stop.type"  , KVT_UINT8             ,    SAVE  ,  1,  "0" },
    { PROP_MOTION_DORMANT_INTRVL , "mot.dorm.rate"  , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_MOTION_DORMANT_COUNT  , "mot.dorm.cnt"   , KVT_UINT16            ,    SAVE  ,  1,  "1" },
    { PROP_MOTION_EXCESS_SPEED   , "mot.exspeed"    , KVT_UINT16|KVT_DEC(1) ,    SAVE  ,  1,  "0.0" },
    { PROP_MOTION_MOVING_INTRVL  , "mot.moving"     , KVT_UINT16            ,    SAVE  ,  1,  "0" },

    // --- Odometer properties
    { PROP_ODOMETER_0_VALUE      , "odo.0.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_1_VALUE      , "odo.1.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_2_VALUE      , "odo.2.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_3_VALUE      , "odo.3.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_4_VALUE      , "odo.4.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_5_VALUE      , "odo.5.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_6_VALUE      , "odo.6.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_7_VALUE      , "odo.7.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_0_LIMIT      , "odo.0.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_1_LIMIT      , "odo.1.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_2_LIMIT      , "odo.2.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_3_LIMIT      , "odo.3.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_4_LIMIT      , "odo.4.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_5_LIMIT      , "odo.5.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_6_LIMIT      , "odo.6.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_7_LIMIT      , "odo.7.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ODOMETER_0_GPS        , "odo.0.gps"      , KVT_GPS               , RO|SAVE  ,  1,  "0" },
    { PROP_ODOMETER_1_GPS        , "odo.1.gps"      , KVT_GPS               , RO|SAVE  ,  1,  "0" },
    { PROP_ODOMETER_2_GPS        , "odo.2.gps"      , KVT_GPS               , RO|SAVE  ,  1,  "0" },
    { PROP_ODOMETER_3_GPS        , "odo.3.gps"      , KVT_GPS               , RO|SAVE  ,  1,  "0" },
    { PROP_ODOMETER_4_GPS        , "odo.4.gps"      , KVT_GPS               , RO|SAVE  ,  1,  "0" },
    { PROP_ODOMETER_5_GPS        , "odo.5.gps"      , KVT_GPS               , RO|SAVE  ,  1,  "0" },
    { PROP_ODOMETER_6_GPS        , "odo.6.gps"      , KVT_GPS               , RO|SAVE  ,  1,  "0" },
    { PROP_ODOMETER_7_GPS        , "odo.7.gps"      , KVT_GPS               , RO|SAVE  ,  1,  "0" },

    // --- Digital input properties
    { PROP_INPUT_STATE           , "inp.state"      , KVT_UINT32            , RO       ,  1,  "0" }, // DON"T SAVE
    { PROP_INPUT_CONFIG_0        , "inp.0.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_INPUT_CONFIG_1        , "inp.1.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_INPUT_CONFIG_2        , "inp.2.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_INPUT_CONFIG_3        , "inp.3.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_INPUT_CONFIG_4        , "inp.4.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_INPUT_CONFIG_5        , "inp.5.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_INPUT_CONFIG_6        , "inp.6.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_INPUT_CONFIG_7        , "inp.7.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
  //{ PROP_INPUT_CONFIG_8        , "inp.8.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
  //{ PROP_INPUT_CONFIG_9        , "inp.9.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
  //{ PROP_INPUT_CONFIG_A        , "inp.A.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
  //{ PROP_INPUT_CONFIG_B        , "inp.B.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
  //{ PROP_INPUT_CONFIG_C        , "inp.C.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
  //{ PROP_INPUT_CONFIG_D        , "inp.D.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
  //{ PROP_INPUT_CONFIG_E        , "inp.E.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
  //{ PROP_INPUT_CONFIG_F        , "inp.F.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },

    // --- Digitaloutput properties
    { PROP_OUTPUT_CONFIG_0       , "out.0.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_OUTPUT_CONFIG_1       , "out.1.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_OUTPUT_CONFIG_2       , "out.2.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_OUTPUT_CONFIG_3       , "out.3.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_OUTPUT_CONFIG_4       , "out.4.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_OUTPUT_CONFIG_5       , "out.5.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_OUTPUT_CONFIG_6       , "out.6.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_OUTPUT_CONFIG_7       , "out.7.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },

    // --- Elapsed time properties
    { PROP_ELAPSED_0_VALUE       , "ela.0.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_1_VALUE       , "ela.1.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_2_VALUE       , "ela.2.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_3_VALUE       , "ela.3.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_4_VALUE       , "ela.4.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_5_VALUE       , "ela.5.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_6_VALUE       , "ela.6.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_7_VALUE       , "ela.7.value"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_0_LIMIT       , "ela.0.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_1_LIMIT       , "ela.1.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_2_LIMIT       , "ela.2.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_3_LIMIT       , "ela.3.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_4_LIMIT       , "ela.4.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_5_LIMIT       , "ela.5.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_6_LIMIT       , "ela.6.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_ELAPSED_7_LIMIT       , "ela.7.limit"    , KVT_UINT32            ,    SAVE  ,  1,  "0" },

    // --- Generic sensor properties
    { PROP_UNDERVOLTAGE_LIMIT    , "bat.limit"      , KVT_UINT32            ,    SAVE  ,  1,  "0" },
    { PROP_SENSOR_CONFIG_0       , "sen.0.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_CONFIG_1       , "sen.1.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_CONFIG_2       , "sen.2.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_CONFIG_3       , "sen.3.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_CONFIG_4       , "sen.4.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_CONFIG_5       , "sen.5.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_CONFIG_6       , "sen.6.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_CONFIG_7       , "sen.7.conf"     , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_RANGE_0        , "sen.0.range"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_RANGE_1        , "sen.1.range"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_RANGE_2        , "sen.2.range"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_RANGE_3        , "sen.3.range"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_RANGE_4        , "sen.4.range"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_RANGE_5        , "sen.5.range"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_RANGE_6        , "sen.6.range"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_SENSOR_RANGE_7        , "sen.7.range"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },

    // --- Temperature properties
    { PROP_TEMP_SAMPLE_INTRVL    , "tmp.smprate"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_TEMP_REPORT_INTRVL    , "tmp.rptrate"    , KVT_UINT32            ,    SAVE  ,  2,  "0,0" },
    { PROP_TEMP_CONFIG_0         , "tmp.0.conf"     , KVT_INT16             ,    SAVE  ,  2,  "0,0" },
    { PROP_TEMP_CONFIG_1         , "tmp.1.conf"     , KVT_INT16             ,    SAVE  ,  2,  "0,0" },
    { PROP_TEMP_CONFIG_2         , "tmp.2.conf"     , KVT_INT16             ,    SAVE  ,  2,  "0,0" },
    { PROP_TEMP_CONFIG_3         , "tmp.3.conf"     , KVT_INT16             ,    SAVE  ,  2,  "0,0" },
    { PROP_TEMP_RANGE_0          , "tmp.0.range"    , KVT_INT16|KVT_DEC(1)  ,    SAVE  ,  2,  "0.0,0.0" },
    { PROP_TEMP_RANGE_1          , "tmp.1.range"    , KVT_INT16|KVT_DEC(1)  ,    SAVE  ,  2,  "0.0,0.0" },
    { PROP_TEMP_RANGE_2          , "tmp.2.range"    , KVT_INT16|KVT_DEC(1)  ,    SAVE  ,  2,  "0.0,0.0" },
    { PROP_TEMP_RANGE_3          , "tmp.3.range"    , KVT_INT16|KVT_DEC(1)  ,    SAVE  ,  2,  "0.0,0.0" },

};

#define PROP_COUNT      (sizeof(properties) / sizeof(properties[0]))

// ----------------------------------------------------------------------------
// forward definitions

static void _propInitKeyValueFromString(KeyValue_t *kv, const char *s, utBool internal);

// ----------------------------------------------------------------------------
// Property table accessor functions

/* get KeyValue_t entry at specified index */
KeyValue_t *propGetKeyValueAt(int ndx)
{
    if ((ndx >= 0) && (ndx < PROP_COUNT)) {
        return &properties[ndx];
    }
    return (KeyValue_t*)0;
}

/* get KeyValue entry for specified key */
KeyValue_t *propGetKeyValueEntry(Key_t key)
{
    if (binarySearchOK) {
        // binary search (PROPERTY KEYS MUST BE IN ASCENDING ORDER!)
        int s = 0, e = PROP_COUNT;
        for (;s < e;) {
            int b = (e + s) / 2;
            KeyValue_t *kv = &properties[b];
            if (kv->key == key) {
                return kv;
            } else
            if (key < kv->key) {
                e = b;
            } else {
                s = b + 1;
            }
        }
    } else {
        // linear search
        int i;
        for (i = 0; i < PROP_COUNT; i++) {
            KeyValue_t *kv = &properties[i];
            if (kv->key == key) {
                return kv;
            }
        }
    }
    logWARNING(LOGSRC,"Key not found: 0x%04X", key);
    return (KeyValue_t*)0;
}

/* get KeyValue entry for specified key */
KeyValue_t *propGetKeyValueEntryByName(const char *keyName)
{
    // could stand some optimizing
    int i;
    for (i = 0; i < PROP_COUNT; i++) {
        KeyValue_t *kv = &properties[i];
        if (strEqualsIgnoreCase(kv->name,keyName)) {
            return kv;
        }
    }
    return (KeyValue_t*)0;
}

// ----------------------------------------------------------------------------

static KeyData_t *_propGetData(KeyValue_t *kv)
{
    if (!kv) {
        // return null if the KeyValue pointer is invalid
        return (KeyData_t*)0;
    } else
    if (kv->type & KVT_POINTER) {
        // never return null if the KeyValue pointer itself is valid
        return (kv->data.p.buf)? (KeyData_t*)kv->data.p.buf : (KeyData_t*)"";
    } else {
        return &(kv->data);
    }
}

static UInt16 _propGetDataCapacity(KeyValue_t *kv)
{
    if (!kv) {
        return 0;
    } else
    if (kv->type & KVT_POINTER) {
        return (kv->data.p.buf)? kv->data.p.bufLen : 0;
    } else {
        return sizeof(kv->data);
    }
}

// ----------------------------------------------------------------------------
// Property table flag modification

// set KVA_READONLY state
utBool propSetReadOnly(Key_t key, utBool readOnly)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (kv) {
        if (readOnly) {
            kv->attr |= KVA_READONLY;
        } else {
            kv->attr &= ~KVA_READONLY;
        }
        return utTrue;
    }
    return utFalse;
}

// set KVA_SAVE state
utBool propSetSave(Key_t key, utBool save)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (kv) {
        if (save) {
            kv->attr |= KVA_SAVE;
        } else {
            kv->attr &= ~KVA_SAVE;
        }
        return utTrue;
    }
    return utFalse;
}

// ----------------------------------------------------------------------------
// Property table alternate buffer support

utBool propSetAltBuffer(Key_t key, void *buf, int bufLen)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (kv) {
        kv->type         |= KVT_POINTER;
        kv->data.p.buf    = buf;
        kv->data.p.bufLen = bufLen;
        return utTrue;
    }
    return utFalse;
}

/* return pointer to data */
void *propGetPointer(Key_t key, UInt16 *maxLen, UInt16 *dtaLen)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        if (maxLen) { *maxLen = 0; }
        if (dtaLen) { *dtaLen = 0; }
        return (UInt8*)0;
    } else {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        if (maxLen) { *maxLen = keyDataSiz; }
        if (dtaLen) { *dtaLen = kv->dataSize; }
        return (void*)keyDataBuf;
    }
}

// ----------------------------------------------------------------------------
// type conversion functions

/* encode GPS value into byte array */
static int _propEncodeGPS(GPSOdometer_t *gps, FmtBuffer_t *dest)
{
    if (BUFFER_DATA(dest) && (BUFFER_DATA_SIZE(dest) >= 4)) {
        binFmtPrintf(dest, "%4u", (UInt32)(gps?gps->fixtime:0L));
        int destLen = BUFFER_DATA_SIZE(dest);
        if ((destLen >= 16) || ((destLen >= 12) && (destLen < 14))) {
            gpsPointEncode8(BUFFER_DATA(dest), (gps?&(gps->point):0));
            binAppendFmtField(dest, 8, 'g');
            binAdvanceFmtBuffer(dest, 8);
            if (destLen >= 16) {
                binFmtPrintf(dest, "%4u", (UInt32)(gps?gps->meters:0L));
            }
        } else
        if ((destLen >= 14) || ((destLen >= 10) && (destLen < 12))) {
            gpsPointEncode6(BUFFER_DATA(dest), (gps?&(gps->point):0));
            binAppendFmtField(dest, 6, 'g');
            binAdvanceFmtBuffer(dest, 6);
            if (destLen >= 14) {
                binFmtPrintf(dest, "%4u", (UInt32)(gps?gps->meters:0L));
            }
        }
        return BUFFER_DATA_LENGTH(dest);
    } else {
        // invalid data length
        return -1;
    }
}

/* decode GPS value from byte array */
static int _propDecodeGPS(GPSOdometer_t *gps, const UInt8 *data , int dataLen)
{
    // Valid lengths:
    //   10/14 - low resolution GPS fix
    //   12/16 - high resolution GPS fix
    if (gps && data && (dataLen >= 4)) {
        gps->fixtime = binDecodeInt32(data, 4, utFalse);
        gpsPointClear(&(gps->point));
        gps->meters = 0L;
        int len = 4;
        if ((dataLen == 10) || (dataLen == 14)) {
            gpsPointDecode6(&(gps->point), &data[len]);
            len += 6;
            if (dataLen == 14) {
                gps->meters = binDecodeInt32(&data[len], 4, utFalse);
                len += 4;
            }
        } else
        if ((dataLen == 12) || (dataLen == 16)) {
            gpsPointDecode8(&(gps->point), &data[len]);
            len += 8;
            if (dataLen == 16) {
                gps->meters = binDecodeInt32(&data[len], 4, utFalse);
                len += 4;
            }
        }
        return len;
    } else {
        return -1;
    }
}

// ----------------------------------------------------------------------------
// Property value refresh/change notification

static void (*propFtn_PropertyGet)(PropertyRefresh_t mode, Key_t key, UInt8 *args, int argLen);
static void (*propFtn_PropertySet)(PropertyRefresh_t mode, Key_t key, UInt8 *args, int argLen);

static void _propRefresh(PropertyRefresh_t mode, KeyValue_t *kv, UInt8 *args, int argLen)
{
    if (kv) {
        if (propFtn_PropertyGet && (mode & PROP_REFRESH_GET)) {
            (*propFtn_PropertyGet)(PROP_REFRESH_GET, kv->key, args, argLen);
        }
        if (propFtn_PropertySet && (mode & PROP_REFRESH_SET)) {
            (*propFtn_PropertySet)(PROP_REFRESH_SET, kv->key, (UInt8*)0, 0);
        }
    }
}

void propSetNotifyFtn(PropertyRefresh_t mode, void (*ftn)(PropertyRefresh_t,Key_t,UInt8*,int))
{
    if (mode & PROP_REFRESH_GET) {
        propFtn_PropertyGet = ftn;
    }
    if (mode & PROP_REFRESH_SET) {
        propFtn_PropertySet = ftn;
    }
}

// ----------------------------------------------------------------------------
// Property table command handler support

utBool propSetCommandFtn(Key_t key, CommandError_t (*cmd)(int pi, Key_t key, const UInt8 *data, int dataLen))
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (kv && (KVT_TYPE(kv->type) == KVT_COMMAND)) {
        KeyData_t *keyDataBuf = _propGetData(kv);
        keyDataBuf->cmd = cmd;
        kv->lenNdx      = 1;
        kv->dataSize    = sizeof(keyDataBuf->cmd); // sizeof(void*)
        return utTrue;
    }
    return utFalse;
}

// This is a sample command type handler
int _sampleCommandHandler(Key_t key, const void *data, int dataLen)
{
    logDEBUG(LOGSRC,"\nExecute command for key 0x%04X ...\n", key);
    return 0; // bytes used from 'data'
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// This section implements functions used to access property values from within code.

/* return maximum index size */
Int16 propGetIndexSize(Key_t key)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    return kv? (Int16)kv->maxNdx : -1;
}

// ----------------------------------------------------------------------------

/* get a 32-bit value for the specified KeyValue entry */
static utBool _propGetUInt32Value(KeyValue_t *kv, int ndx, UInt32 *val)
{
    if (!val) {
        // no place to put value
        return utFalse;
    } else
    if ((ndx < 0) || (ndx >= kv->lenNdx)) {
        // invalid index
        return utFalse;
    } else
    if (KVT_IS_UINT(kv->type)) {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        if (keyDataSiz >= ((ndx + 1) * sizeof(keyDataBuf->i[0]))) {
            *val = keyDataBuf->i[ndx];
            return utTrue;
        } else {
            // insufficient size for index
            //logWARNING(LOGSRC,"Insufficient size: %d 0x%04X\n", ndx, keyDataSiz);
            return utFalse;
        }
    } else {
        // unsupported type
        //logWARNING(LOGSRC,"Unsupported type: 0x%04X\n", kv->type);
        return utFalse;
    }
}

/* set a 32-bit value for the specified KeyValue entry */
static utBool _propSetUInt32Value(KeyValue_t *kv, int ndx, UInt32 *val)
{
    if (!val) {
        // no specified value
        return utFalse;
    } else
    if ((ndx < 0) || (ndx >= kv->maxNdx)) {
        // invalid index
        return utFalse;
    } else
    if (KVT_IS_UINT(kv->type)) {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        if (keyDataSiz >= ((ndx + 1) * sizeof(keyDataBuf->i[0]))) {
            keyDataBuf->i[ndx] = *val;
            if (kv->lenNdx <= ndx) { 
                kv->lenNdx = ndx + 1;
                kv->dataSize = kv->lenNdx * KVT_UINT_SIZE(kv->type);
            }
            kv->attr |= KVA_NONDEFAULT; 
            kv->attr |= KVA_CHANGED; // set changed
            return utTrue;
        } else {
            // insufficient size
            return utFalse;
        }
    } else {
        // unsupported type
        return utFalse;
    }
}

/* get a 32-bit value for the specified key at the specified index */
UInt32 propGetUInt32AtIndex(Key_t key, int ndx, UInt32 dft)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        return dft;
    } else {
        UInt32 val32 = dft;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // no args
            if (kv->lenNdx < (ndx + 1)) {
                val32 = dft;
            } else {
                UInt32 v;
                utBool ok = _propGetUInt32Value(kv, ndx, &v);
                val32 = ok? v : dft;
            }
        } PROP_UNLOCK
        return val32;
    }
}

/* set a 32-bit value for the specified key at the specified index */
utBool propSetUInt32AtIndex(Key_t key, int ndx, UInt32 val)
{
    return propSetUInt32AtIndexRefresh(key, ndx, val, utTrue);
}

/* set a 32-bit value for the specified key at the specified index */
utBool propSetUInt32AtIndexRefresh(Key_t key, int ndx, UInt32 val, utBool refresh)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (kv) {
        if (kv->maxNdx < (ndx + 1)) {
            return utFalse;
        } else {
            utBool ok;
            PROP_LOCK {
                ok = _propSetUInt32Value(kv, ndx, &val);
                if (ok && refresh) { 
                    _propRefresh(PROP_REFRESH_SET, kv, (UInt8*)0, 0);
                }
            } PROP_UNLOCK
            return ok;
        }
    } else {
        return utFalse;
    }
}

/* add a 32-bit value to the specified key at the specified index */
utBool propAddUInt32AtIndex(Key_t key, int ndx, UInt32 *val)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        return utFalse;
    } else {
        utBool ok = utFalse;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // no args
            if (kv->lenNdx < (ndx + 1)) {
                // index is beyond available entries
                ok = utFalse;
            } else {
                UInt32 oldVal;
                utBool ok = _propGetUInt32Value(kv, ndx, &oldVal);
                if (ok) {
                    *val += oldVal;
                    ok = _propSetUInt32Value(kv, ndx, val);
                    if (ok) { _propRefresh(PROP_REFRESH_SET, kv, (UInt8*)0, 0); }
                } else {
                    ok = utFalse;
                }
            }
        } PROP_UNLOCK
        return ok;
    }
}

/* get a 32-bit value for the specified key */
UInt32 propGetUInt32(Key_t key, UInt32 dft)
{
    return propGetUInt32AtIndex(key, 0, dft);
}

/* set a 32-bit value for the specified key */
utBool propSetUInt32(Key_t key, UInt32 val)
{
    return propSetUInt32AtIndexRefresh(key, 0, val, utTrue);
}

/* set a 32-bit value for the specified key */
utBool propSetUInt32Refresh(Key_t key, UInt32 val, utBool refresh)
{
    return propSetUInt32AtIndexRefresh(key, 0, val, refresh);
}

/* add a 32-bit value to the specified key */
utBool propAddUInt32(Key_t key, UInt32 val)
{
    return propAddUInt32AtIndex(key, 0, &val);
}

// ----------------------------------------------------------------------------

/* get a boolean value for the specified key at the specified index */
utBool propGetBooleanAtIndex(Key_t key, int ndx, utBool dft)
{
    return propGetUInt32AtIndex(key, ndx, (UInt32)dft)? utTrue : utFalse;
}

/* set a boolean value for the specified key at the specified index */
utBool propSetBooleanAtIndex(Key_t key, int ndx, utBool val)
{
    return propSetUInt32AtIndexRefresh(key, ndx, (UInt32)val, utTrue);
}

/* get a boolean value for the specified key */
utBool propGetBoolean(Key_t key, utBool dft)
{
    return propGetBooleanAtIndex(key, 0, dft);
}

/* set a boolean value for the specified key */
utBool propSetBoolean(Key_t key, utBool val)
{
    return propSetBooleanAtIndex(key, 0, val);
}

// ----------------------------------------------------------------------------

/* get a double value for the KeyValue entry at the specified index */
static utBool _propGetDoubleValue(KeyValue_t *kv, int ndx, double *val)
{
    if (kv) {
        UInt32 uval;
        utBool ok = _propGetUInt32Value(kv, ndx, &uval);
        if (ok) {
            if (KVT_IS_SIGNED(kv->type)) {
                *val = UInt32_to_Double((Int32)uval, kv->type);
            } else {
                *val = UInt32_to_Double(uval, kv->type);
            }
            return utTrue;
        } else {
            return utFalse;
        }
    } else {
        return utFalse;
    }
}

/* set a double value for the KeyValue entry at the specified index */
static utBool _propSetDoubleValue(KeyValue_t *kv, int ndx, double *val)
{
    UInt32 uval = Double_to_UInt32(*val, kv->type);
    return _propSetUInt32Value(kv, ndx, &uval);
}

/* get a double value for the specified key at the specified index */
double propGetDoubleAtIndex(Key_t key, int ndx, double dft)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        return dft;
    } else {
        double valDbl = dft;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // no args
            if (kv->lenNdx < (ndx + 1)) {
                valDbl = dft;
            } else {
                double v;
                utBool ok = _propGetDoubleValue(kv, ndx, &v);
                valDbl = ok? v : dft;
            }
        } PROP_UNLOCK
        return valDbl;
    }
}

/* set a double value for the specified key at the specified index */
utBool propSetDoubleAtIndex(Key_t key, int ndx, double val)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (kv) {
        if (kv->maxNdx < (ndx + 1)) {
            return utFalse;
        } else {
            utBool ok = utFalse;
            PROP_LOCK {
                ok = _propSetDoubleValue(kv, ndx, &val);
                if (ok) { _propRefresh(PROP_REFRESH_SET, kv, (UInt8*)0, 0); }
            } PROP_UNLOCK
            return ok;
        }
    }
    return utFalse;
}

/* add a double value to the specified key at the specified index */
utBool propAddDoubleAtIndex(Key_t key, int ndx, double *val)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        return utFalse;
    } else {
        utBool ok = utFalse;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // no args
            if (kv->lenNdx < (ndx + 1)) {
                ok = utFalse;
            } else {
                double oldVal;
                utBool ok = _propGetDoubleValue(kv, ndx, &oldVal);
                if (ok) {
                    *val += oldVal;
                    ok = _propSetDoubleValue(kv, ndx, val);
                    if (ok) { _propRefresh(PROP_REFRESH_SET, kv, (UInt8*)0, 0); }
                } else {
                    ok = utFalse;
                }
            }
        } PROP_UNLOCK
        return ok;
    }
}

/* get a double value for the specified key */
double propGetDouble(Key_t key, double dft)
{
    return propGetDoubleAtIndex(key, 0, dft);
}

/* set a double value for the specified key */
utBool propSetDouble(Key_t key, double val)
{
    return propSetDoubleAtIndex(key, 0, val);
}

/* add a double value to the specified key */
utBool propAddDouble(Key_t key, double val)
{
    return propAddDoubleAtIndex(key, 0, &val);
}

// ----------------------------------------------------------------------------

/* get a null-terminated string value for the KeyValue entry */
static utBool _propGetStringValue(KeyValue_t *kv, const char *(*val))
{
    if (!kv || !val) {
        // no place to get value
        return utFalse;
    } else
    if (KVT_TYPE(kv->type) == KVT_STRING) {
        KeyData_t *keyDataBuf = _propGetData(kv);
      //UInt16     keyDataSiz = _propGetDataCapacity(kv);
        *val = (char*)(keyDataBuf->b); // assume already terminated
        return utTrue;
    } else {
        // unsupported type
        return utFalse;
    }
}

/* set a null-terminated string value for the KeyValue entry */
static utBool _propSetStringValue(KeyValue_t *kv, const char *(*val))
{
    if (!kv || !val) {
        // no specified value
        return utFalse;
    } else
    if (KVT_TYPE(kv->type) == KVT_STRING) {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        if (keyDataSiz > 0) {
            int len = strlen(*val);
            if (len > (keyDataSiz - 1)) { len = (keyDataSiz - 1); }
            if ((char*)keyDataBuf->b != *val) {
                strncpy((char*)keyDataBuf->b, *val, len);
            }
            keyDataBuf->b[len] = 0; // terminate
            kv->lenNdx = 1; // a single string is defined
            kv->dataSize = len; // not including terminating null
            kv->attr |= KVA_NONDEFAULT;
            kv->attr |= KVA_CHANGED; 
            return utTrue;
        } else {
            // insufficient size
            return utFalse;
        }
    } else {
        // unsupported type
        return utFalse;
    }
}

/* get a null-terminated string value for the specified key */
const char *propGetString(Key_t key, const char *dft)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        return dft;
    } else {
        const char *valChar = dft;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // no args
            if (kv->lenNdx <= 0) {
                valChar = dft;
            } else {
                const char *v;
                utBool ok = _propGetStringValue(kv, &v);
                valChar = ok? v : dft;
            }
        } PROP_UNLOCK
        return valChar;
    }
}

utBool propSetString(Key_t key, const char *val)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        return utFalse;
    } else {
        utBool ok = utFalse;
        PROP_LOCK {
            ok = _propSetStringValue(kv, &val);
            if (ok) { _propRefresh(PROP_REFRESH_SET, kv, (UInt8*)0, 0); }
        } PROP_UNLOCK
        return ok;
    }
}

// ----------------------------------------------------------------------------

/* get a null-terminated string value for the KeyValue entry */
static utBool _propGetBinaryValue(KeyValue_t *kv, const UInt8 *(*val), UInt16 *maxLen, UInt16 *dtaLen)
{
    if (!kv || !val || !maxLen || !dtaLen) {
        // no place to get value
        return utFalse;
    } else
    if (KVT_TYPE(kv->type) == KVT_BINARY) {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        *val    = keyDataBuf->b;
        *maxLen = keyDataSiz;
        *dtaLen = kv->dataSize;
        return utTrue;
    } else {
        // unsupported type
        return utFalse;
    }
}

/* set a null-terminated string value for the KeyValue entry */
static utBool _propSetBinaryValue(KeyValue_t *kv, const UInt8 *(*val), UInt16 dtaLen)
{
    if (!kv || !val) {
        // no specified value
        return utFalse;
    } else
    if (KVT_TYPE(kv->type) == KVT_BINARY) {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        if (keyDataSiz > 0) {
            int len = (dtaLen < keyDataSiz)? dtaLen : keyDataSiz;
            if (keyDataBuf->b != *val) {
                memcpy(keyDataBuf->b, *val, len);
            }
            kv->lenNdx = len;
            kv->dataSize = len;
            kv->attr |= KVA_NONDEFAULT;
            kv->attr |= KVA_CHANGED; 
            return utTrue;
        } else {
            // insufficient size
            return utFalse;
        }
    } else {
        // unsupported type
        return utFalse;
    }
}

/* get a binary value for the specified key */
const UInt8 *propGetBinary(Key_t key, const UInt8 *dft, UInt16 *maxLen, UInt16 *dtaLen)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        // maxLen. dtaLen left as-is
        return dft;
    } else {
        const UInt8 *valChar = dft;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // args?
            const UInt8 *v = (UInt8*)0;
            UInt16 max = 0, dta = 0;
            utBool ok = _propGetBinaryValue(kv, &v, &max, &dta);
            valChar = ok? v : dft;
            if (maxLen) { *maxLen = ok? max : 0; }
            if (dtaLen) { *dtaLen = ok? dta : 0; }
        } PROP_UNLOCK
        return valChar;
    }
}

/* set a binary value */
utBool propSetBinary(Key_t key, const UInt8 *val, UInt16 dtaLen)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        return utFalse;
    } else {
        utBool ok = utFalse;
        PROP_LOCK {
            ok = _propSetBinaryValue(kv, &val, dtaLen);
            if (ok) { _propRefresh(PROP_REFRESH_SET, kv, (UInt8*)0, 0); }
        } PROP_UNLOCK
        return ok;
    }
}

// ----------------------------------------------------------------------------

/* get a GPSOdometer_t value for the KeyValue entry */
static utBool _propGetGPSValue(KeyValue_t *kv, const GPSOdometer_t *(*gps))
{
    if (!kv || !gps) {
        // no place to get value
        return utFalse;
    } else
    if (KVT_TYPE(kv->type) == KVT_GPS) {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        if (keyDataSiz >= sizeof(GPSOdometer_t)) {
            *gps = &(keyDataBuf->gps);
            return utTrue;
        } else {
            // insufficient size
            return utFalse;
        }
    } else {
        // unsupported type
        return utFalse;
    }
}

/* set a GPSOdometer_t value for the KeyValue entry */
static utBool _propSetGPSValue(KeyValue_t *kv, const GPSOdometer_t *(*gps))
{
    if (!kv || !gps) { // 'gps' should not be null, however *gps may be
        // no specified value
        return utFalse;
    } else
    if (KVT_TYPE(kv->type) == KVT_GPS) {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        if (keyDataSiz >= sizeof(GPSOdometer_t)) {
            if (!*gps) {
                // GPS is null, clear KeyData_t GPS point
                memset(&(keyDataBuf->gps), 0, sizeof(GPSOdometer_t));
                gpsPointClear((GPSPoint_t*)&(keyDataBuf->gps));
            } else
            if (&(keyDataBuf->gps) != *gps) {
                // copy iff the pointers are not the same
                memcpy(&(keyDataBuf->gps), *gps, sizeof(GPSOdometer_t));
            }
            kv->lenNdx = 1; // a single GPS point is defined
            kv->dataSize = sizeof(GPSOdometer_t);
            kv->attr |= KVA_NONDEFAULT;
            kv->attr |= KVA_CHANGED; 
            return utTrue;
        } else {
            // insufficient size
            return utFalse;
        }
    } else {
        // unsupported type
        return utFalse;
    }
}

/* get a GPSOdometer_t value for the KeyValue entry */
const GPSOdometer_t *propGetGPS(Key_t key, const GPSOdometer_t *dft)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        return dft;
    } else {
        const GPSOdometer_t *valGPS = dft;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // no args
            if (kv->lenNdx <= 0) {
                valGPS = dft;
            } else {
                const GPSOdometer_t *gps;
                utBool ok = _propGetGPSValue(kv, &gps);
                valGPS = ok? gps : dft;
            }
        } PROP_UNLOCK
        return valGPS;
    }
}

/* set GPS property value */
utBool propSetGPS(Key_t key, const GPSOdometer_t *gps)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        // key not found
        return utFalse;
    } else {
        utBool ok = utFalse;
        PROP_LOCK {
            ok = _propSetGPSValue(kv, &gps); // gps may be null
            if (ok) { _propRefresh(PROP_REFRESH_SET, kv, (UInt8*)0, 0); }
        } PROP_UNLOCK
        return ok;
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// This section implements conversion of properties to/from binary values used
// for getting/setting properties

/* set the KeyValue entry value to the specified bytes */
// The type of KeyValue determines how the data will be interpreted
static PropertyError_t _propSetValue(KeyValue_t *kv, const UInt8 *data, int dataLen)
{
    if (!kv) {
        // invalid key
        return PROP_ERROR_INVALID_KEY;
    } else
    if (KVT_TYPE(kv->type) == KVT_COMMAND) {
        // should not be here (pre-validated)
        return PROP_ERROR_INVALID_KEY;
    } else
    if (dataLen <= 0) {
        kv->lenNdx = 0; // data is now empty
        kv->dataSize = 0;
        kv->attr |= KVA_NONDEFAULT;
        kv->attr |= KVA_CHANGED; 
        return PROP_ERROR_OK;
    } else {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        int type = KVT_TYPE(kv->type), maxBpe = 0, len = 0;
        switch (type) {
            case KVT_UINT8:
            case KVT_UINT16:
            case KVT_UINT24:
            case KVT_UINT32:
                maxBpe = KVT_UINT_SIZE(type); // bytes per type
                if ((dataLen >= (kv->maxNdx * maxBpe)) || ((dataLen % kv->maxNdx) == 0)) {
                    int n, bpe = dataLen / kv->maxNdx;
                    if (bpe > maxBpe) { bpe = maxBpe; } // max bytes per element
                    for (n = 0; n < kv->maxNdx; n++) {
                        UInt32 val = binDecodeInt32(&data[n * bpe], bpe, KVT_IS_SIGNED(kv->type));
                        keyDataBuf->i[n] = val;
                    }
                    kv->lenNdx = kv->maxNdx;
                    kv->dataSize = dataLen;
                    kv->attr |= KVA_NONDEFAULT;
                    kv->attr |= KVA_CHANGED; 
                    return PROP_ERROR(PROP_ERROR_OK, (kv->lenNdx * bpe));
                } else {
                    // invalid data length
                    return PROP_ERROR_INVALID_LENGTH;
                }
            case KVT_BINARY:
                kv->lenNdx = (dataLen < keyDataSiz)? dataLen : keyDataSiz;
                kv->dataSize = kv->lenNdx;
                logINFO(LOGSRC,"Binary length: %lu", (UInt32)kv->lenNdx);
                if ((void*)data != (void*)keyDataBuf) {
                    memset((void*)keyDataBuf, 0, keyDataSiz);
                    memcpy((void*)keyDataBuf, (void*)data, kv->lenNdx);
                }
                kv->attr |= KVA_NONDEFAULT;
                kv->attr |= KVA_CHANGED; 
                return PROP_ERROR(PROP_ERROR_OK, kv->lenNdx);
            case KVT_STRING: // _propSetValue
                // 'data' may, or may not, be terminated
                len = (dataLen < (keyDataSiz - 1))? dataLen : (keyDataSiz - 1);
                strncpy((char*)keyDataBuf->b, (char*)data, len); // strlen may be < len if 'data' was terminated
                keyDataBuf->b[len] = 0; // make sure it's terminated (however, data _may_ include terminator)
                kv->lenNdx = 1; // a string has been defined
                kv->dataSize = len; // not including terminating null
                kv->attr |= KVA_NONDEFAULT;
                kv->attr |= KVA_CHANGED; 
                return PROP_ERROR(PROP_ERROR_OK, strlen((char*)keyDataBuf->b));
            case KVT_GPS: // _propSetValue (binary format)
                len = _propDecodeGPS(&(keyDataBuf->gps), data, dataLen);
                if (len >= 0) {
                    kv->dataSize = len;
                    kv->lenNdx = 1; // GPS defined
                    kv->attr |= KVA_NONDEFAULT;
                    kv->attr |= KVA_CHANGED; 
                    return PROP_ERROR(PROP_ERROR_OK, len);
                } else {
                    // invalid length
                    return PROP_ERROR_INVALID_LENGTH;
                }
                break;
        }
    }
    // unsupported type
    return PROP_ERROR_INVALID_TYPE;
} // _propSetValue

/* set the key value to the specified bytes (from the server) */
// This function obeys the 'KVA_IS_READONLY' flag.
PropertyError_t propSetValueCmd(int protoNdx, Key_t key, const UInt8 *data, int dataLen)
{
    KeyValue_t *kv = (KeyValue_t*)0;
#if defined(SECONDARY_SERIAL_TRANSPORT)
    if ((key == PROP_STATE_DEVICE_ID) && (protoNdx == 1)) {
        kv = propGetKeyValueEntry(PROP_STATE_DEVICE_BT);
    } else {
        kv = propGetKeyValueEntry(key);
    }
#else
    kv = propGetKeyValueEntry(key);
#endif
    if (kv) {
        if (KVA_IS_READONLY(kv->attr)) {
            // cannot set a read-only property
            return PROP_ERROR_READ_ONLY;
        } else
        if (KVT_TYPE(kv->type) == KVT_COMMAND) {
            KeyData_t *keyDataBuf = _propGetData(kv);
            UInt16     keyDataSiz = _propGetDataCapacity(kv);
            if (keyDataBuf->cmd && (keyDataSiz >= sizeof(keyDataBuf->cmd))) {
                CommandError_t err;
                PROP_LOCK {
                    err = (*keyDataBuf->cmd)(protoNdx, key, data, dataLen);
                } PROP_UNLOCK
                if (err == COMMAND_OK) {
                    return PROP_ERROR_OK;
                } else
                if (err == COMMAND_OK_ACK) {
                    return PROP_ERROR(PROP_ERROR_COMMAND_ERROR,err);
                } else {
                    return PROP_ERROR(PROP_ERROR_COMMAND_ERROR,err);
                }
            } else {
                // the command has not been initialized (internal error)
                logERROR(LOGSRC,"Command not initialized: 0x%04X", (int)key);
                return PROP_ERROR_COMMAND_INVALID;
            }
        } else {
            PropertyError_t err;
            PROP_LOCK {
                err = _propSetValue(kv, data, dataLen);
                if (PROP_ERROR_OK_LENGTH(err) >= 0) {
                    _propRefresh(PROP_REFRESH_SET, kv, (UInt8*)0, 0); 
                }
            } PROP_UNLOCK
            return err; // length, or <0 if error
        }
    } else {
        return PROP_ERROR_INVALID_KEY;
    }
}

// ----------------------------------------------------------------------------

/* get the byte array data for the KeyValue entry */
static PropertyError_t _propGetValue(KeyValue_t *kv, FmtBuffer_t *bf)
{
    if (KVA_IS_WRITEONLY(kv->attr)) {
        return PROP_ERROR_WRITE_ONLY;
    } else
    if (KVT_TYPE(kv->type) == KVT_COMMAND) {
        // should not be here (pre-validated)
        return PROP_ERROR_WRITE_ONLY;
    } else
    if (!BUFFER_DATA(bf) || (BUFFER_DATA_SIZE(bf) <= 0)) {
        // no place to put the data
        return PROP_ERROR_OK; // return ok anyway
    } else {
        KeyData_t *keyDataBuf = _propGetData(kv);
        UInt16     keyDataSiz = _propGetDataCapacity(kv);
        int type = KVT_TYPE(kv->type), maxBpe = 0;
        int len = 0;
        if (bf->fmt && (bf->fmtLen > 0)) { *bf->fmt = 0; /* init format */ }
        switch (type) {
            case KVT_UINT8:
            case KVT_UINT16:
            case KVT_UINT24:
            case KVT_UINT32:
                maxBpe = KVT_UINT_SIZE(type); // bytes per type
                if ((BUFFER_DATA_SIZE(bf) >= (kv->maxNdx * maxBpe)) || ((BUFFER_DATA_SIZE(bf) % kv->maxNdx) == 0)) {
                    utBool isSigned = KVT_IS_SIGNED(kv->type);
                    char fmtCh = isSigned? 'i' : (KVT_IS_HEX(kv->type)? 'x' : 'u');
                    int n, bpe = BUFFER_DATA_SIZE(bf) / kv->maxNdx;
                    if (bpe > maxBpe) { bpe = maxBpe; } // max bytes per element
                    for (n = 0; n < kv->maxNdx; n++) {
                        binEncodeInt32(BUFFER_DATA(bf), bpe, keyDataBuf->i[n], isSigned);
                        binAppendFmtField(bf, bpe, fmtCh);
                        binAdvanceFmtBuffer(bf, bpe);
                    }
                    return BUFFER_DATA_LENGTH(bf);
                } else {
                    // invalid data length (internal error)
                    return PROP_ERROR_INVALID_LENGTH;
                }
            case KVT_BINARY:
                len = (BUFFER_DATA_SIZE(bf) < kv->lenNdx)? BUFFER_DATA_SIZE(bf) : kv->lenNdx;
                memcpy(BUFFER_DATA(bf), keyDataBuf->b, len);
                binAppendFmtField(bf, len, 'b');
                binAdvanceFmtBuffer(bf, len);
                return PROP_ERROR(PROP_ERROR_OK, len);
            case KVT_STRING: // _propGetValue
                len = strLength((char*)keyDataBuf->b, keyDataSiz);
                if (len > BUFFER_DATA_SIZE(bf)) { len = BUFFER_DATA_SIZE(bf); }
                strncpy((char*)BUFFER_DATA(bf), (char*)keyDataBuf->b, len);
                if (len < BUFFER_DATA_SIZE(bf)) { BUFFER_DATA(bf)[len++] = 0; } // terminate if we have room
                binAppendFmtField(bf, len, 's');
                binAdvanceFmtBuffer(bf, len);
                return PROP_ERROR(PROP_ERROR_OK, len); // length of string sans terminator
            case KVT_GPS: // _propGetValue (binary format)
                len = _propEncodeGPS(&(keyDataBuf->gps), bf);
                return PROP_ERROR(PROP_ERROR_OK, len);
        }
    }
    // unsupported type (internal error)
    return PROP_ERROR_INVALID_TYPE;
} // _propGetValue

/* get the specified property value as a byte array */
// returns the number of bytes placed into the buffer
// or -1, if there was an error
PropertyError_t propGetValue(Key_t key, UInt8 *data, int dataLen)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (!kv) {
        // invalid key
        return PROP_ERROR_INVALID_KEY;
    } else
    if (KVA_IS_WRITEONLY(kv->attr)) {
        // cannot read a write-only property
        return PROP_ERROR_WRITE_ONLY;
    } else
    if (KVT_TYPE(kv->type) == KVT_COMMAND) {
        // cannot read from a command
        // should not be here (all commands should be write-only)
        return PROP_ERROR_WRITE_ONLY;
    } else {
        PropertyError_t err;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // args?
            FmtBuffer_t bb, *bf = binFmtBuffer(&bb, data, dataLen, 0, 0);
            err = _propGetValue(kv, bf);
        } PROP_UNLOCK
        return err;
    }
}

/* create a packet containing the specified property value (for sending to the server) */
PropertyError_t propGetPropertyPacket(int protoNdx, Packet_t *pkt, Key_t key, UInt8 *args, int argLen)
{
    KeyValue_t *kv = (KeyValue_t*)0;
#if defined(SECONDARY_SERIAL_TRANSPORT)
    if ((key == PROP_STATE_DEVICE_ID) && (protoNdx == 1)) {
        kv = propGetKeyValueEntry(PROP_STATE_DEVICE_BT);
    } else {
        kv = propGetKeyValueEntry(key);
    }
#else
    kv = propGetKeyValueEntry(key);
#endif
    if (!kv) {
        // invalid key
        return PROP_ERROR_INVALID_KEY;
    } else
    if (KVA_IS_WRITEONLY(kv->attr)) {
        // cannot read a write-only property
        return PROP_ERROR_WRITE_ONLY;
    } else
    if (KVT_TYPE(kv->type) == KVT_COMMAND) {
        // cannot read from a command
        // should not be here (all commands should be write-only)
        return PROP_ERROR_WRITE_ONLY;
    } else {
        PropertyError_t err;
        PROP_LOCK {
            _propRefresh(PROP_REFRESH_GET, kv, args, argLen); // no args
            pktInit(pkt, PKT_CLIENT_PROPERTY_VALUE, (char*)0); // payload filled-in below
            FmtBuffer_t bb, *bf = pktFmtBuffer(&bb, pkt);
            binFmtPrintf(bf, "%2x", (UInt32)key); // property key
            err = _propGetValue(kv, bf); // property value
            pkt->dataLen = (UInt8)BUFFER_DATA_LENGTH(bf); // remember to save buffer length
        } PROP_UNLOCK
        return err;
    }
}
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// This section implements conversion of properties to/from ASCIIZ strings.

/* start-up initialization for individual KeyValue entry */
static void _propInitKeyValueFromString(KeyValue_t *kv, const char *s, utBool internal)
{
    
    /* reset attributes/length */
    kv->lenNdx = 0; // reset actual length
    kv->dataSize = 0;
    if (internal) {
        kv->attr &= ~KVA_NONDEFAULT; // set to 'default' (clear non-default flag)
    }
    
    /* get key data buffer */
    KeyData_t *keyDataBuf = _propGetData(kv);
    UInt16     keyDataSiz = _propGetDataCapacity(kv);
    memset(keyDataBuf, 0, keyDataSiz); // clear data buffer
    
    /* scan through types */
    if (KVT_TYPE(kv->type) == KVT_COMMAND) {
        if (internal) {
            // reset/clear command type (value of string is ignored)
            keyDataBuf->cmd = 0;
        }
        return /*PROP_ERROR_OK*/;
    }
    
    /* parse for specific type */
    int len = 0;
    if (!s) { s = ""; } // <-- 's' must not be null
    switch (KVT_TYPE(kv->type)) {
        case KVT_UINT8:
        case KVT_UINT16:
        case KVT_UINT24:
        case KVT_UINT32:
            while ((kv->lenNdx < kv->maxNdx) && (((kv->lenNdx + 1) * sizeof(UInt32)) <= keyDataSiz)) {
                UInt32 val = 0L;
                if (KVT_DEC(kv->type) > 0) {
                    double dval = strParseDouble(s, 0.0);
                    val = Double_to_UInt32(dval, kv->type);
                } else
                if (strStartsWithIgnoreCase(s,"0x")) {
                    val = strParseHex32(s, 0x0000L);
                } else {
                    val = strParseUInt32(s, 0L);
                }
                keyDataBuf->i[kv->lenNdx] = val;
                kv->lenNdx++;
                kv->dataSize = &keyDataBuf->i[kv->lenNdx] - &keyDataBuf->i[0];
                while (*s && (*s != ',')) { s++; }
                if (*s != ',') { break; }
                *s++; // skip ','
            }
            break;
        case KVT_BINARY:
            len = (kv->maxNdx < keyDataSiz)? kv->maxNdx : keyDataSiz;
            kv->lenNdx = strParseHex(s, -1, keyDataBuf->b, len); // size of binary data
            kv->dataSize = kv->lenNdx;
            break;
        case KVT_STRING: // _propInitKeyValueFromString
            // 's' will be terminated
            len = strlen(s);
            if (len > (keyDataSiz - 1)) { len = (keyDataSiz - 1); }
            strncpy((char*)keyDataBuf->b, s, len);
            keyDataBuf->b[len] = 0; // terminate
            kv->lenNdx = 1; // only one string
            kv->dataSize = len;
            break;
        case KVT_GPS: // _propInitKeyValueFromString (initializes with a string)
            // GPSOdometer_t only! Expected to be in the format "<time>,<latitude>,<longitude>,<meters>"
            gpsOdomParseString(&(keyDataBuf->gps), s);
            kv->lenNdx = 1; // only 1 GPS point
            kv->dataSize = sizeof(GPSOdometer_t);
            break;
    }
    
    return /*PROP_ERROR_OK*/;
}

/* start-up initialization for all KeyValue entries */
// This function can be called at any time to reset the properties to their start-up 
// defaults by calling this function with a 'true' argument.  Calling this function
// with a 'false' argument will cause the properties to be initialized only if they
// have not yet been initialized.
static utBool _propsDidInit = utFalse;
void propInitialize(utBool forceReset)
{
    if (!_propsDidInit || forceReset) {
        logDEBUG(LOGSRC,"Property table size: %d entries, %lu bytes", PROP_COUNT, (UInt32)sizeof(properties));
        utBool allInSequence = utTrue;
        Key_t lastKey = 0x0000;
        int i;
        PROP_LOCK {
            for (i = 0; i < PROP_COUNT; i++) {
                KeyValue_t *kv = &properties[i];
                if (kv->key < lastKey) {
                    logWARNING(LOGSRC,"Property key out of sequence: 0x%04X %s", kv->key, kv->name);
                    allInSequence = utFalse;
                }
                lastKey = kv->key;
                _propInitKeyValueFromString(kv, kv->dftInit, utTrue); // initialize
            }
        } PROP_UNLOCK
        _propsDidInit = utTrue;
        binarySearchOK = allInSequence;
    }
}

/* initialize KeyValue from string (local initialization) */
utBool propInitFromString(Key_t key, const char *s)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (kv) {
        PROP_LOCK {
            _propInitKeyValueFromString(kv, s, utTrue); // initialize
            // [DO NOT CALL "_propRefresh(PROP_REFRESH_SET...)" ]
        } PROP_UNLOCK
        return utTrue;
    }
    return utFalse;
}

// ----------------------------------------------------------------------------

/* convert property value to string */
static const char *_propToString(KeyValue_t *kv, char *buf, int bufLen)
{
    char *s = buf;
    int slen = bufLen;

    /* single pass loop */
    for (;;) {
        
        /* refresh */
        _propRefresh(PROP_REFRESH_GET, kv, (UInt8*)0, 0); // no args

        /* create "value" string */
        if (kv->lenNdx > 0) {
            UInt8 tmp[300];
            FmtBuffer_t bb, *bf = binFmtBuffer(&bb, tmp, sizeof(tmp), 0, 0); // MUST be FmtBuffer_t !!
            //KeyData_t *keyDataBuf = _propGetData(kv);
            //UInt16     keyDataSiz = _propGetDataCapacity(kv);
            int n;
            PropertyError_t err;
            switch (KVT_TYPE(kv->type)) {
                case KVT_UINT8:
                case KVT_UINT16:
                case KVT_UINT24:
                case KVT_UINT32:
                    for (n = 0; n < kv->lenNdx; n++) {
                        if (12 > slen) {
                            // overflow
                            return (char*)0;
                        }
                        if (n > 0) {
                            *s++ = ',';
                            slen--;
                        }
                        if (KVT_DEC(kv->type) > 0) {
                            double x = 0.0;
                            _propGetDoubleValue(kv, n, &x);
                            sprintf(s, "%.*lf", KVT_DEC(kv->type), x);
                        } else {
                            UInt32 x = 0L;
                            _propGetUInt32Value(kv, n, &x);
                            if (KVT_IS_HEX(kv->type)) {
                                int bpe = KVT_UINT_SIZE(kv->type);
                                sprintf(s, "0x%0*lX", bpe, x);
                            } else {
                                sprintf(s, "%lu", x);
                            }
                        }
                        int sn = strlen(s);
                        s += sn;
                        slen -= sn;
                    }
                    break;
                case KVT_BINARY:
                    if ((kv->lenNdx * 2) + 3 > bufLen) {
                        // overflow
                        return (char*)0;
                    }
                    err = _propGetValue(kv, bf);
                    sprintf(s, "0x"); s += strlen(s);
                    strEncodeHex(s, -1, BUFFER_PTR(bf), BUFFER_DATA_LENGTH(bf)); s += strlen(s);
                    break;
                case KVT_STRING: // _propToString
                    if ((int)(strlen((char*)kv->data.b) + 1) > bufLen) {
                        // overflow
                        return (char*)0;
                    }
                    err = _propGetValue(kv, bf);
                    sprintf(s, "%.*s", (UInt16)BUFFER_DATA_LENGTH(bf), BUFFER_PTR(bf)); s += strlen(s);
                    break;
                case KVT_GPS: // _propToString
                    // Outputs a string with the format "<fixtime>,<latitude>,<longitude>"
                    if (kv->lenNdx + 10 > bufLen) {
                        // overflow
                        return (char*)0;
                    }
                    err = _propGetValue(kv, bf); // <-- NOTE: Returns encoded GPS data!!!
                    if (PROP_ERROR_OK_LENGTH(err) > 0) { // print iff there is gps data defined
                        // a little redundant (first encode, just to decode)
                        GPSOdometer_t gps;
                        memset(&gps, 0, sizeof(GPSOdometer_t));
                        gpsPointClear(&(gps.point));
                        _propDecodeGPS(&gps, BUFFER_PTR(bf), BUFFER_DATA_LENGTH(bf));
                        gpsOdomToString(&gps, s, (bufLen - (s - buf))); s += strlen(s);
                    }
                    break;
                default:
                    break;
            }
        }
        
        break;
    }
    
    return buf;
}

/* print KeyValue to string (local) */
utBool propPrintToString(Key_t key, char *buf, int bufLen)
{
    KeyValue_t *kv = propGetKeyValueEntry(key);
    if (kv) {
        PROP_LOCK {
            _propToString(kv, buf, bufLen);
        } PROP_UNLOCK
        return utTrue;
    }
    return utFalse;
}

// ----------------------------------------------------------------------------

/* print property values */
utBool _propSaveProperties(FILE *file, utBool saveKeyName, utBool all)
{
    char buf[300];
    int bufLen = sizeof(buf);

    /* iterate through properties */
    int i, len = 0;
    for (i = 0; i < PROP_COUNT; i++) {
        KeyValue_t *kv = &properties[i];
        char *s = buf;
        
        /* non-default only? */
        if (!all) {
            if (!KVA_IS_SAVE(kv->attr)) {
                // property is not to be saved
                kv->attr &= ~KVA_CHANGED; // make sure 'changed' flag is cleared
                continue;
            } else
            if (!KVA_IS_NONDEFAULT(kv->attr)) {
                // property is already the default value, no ned to save
                kv->attr &= ~KVA_CHANGED; // make sure 'changed' flag is cleared
                continue;
            }
        }
        
        /* "<Key>=" */
        if (saveKeyName && kv->name && *kv->name) {
            sprintf(s, "%s=", kv->name);
        } else {
            sprintf(s, "0x%04X=", kv->key);
        }
        s += strlen(s);
         
        /* "<Value>" */
        _propToString(kv, s, bufLen - (s - buf));
        s += strlen(s);
        
        /* EOL */
        strcpy(s, "\n");
        s += strlen(s);
        
        /* write to file */
        len = ioWriteStream(file, buf, (s - buf));
        if (len < 0) {
            // logERROR(LOGSRC,"Error saving properties");
            break; // write error
        }
        
        /* clear changes */
        kv->attr &= ~KVA_CHANGED; // clear changed flag
        
    }

    /* return success state */
    return (len < 0)? utFalse : utTrue;
    
}

/* print property values */
utBool propPrintProperties(FILE *file, utBool all)
{
    utBool ok;
    PROP_LOCK {
        ok = _propSaveProperties(file, utTrue, all);
    } PROP_UNLOCK
    return ok;
}

/* return true if properties have changed since last saved */
utBool propHasChanged()
{
    int i;
    for (i = 0; i < PROP_COUNT; i++) {
        KeyValue_t *kv = &properties[i];
        if (KVA_IS_SAVE(kv->attr) && KVA_IS_CHANGED(kv->attr)) {
            // property is to be saved, and it has changed since last save
            return utTrue;
        }
    }
    return utFalse;
}

/* clear all property changed flags */
void propClearChanged()
{
    int i;
    for (i = 0; i < PROP_COUNT; i++) {
        KeyValue_t *kv = &properties[i];
        kv->attr &= ~KVA_CHANGED;
    }
}

/* save properties */
utBool propSaveProperties(const char *propFile, utBool all)
{

    /* open file */
    FILE *file;
    if (!propFile || !*propFile || strEqualsIgnoreCase(propFile,"stdout")) {
        file = stdout;
    } else {
        file = ioOpenStream(propFile, IO_OPEN_WRITE);
        if (!file) {
            // error openning
            return utFalse;
        }
    }
    
    /* save to file stream */
    utBool rtn = utFalse;
    PROP_LOCK {
        rtn = _propSaveProperties(file, PROP_SAVE_KEY_NAME, all);
    } PROP_UNLOCK

    /* close file */
    if ((file != stdout) || (file != stderr)) {
        ioCloseStream(file);
    }

    return rtn;
    
}

// ----------------------------------------------------------------------------

/* load property values */
// eg:
//  0xFXXX=<value>
// Notes:
//  - Blank lines and lines beginning with '#' are ignored.
//  - Last line MUST be terminated with a newline, otherwise it will be ignored
//  - DOES NOT CALL "_propRefresh(PROP_REFRESH_SET...)" 
utBool propLoadProperties(const char *propFile, utBool showProps)
{
    char buf[300];
    //int bufLen = sizeof(buf);
    
    /* invalid/unspecified file? */
    if (!propFile || !*propFile) {
        logDEBUG(LOGSRC,"Property file not specified");
        return utFalse;
    }
    
    /* open file */
    FILE *file = ioOpenStream(propFile, IO_OPEN_READ);
    if (!file) {
        // error openning (file may not exist)
        logDEBUG(LOGSRC,"Unable to open property file: %s", propFile);
        return utFalse;
    }
    
    /* read */
    char *b = buf;
    for (;;) {
        long len = ioReadStream(file, b, 1);
        if (len <= 0) {
            break; // error/eof
        } else
        if (*b == '\n') {
            *b = 0;
            char *k = buf;
            while (*k && isspace(*k)) { k++; }
            if (!*k || (*k == '#')) {
                // blank-lines and comment-lines ignored
            } else {
                utBool ok = utFalse;
                char *v = k;
                while (*v && (*v != '=')) { v++; }
                if (*v == '=') {
                    *v++ = 0; // terminate key
                    while (*v && isspace(*v)) { v++; } // skip prefixing spaces before value
                    KeyValue_t *kv = strStartsWithIgnoreCase(k,"0x")?
                        propGetKeyValueEntry((Key_t)strParseHex32(k, 0xFFFFFFFFL)) :
                        propGetKeyValueEntryByName(k);
                    if (kv) {
                        PROP_LOCK {
                            _propInitKeyValueFromString(kv, v, utTrue); // initialize
                            kv->attr |= KVA_NONDEFAULT; // force non-default
                            kv->attr &= ~KVA_CHANGED;   // clear changed flag
                        } PROP_UNLOCK
                        if (showProps) {
                            // even under 'debug' mode, we may not want to display loaded properties
                            logDEBUG(LOGSRC,"Loaded %s=%s", k, v);
                        }
                        ok = utTrue;
                    }
                }
                if (!ok) {
                    logWARNING(LOGSRC,"Unknown key/value ignored: %s", k);
                }
            }
            b = buf;
        } else
        if (*b != '\r') {
            b++;
        }
    }
    
    /* close file */
    ioCloseStream(file);

    return utTrue;
    
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

/* return account-id */
const char *propGetAccountID()
{
    return propGetString(PROP_STATE_ACCOUNT_ID,"");
}

/* return device-id */
const char *propGetDeviceID(int protoNdx)
{
#if defined(SECONDARY_SERIAL_TRANSPORT)
    if (protoNdx == 1) {
        return propGetString(PROP_STATE_DEVICE_BT,"");
    }
#endif
    return propGetString(PROP_STATE_DEVICE_ID,"");
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

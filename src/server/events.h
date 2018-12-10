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

#ifndef _EVENTS_H
#define _EVENTS_H
#ifdef __cplusplus
extern "C" {
#endif

#include "tools/stdtypes.h"
#include "tools/gpstools.h"

#include "server/defaults.h"
#include "server/packet.h"

// ----------------------------------------------------------------------------

#define MAX_ID_SIZE             20

// ----------------------------------------------------------------------------

#define EVENT_INCL_STRING
#define EVENT_INCL_BINARY
#define EVENT_INCL_GPS_STATS
#define EVENT_INCL_TEMPERATURE
#define EVENT_INCL_ENTITY

#ifdef NOT_DEF
// These have not yet been tested in this module
#define EVENT_INCL_DIGITAL_INPUT
#define EVENT_INCL_ANALOG_INPUT
#define EVENT_INCL_OBC
#endif

// ----------------------------------------------------------------------------

#define HI_RES                  utTrue
#define LO_RES                  utFalse

enum EventFieldType_enum {
    
 // Most common fields                  // Low                          High
    FIELD_STATUS_CODE           = 0x01, // %2u
    FIELD_TIMESTAMP             = 0x02, // %4u
    FIELD_INDEX                 = 0x03, // %4u 0 to 4294967295
    
 // Sequence number field               // Low                          High
    FIELD_SEQUENCE              = 0x04, // %1u 0 to 255

 // GPS fields                          // Low                          High
    FIELD_GPS_POINT             = 0x06, // %6g                          %8g
    FIELD_GPS_AGE               = 0x07, // %2u 0 to 65535 sec
    FIELD_SPEED                 = 0x08, // %1u 0 to 255 kph             %2u 0.0 to 655.3 kph
    FIELD_HEADING               = 0x09, // %1u 1.412 deg un.            %2u 0.00 to 360.00 deg
    FIELD_ALTITUDE              = 0x0A, // %2i -32767 to +32767 m       %3i -838860.7 to +838860.7 m
    FIELD_DISTANCE              = 0x0B, // %3u 0 to 16777216 km         %3u 0.0 to 1677721.6 km
    FIELD_ODOMETER              = 0x0C, // %3u 0 to 16777216 km         %4u 0.0 to 429496729.5 km

 // Misc fields                         // Low                          High
    FIELD_GEOFENCE_ID           = 0x0E, // %4u 0x00000000 to 0xFFFFFFFF
    FIELD_TOP_SPEED             = 0x0F, // %1u 0 to 255 kph             %2u 0.0 to 655.3 kph

 // String/ID field                     // Low                          High
    FIELD_STRING                = 0x11, // %*s may contain only 'A'..'Z', 'a'..'z, '0'..'9', '-', '.'
    FIELD_STRING_PAD            = 0x12, // %*s may contain only 'A'..'Z', 'a'..'z, '0'..'9', '-', '.'

 // Entity String field                 // Low                          High
    FIELD_ENTITY                = 0x15, // %*s may contain only 'A'..'Z', 'a'..'z, '0'..'9', '-', '.'
    FIELD_ENTITY_PAD            = 0x16, // %*s may contain only 'A'..'Z', 'a'..'z, '0'..'9', '-', '.'

 // Generic binary field                // Low                          High
    FIELD_BINARY                = 0x1A, // %*b  

 // Digital I/O fields                  // Low                          High
    FIELD_INPUT_ID              = 0x21, // %4u 0x00000000 to 0xFFFFFFFF
    FIELD_INPUT_STATE           = 0x22, // %4u 0x00000000 to 0xFFFFFFFF
    FIELD_OUTPUT_ID             = 0x24, // %4u 0x00000000 to 0xFFFFFFFF
    FIELD_OUTPUT_STATE          = 0x25, // %4u 0x00000000 to 0xFFFFFFFF
    FIELD_ELAPSED_TIME          = 0x27, // %3u 0 to 16777216 sec
    FIELD_COUNTER               = 0x28, // %4u 0 to 4294967295

 // Analog I/O fields                   // Low                          High
    FIELD_SENSOR32_LOW          = 0x31, // %4u 0x00000000 to 0xFFFFFFFF
    FIELD_SENSOR32_HIGH         = 0x32, // %4u 0x00000000 to 0xFFFFFFFF
    FIELD_SENSOR32_AVER         = 0x33, // %4u 0x00000000 to 0xFFFFFFFF

 // Temperature fields                  // Low                          High
    FIELD_TEMP_LOW              = 0x3A, // %1i -126 to +126 C           %2i -3276.6 to +3276.6 C
    FIELD_TEMP_HIGH             = 0x3B, // %1i -126 to +126 C           %2i -3276.6 to +3276.6 C
    FIELD_TEMP_AVER             = 0x3C, // %1i -126 to +126 C           %2i -3276.6 to +3276.6 C

 // GPS quality fields                  // Low                          High
    FIELD_GPS_DGPS_UPDATE       = 0x41, // %2u 0 to 65535 sec
    FIELD_GPS_HORZ_ACCURACY     = 0x42, // %1u 0 to 255 m               %2u 0.0 to 6553.5 m
    FIELD_GPS_VERT_ACCURACY     = 0x43, // %1u 0 to 255 m               %2u 0.0 to 6553.5 m
    FIELD_GPS_SATELLITES        = 0x44, // %1u 0 to 12
    FIELD_GPS_MAG_VARIATION     = 0x45, // %2i -180.00 to 180.00 deg
    FIELD_GPS_QUALITY           = 0x46, // %1u (0=None, 1=GPS, 2=DGPS, ...)
    FIELD_GPS_TYPE              = 0x47, // %1u (1=None, 2=2D, 3=3D, ...)
    FIELD_GPS_GEOID_HEIGHT      = 0x48, // %1i -128 to +127 m           %2i -3276.7 to +3276.7 m
    FIELD_GPS_PDOP              = 0x49, // %1u 0.0 to 25.5              %2u 0.0 to 99.9
    FIELD_GPS_HDOP              = 0x4A, // %1u 0.0 to 25.5              %2u 0.0 to 99.9
    FIELD_GPS_VDOP              = 0x4B, // %1u 0.0 to 25.5              %2u 0.0 to 99.9

 // OBC/J1708 fields
#ifdef EVENT_INCL_OBC
    FIELD_OBC_VALUE             = 0x50, // %*b (at least 4 bytes, includes mid/pid)
    FIELD_OBC_GENERIC           = 0x51, // %4u
    FIELD_OBC_J1708_FAULT       = 0x52, // %4u
    FIELD_OBC_DISTANCE          = 0x54, // %3u 0 to 16777216 km         %4u 0.0 to 429496729.5 km
    FIELD_OBC_ENGINE_HOURS      = 0x57, // %3u 0 to 1677721.6 hours
    FIELD_OBC_ENGINE_RPM        = 0x58, // %2u 0 to 65535 rpm
    FIELD_OBC_COOLANT_TEMP      = 0x59, // %1i -126 to 126 C            %2i -3276.7 to +3276.7 C
    FIELD_OBC_COOLANT_LEVEL     = 0x5A, // %1u 0% to 100% percent       %2u 0.0% to 100.0% percent
    FIELD_OBC_OIL_LEVEL         = 0x5B, // %1u 0% to 100% percent       %2u 0.0% to 100.0% percent
    FIELD_OBC_OIL_PRESSURE      = 0x5C, // %1u 0 to 255 kPa             %2u 0.0 to 6553.5 kPa
    FIELD_OBC_FUEL_LEVEL        = 0x5D, // %1u 0% to 100% percent       %2u 0.0% to 100.0% percent
    FIELD_OBC_FUEL_ECONOMY      = 0x5E, // %1u 0 to 255 kpl             %2u 0.0 to 6553.5 kpl
    FIELD_OBC_FUEL_USED         = 0x5F, // %3u 0 to 16777216 liters     %4u 0.0 to 429496729.5 liters
#endif

};
typedef enum EventFieldType_enum EventFieldType_t;

// ----------------------------------------------------------------------------

/* field definition */
typedef struct {
    EventFieldType_t    type;       // field type 
    utBool              hiRes;      // field flags (eg. HI_RES, LO_RES)
    UInt8               index;      // type index
    UInt8               length;     // field byte size
} FieldDef_t;

#define EVENT_FIELD(T,H,I,L)    { (T), (H), (UInt8)(I), (UInt8)(L) }

// Mask Layout:
//   23:1  HiRes    (1 bit)     0x800000
//   16:7  Type     (0..63)     0x7F0000
//    8:8  Index    (0..15)     0x00FF00
//    0:8  Length   (0..15)     0x0000FF
#define _FLDMSK_REZ_24          0x01    // 1 bits
#define _FLDMSK_TYP_24          0x7F    // 7 bits
#define _FLDMSK_NDX_24          0xFF    // 8 bits
#define _FLDMSK_LEN_24          0xFF    // 8 bits
#define _FLD_DEF24(T,F,I,L)     (((UInt32)((F)&_FLDMSK_REZ_24)<<23)|((UInt32)((T)&_FLDMSK_TYP_24)<<16)|((UInt32)((I)&_FLDMSK_NDX_24)<<8)|(UInt32)((L)&_FLDMSK_LEN_24))
#define FIELD_DEF24(F)          _FLD_DEF24((F)->type,(F)->hiRes,(F)->index,(F)->length)

/* custom packet definition */
typedef struct {
    ClientPacketType_t  hdrType;
    UInt16              fldLen;
    FieldDef_t          *fld;
} CustomDef_t;

// ----------------------------------------------------------------------------
// MID : Message ID
//          128 = Engine
//          130 = Transmission
//          136 = Brakes (ABS)
// PID: Parameter ID

/* OBC/J1708 value */
#ifdef EVENT_INCL_OBC
typedef struct {
    UInt16          mid;
    UInt16          pid;
    UInt8           dataLen;
    UInt8           data[21];
} EvOBCValue_t;
#endif

/* Event structure */
typedef struct {
        
    UInt16          statusCode;
    UInt32          timestamp[1];
    UInt32          index;

    GPSPoint_t      gpsPoint[1];
    UInt32          gpsAge;
    double          speedKPH;
    double          heading;
    double          altitude;
    double          distanceKM;
    double          odometerKM;

    UInt32          sequence;
    UInt32          seqLen;

    UInt32          geofenceID[2];
    double          topSpeedKPH;

    char            entity[2][MAX_ID_SIZE + 1];

    char            string[2][MAX_ID_SIZE + 1];

    UInt8           *binary;
    UInt8           binaryLen;

    UInt32          inputID;
    UInt32          inputState;
    UInt32          outputID;
    UInt32          outputState;
    UInt32          elapsedTimeSec[8];
    UInt32          counter[1];         // [8];

    UInt32          supplyVoltageMV;
    UInt32          sensor32LO[1];      // [8];
    UInt32          sensor32HI[1];      // [8];
    UInt32          sensor32AV[1];      // [8];

    double          tempLO[4];          // [8];
    double          tempHI[4];          // [8];
    double          tempAV[4];          // [8];

    UInt32          gpsDgpsUpdate;
    double          gpsHorzAccuracy;
    double          gpsVertAccuracy;
    UInt32          gpsSatellites;
    double          gpsMagVariation;
    UInt32          gpsQuality;         // 1=GPS, 2=DGPS, 3=PPS
    UInt32          gps2D3D;            // 2=2D, 3=3D
    double          gpsGeoidHeight;
    double          gpsPDOP;
    double          gpsHDOP;
    double          gpsVDOP;

#ifdef EVENT_INCL_OBC
    EvOBCValue_t    obcValue[2];
    UInt32          obcGeneric[2];
    UInt32          obcJ1708Fault[2];
    double          obcDistanceKM;
    double          obcEngineHours;
    UInt32          obcEngineRPM;
    double          obcCoolantTemp;
    double          obcCoolantLevel;
    double          obcOilLevel;
    double          obcOilPressure;
    double          obcFuelLevel;
    double          obcFuelEconomy;
    double          obcAvgFuelEcon;
    double          obcFuelUsed;
#endif

} Event_t;

// ----------------------------------------------------------------------------

utBool evAddCustomDefinition(CustomDef_t *cd);

Event_t *evParseEventPacket(Packet_t *pkt, Event_t *er);

// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif

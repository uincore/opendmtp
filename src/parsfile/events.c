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
//  GPS/Data event manager. 
//  Generates and manages events.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2007/01/28  Martin D. Flynn
//     -Changed to support elapsed time resolution of 1 second.
//  2007/02/11  Martin D. Flynn
//     -Added FIELD_OBC_COOLANT_LEVEL, FIELD_OBC_OIL_PRESSURE
//     -Changed FIELD_OBC_ENGINE_TEMP to FIELD_OBC_COOLANT_TEMP and added hiRes mode
//  2007/02/25  Martin D. Flynn
//     -Changed 'FIELD_OBC_FAULT_CODE' to 'FIELD_OBC_J1708_FAULT'
//     -Changed 'obcFaultCode' to 'obcJ1708Fault'
//  2007/03/11  Martin D. Flynn
//     -Added support for 'FIELD_OBC_FUEL_USED'
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#define SKIP_TRANSPORT_MEDIA_CHECK // only if TRANSPORT_MEDIA not used in this file 
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

#include "custom/log.h"

#include "tools/stdtypes.h"
#include "tools/bintools.h"
#include "tools/strtools.h"
#include "tools/utctools.h"

#include "events.h"

// ----------------------------------------------------------------------------

/* define standard resolution fixed packet type */
static FieldDef_t   FixedFields_30[] = {
    EVENT_FIELD(FIELD_STATUS_CODE   , LO_RES, 0, 2),
    EVENT_FIELD(FIELD_TIMESTAMP     , LO_RES, 0, 4),
    EVENT_FIELD(FIELD_GPS_POINT     , LO_RES, 0, 6),
    EVENT_FIELD(FIELD_SPEED         , LO_RES, 0, 1),
    EVENT_FIELD(FIELD_HEADING       , LO_RES, 0, 1),
    EVENT_FIELD(FIELD_ALTITUDE      , LO_RES, 0, 2),
    EVENT_FIELD(FIELD_DISTANCE      , LO_RES, 0, 3),
    EVENT_FIELD(FIELD_SEQUENCE      , LO_RES, 0, 1),
};
static CustomDef_t FixedPacket_30 = {
    PKT_CLIENT_FIXED_FMT_STD,
    (sizeof(FixedFields_30)/sizeof(FixedFields_30[0])),
    FixedFields_30
};

/* define high resolution fixed packet type */
static FieldDef_t   FixedFields_31[] = {
    EVENT_FIELD(FIELD_STATUS_CODE   , HI_RES, 0, 2),
    EVENT_FIELD(FIELD_TIMESTAMP     , HI_RES, 0, 4),
    EVENT_FIELD(FIELD_GPS_POINT     , HI_RES, 0, 8),
    EVENT_FIELD(FIELD_SPEED         , HI_RES, 0, 2),
    EVENT_FIELD(FIELD_HEADING       , HI_RES, 0, 2),
    EVENT_FIELD(FIELD_ALTITUDE      , HI_RES, 0, 3),
    EVENT_FIELD(FIELD_DISTANCE      , HI_RES, 0, 3),
    EVENT_FIELD(FIELD_SEQUENCE      , HI_RES, 0, 1),
};
static CustomDef_t FixedPacket_31 = {
    PKT_CLIENT_FIXED_FMT_HIGH,
    (sizeof(FixedFields_31)/sizeof(FixedFields_31[0])),
    FixedFields_31
};

/* table of predefined formats */
static CustomDef_t *FixedEventTable[] = {
    &FixedPacket_30,
    &FixedPacket_31,
};

/* table of custom formats */
static CustomDef_t *CustomEventTable[] = {
    (CustomDef_t*)0,
    (CustomDef_t*)0,
    (CustomDef_t*)0,
    (CustomDef_t*)0,
    (CustomDef_t*)0
};

// ----------------------------------------------------------------------------

utBool evAddCustomDefinition(CustomDef_t *cd)
{
    int i, maxSize;
    maxSize = sizeof(CustomEventTable)/sizeof(CustomEventTable[0]);
    for (i = 0; i < maxSize; i++) {
        if (!CustomEventTable[i]) {
            CustomEventTable[i] = cd;
            return utTrue;
        }
    }
    return utFalse;
}

// ----------------------------------------------------------------------------

static void _evSetFieldMask(Event_t *er, UInt16 type)
{
    if (er) {
        UInt16 ndx = type / 8;
        if (ndx < sizeof(er->fieldMask)) {
            UInt16 bit = type % 8;
            er->fieldMask[ndx] |= (1 << bit);
        }
    }
}

utBool evIsFieldSet(Event_t *er, UInt16 type)
{
    if (er) {
        UInt16 ndx = type / 8;
        if (ndx < sizeof(er->fieldMask)) {
            UInt16 bit = type % 8;
            return (er->fieldMask[ndx] & (1 << bit))? utTrue : utFalse;
        }
    }
    return utFalse;
}

// ----------------------------------------------------------------------------

static CustomDef_t *_evGetCustomDefinitionForType(ClientPacketType_t hdrType)
{
    int i, maxSize;
    
    /* first check fixed formats */
    maxSize = sizeof(FixedEventTable)/sizeof(FixedEventTable[0]);
    for (i = 0; (i < maxSize) && FixedEventTable[i]; i++) {
        if (hdrType == FixedEventTable[i]->hdrType) {
            return FixedEventTable[i];
        }
    }
    
    /* check custom formats */
    maxSize = sizeof(CustomEventTable)/sizeof(CustomEventTable[0]);
    for (i = 0; (i < maxSize) && CustomEventTable[i]; i++) {
        if (hdrType == CustomEventTable[i]->hdrType) {
            return CustomEventTable[i];
        }
    }
    
    /* still not found */
    return (CustomDef_t*)0;
}

static Event_t * _evClearEvent(Event_t *er)
{
    if (er) {
        
        /* clear entire structure */
        memset(er, 0, sizeof(Event_t));
        
        /* default timestamp */
        er->timestamp[0]    = utcGetTimeSec();
        
        /* gps point */
        int gi;
        for (gi = 0; gi < sizeof(er->gpsPoint)/sizeof(er->gpsPoint[0]); gi++) {
            gpsPointClear(&(er->gpsPoint[gi]));
        }
        
        /* gps attributes */
        er->speedKPH        = GPS_UNDEFINED_SPEED;
        er->heading         = GPS_UNDEFINED_HEADING;
        er->altitude        = GPS_UNDEFINED_ALTITUDE;
        er->distanceKM      = GPS_UNDEFINED_DISTANCE;
        er->odometerKM      = GPS_UNDEFINED_DISTANCE;

        /* sensor init */
#ifdef EVENT_INCL_TEMPERATURE
        int ai;
        for (ai = 0; ai < sizeof(er->tempLO)/sizeof(er->tempLO[0]); ai++) {
            er->tempLO[ai]   = GPS_UNDEFINED_TEMPERATURE;
            er->tempHI[ai]   = GPS_UNDEFINED_TEMPERATURE;
            er->tempAV[ai]   = GPS_UNDEFINED_TEMPERATURE;
        }
#endif

        /* misc */
        er->topSpeedKPH     = GPS_UNDEFINED_SPEED;

        /* gps */
#ifdef EVENT_INCL_GPS_STATS
        er->gpsHorzAccuracy = GPS_UNDEFINED_ACCURACY;
        er->gpsVertAccuracy = GPS_UNDEFINED_ACCURACY;
        er->gpsMagVariation = GPS_UNDEFINED_MAG_VARIATION;
        er->gpsGeoidHeight  = GPS_UNDEFINED_GEOID_HEIGHT;
        er->gpsPDOP         = GPS_UNDEFINED_DOP;
        er->gpsHDOP         = GPS_UNDEFINED_DOP;
        er->gpsVDOP         = GPS_UNDEFINED_DOP;
#endif

    }
    return er;
}

#define LIMIT_INDEX(N,L)    (((N) >= (L))? ((L) - 1) : (N))
Event_t *evParseEventPacket(Packet_t *pkt, Event_t *er)
{
    
    /* get event format definition */
    CustomDef_t *custDef = _evGetCustomDefinitionForType(pkt->hdrType);
    if (!custDef) {
        //logERROR(LOGSRC,"Custom event definition not found: %04X", pkt->hdrType);
        return (Event_t*)0;
    }
    
    /* field info */
    UInt8 fldLen  = custDef->fldLen;
    FieldDef_t *fld = custDef->fld;

    /* source buffer */
    Buffer_t bb, *bf = binBuffer(&bb, pkt->data, pkt->dataLen, BUFFER_SOURCE);
    
    /* clear event structure before we begin */
    _evClearEvent(er);
    
    /* parse custom fields */
    int i;
    for (i = 0; i < fldLen; i++) {
        int   len        = (int)fld[i].length;
        UInt8 ndx        = (UInt8)fld[i].index;
        utBool isHiRes   = fld[i].hiRes;
        UInt32 uVal32    = 0L;
        Int32 iVal32     = 0L;
        switch ((EventFieldType_t)fld[i].type) {
            
            case FIELD_STATUS_CODE      : // hex
                // 'len' had better be '2' (however, no checking is made at this point)
                binBufScanf(bf, "%*x", len, &uVal32);
                er->statusCode = (UInt16)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_TIMESTAMP        :
                // 'len' had better be '4' (however, no checking is made at this point)
                ndx = LIMIT_INDEX(ndx, sizeof(er->timestamp)/sizeof(er->timestamp[0]));
                binBufScanf(bf, "%*u", len, &(er->timestamp[ndx]));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_INDEX            :
                binBufScanf(bf, "%*u", len, &(er->index));
                _evSetFieldMask(er, fld[i].type);
                break;
                
            case FIELD_GPS_POINT        :
                // 'len' had better be '6' or '8' (however, no checking is made at this point)
                ndx = LIMIT_INDEX(ndx, sizeof(er->gpsPoint)/sizeof(er->gpsPoint[0]));
                binBufScanf(bf, "%*g", len, &(er->gpsPoint[ndx]));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_AGE          :
                binBufScanf(bf, "%*u", len, &(er->gpsAge));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_SPEED            : // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->speedKPH = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_HEADING          : // double
                binBufScanf(bf, (isHiRes? "%*u" : "%*x"), len, &uVal32);
                er->heading = isHiRes? ((double)uVal32 / 100.0) : ((double)uVal32 * 360.0/255.0);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_ALTITUDE         : // double +/-
                binBufScanf(bf, "%*i", len, &iVal32);
                er->altitude = isHiRes? ((double)iVal32 / 10.0) : (double)iVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_DISTANCE         : // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->distanceKM = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_ODOMETER         : // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->odometerKM = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;

            case FIELD_SEQUENCE         : // hex
                er->seqLen = len;
                binBufScanf(bf, "%*x", len, &(er->sequence));
                _evSetFieldMask(er, fld[i].type);
                break;
                
            case FIELD_GEOFENCE_ID      : // hex
                ndx = LIMIT_INDEX(ndx, sizeof(er->geofenceID)/sizeof(er->geofenceID[0]));
                binBufScanf(bf, "%*x", len, &(er->geofenceID[ndx]));
                er->geofenceIDMask |= (1 << ndx);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_TOP_SPEED        : // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->topSpeedKPH = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;

            case FIELD_STRING           :
                ndx = LIMIT_INDEX(ndx, sizeof(er->string)/sizeof(er->string[0]));
                binBufScanf(bf, "%*s", len, er->string[ndx]);
                er->stringMask |= (1 << ndx);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_STRING_PAD       :
                ndx = LIMIT_INDEX(ndx, sizeof(er->string)/sizeof(er->string[0]));
                binBufScanf(bf, "%*p", len, er->string[ndx]);
                er->stringMask |= (1 << ndx);
                _evSetFieldMask(er, fld[i].type);
                break;

            case FIELD_ENTITY           :
                ndx = LIMIT_INDEX(ndx, sizeof(er->entity)/sizeof(er->entity[0]));
                binBufScanf(bf, "%*s", len, er->entity[ndx]);
                er->entityMask |= (1 << ndx);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_ENTITY_PAD       :
                ndx = LIMIT_INDEX(ndx, sizeof(er->entity)/sizeof(er->entity[0]));
                binBufScanf(bf, "%*p", len, er->entity[ndx]);
                er->entityMask |= (1 << ndx);
                _evSetFieldMask(er, fld[i].type);
                break;

            case FIELD_BINARY           :
                // the binary field in the 'er' structure must already be initialized
                if (er->binary) {
                    if (len <= er->binaryLen) {
                        binBufScanf(bf, "%*b", len, er->binary);
                    } else {
                        binBufScanf(bf, "%*b%*z", er->binaryLen, er->binary, len - er->binaryLen);
                    }
                } else {
                    binBufScanf(bf, "%*z", len);
                }
                _evSetFieldMask(er, fld[i].type);
                break;

            case FIELD_INPUT_ID         : // hex
                binBufScanf(bf, "%*x", len, &(er->inputID));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_INPUT_STATE      : // hex
                binBufScanf(bf, "%*x", len, &(er->inputState));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OUTPUT_ID        : // hex
                binBufScanf(bf, "%*x", len, &(er->outputID));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OUTPUT_STATE     : // hex
                binBufScanf(bf, "%*x", len, &(er->outputState));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_ELAPSED_TIME     :
                ndx = LIMIT_INDEX(ndx, sizeof(er->elapsedTimeSec)/sizeof(er->elapsedTimeSec[0]));
                binBufScanf(bf, "%*u", len, &(er->elapsedTimeSec[ndx]));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_COUNTER          :
                ndx = LIMIT_INDEX(ndx, sizeof(er->counter)/sizeof(er->counter[0]));
                binBufScanf(bf, "%*u", len, &(er->counter[ndx]));
                _evSetFieldMask(er, fld[i].type);
                break;

            case FIELD_SENSOR32_LOW     :
                ndx = LIMIT_INDEX(ndx, sizeof(er->sensor32LO)/sizeof(er->sensor32LO[0]));
                binBufScanf(bf, "%*u", len, &(er->sensor32LO[ndx]));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_SENSOR32_HIGH    :
                ndx = LIMIT_INDEX(ndx, sizeof(er->sensor32HI)/sizeof(er->sensor32HI[0]));
                binBufScanf(bf, "%*u", len, &(er->sensor32HI[ndx]));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_SENSOR32_AVER    :
                ndx = LIMIT_INDEX(ndx, sizeof(er->sensor32AV)/sizeof(er->sensor32AV[0]));
                binBufScanf(bf, "%*u", len, &(er->sensor32AV[ndx]));
                _evSetFieldMask(er, fld[i].type);
                break;

            case FIELD_TEMP_LOW         : // double +/-
                ndx = LIMIT_INDEX(ndx, sizeof(er->tempLO)/sizeof(er->tempLO[0]));
                binBufScanf(bf, "%*i", len, &iVal32);
                er->tempLO[ndx] = isHiRes? ((double)iVal32 / 10.0) : (double)iVal32;
                er->tempLOMask |= (1 << ndx);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_TEMP_HIGH        : // double +/-
                ndx = LIMIT_INDEX(ndx, sizeof(er->tempHI)/sizeof(er->tempHI[0]));
                binBufScanf(bf, "%*i", len, &iVal32);
                er->tempHI[ndx] = isHiRes? ((double)iVal32 / 10.0) : (double)iVal32;
                er->tempHIMask |= (1 << ndx);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_TEMP_AVER        : // double +/-
                ndx = LIMIT_INDEX(ndx, sizeof(er->tempAV)/sizeof(er->tempAV[0]));
                binBufScanf(bf, "%*i", len, &iVal32);
                er->tempAV[ndx] = isHiRes? ((double)iVal32 / 10.0) : (double)iVal32;
                er->tempAVMask |= (1 << ndx);
                _evSetFieldMask(er, fld[i].type);
                break;

            case FIELD_GPS_DGPS_UPDATE  :
                binBufScanf(bf, "%*u", len, &(er->gpsDgpsUpdate));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_HORZ_ACCURACY: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->gpsHorzAccuracy = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_VERT_ACCURACY: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->gpsVertAccuracy = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_SATELLITES   :
                binBufScanf(bf, "%*u", len, &(er->gpsSatellites));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_MAG_VARIATION: // double +/-
                binBufScanf(bf, "%*i", len, &iVal32);
                er->gpsMagVariation = (double)iVal32 / 100.0;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_QUALITY      :
                binBufScanf(bf, "%*u", len, &(er->gpsQuality));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_TYPE         :
                binBufScanf(bf, "%*u", len, &(er->gps2D3D));
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_GEOID_HEIGHT : // double +/-
                binBufScanf(bf, "%*i", len, &iVal32);
                er->gpsGeoidHeight = isHiRes? ((double)iVal32 / 10.0) : (double)iVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_PDOP         : // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->gpsPDOP = (double)uVal32 / 10.0;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_HDOP         : // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->gpsHDOP = (double)uVal32 / 10.0;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_GPS_VDOP         : // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->gpsVDOP = (double)uVal32 / 10.0;
                _evSetFieldMask(er, fld[i].type);
                break;

#ifdef EVENT_INCL_OBC
            case FIELD_OBC_VALUE: // EvOBCValue_t
                ndx = LIMIT_INDEX(ndx, sizeof(er->obcValue)/sizeof(er->obcValue[0]));
                if (len >= 4) {
                    EvOBCValue_t *J = &(er->obcValue[ndx]);
                    UInt32 mid = 0L, pid = 0L;
                    binBufScanf(bf, "%2u%2u", &mid, &pid);
                    J->mid = (UInt16)mid;
                    J->pid = (UInt16)pid;
                    len -= 4;
                    if (len <= sizeof(J->data)) {
                        J->dataLen = len;
                        binBufScanf(bf, "%*b", (int)J->dataLen, J->data);
                    } else {
                        J->dataLen = (UInt8)sizeof(J->data);
                        binBufScanf(bf, "%*b%*z", (int)J->dataLen, J->data, len - (int)J->dataLen);
                    }
                } else {
                    binBufScanf(bf, "%*z", len);
                }
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_GENERIC: // UInt32
                ndx = LIMIT_INDEX(ndx, sizeof(er->obcGeneric)/sizeof(er->obcGeneric[0]));
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcGeneric[ndx] = uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_J1708_FAULT: // UInt32
                ndx = LIMIT_INDEX(ndx, sizeof(er->obcJ1708Fault)/sizeof(er->obcJ1708Fault[0]));
                binBufScanf(bf, "%*x", len, &uVal32);
                er->obcJ1708Fault[ndx] = uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_DISTANCE   : // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcDistanceKM = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_ENGINE_HOURS: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcEngineHours = (double)uVal32 / 10.0;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_ENGINE_RPM: // UInt32
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcEngineRPM = uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_COOLANT_TEMP: // double
                binBufScanf(bf, "%*i", len, &iVal32);
                er->obcCoolantTemp = isHiRes? ((double)iVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_COOLANT_LEVEL: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcCoolantLevel = isHiRes? ((double)uVal32 / 1000.0) : ((double)uVal32 / 100.0);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_OIL_LEVEL: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcOilLevel = isHiRes? ((double)uVal32 / 1000.0) : ((double)uVal32 / 100.0);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_OIL_PRESSURE: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcOilPressure = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_FUEL_LEVEL: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcFuelLevel = isHiRes? ((double)uVal32 / 1000.0) : ((double)uVal32 / 100.0);
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_FUEL_ECONOMY: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcFuelEconomy = (double)uVal32 / 10.0;
                er->obcAvgFuelEcon = (double)uVal32 / 10.0;
                _evSetFieldMask(er, fld[i].type);
                break;
            case FIELD_OBC_FUEL_USED: // double
                binBufScanf(bf, "%*u", len, &uVal32);
                er->obcFuelUsed = isHiRes? ((double)uVal32 / 10.0) : (double)uVal32;
                _evSetFieldMask(er, fld[i].type);
                break;
#endif

        }
    }

    /* return event */
    return er;

}

// ----------------------------------------------------------------------------

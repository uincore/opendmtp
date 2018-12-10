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
//  Geozone upload (server to client) support.
// ---
// Change History:
//  2006/05/07  Martin D. Flynn
//     -Initial release
// ----------------------------------------------------------------------------

#include "server/defaults.h"
#if defined(INCLUDE_GEOZONE)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/base64.h"
#include "tools/checksum.h"
#include "tools/io.h"

#include "base/props.h"

#include "server/upload.h"
#include "server/packet.h"
#include "server/protocol.h"
#include "server/log.h"
#include "server/server.h"
#include "server/geozone.h"

// ----------------------------------------------------------------------------

#define PRINT_GEOZONE

// ----------------------------------------------------------------------------

#define NO_ZONE                 ((GeoZoneID_t)0x0)
#define MAX_GEOZONES            4000
#define PACKED_GEOZONE_SIZE     16

// ----------------------------------------------------------------------------

static char geozFilename[80] = { 0 };
static utBool didUploadGeoZones = utFalse;

/* set upload geozone file */
void geozSetGeozoneFile(const char *file)
{
    memset(geozFilename, 0, sizeof(geozFilename));
    if (file && *file) {
        logINFO(LOGSRC,"Setting Geozone file: %s", file);
        strncpy(geozFilename, file, sizeof(geozFilename) - 1);
    }
}

/* upload geozones */
void geozUploadGeozonesNow()
{
    if (!didUploadGeoZones) {
        didUploadGeoZones = utTrue;
        if (*geozFilename) {
            geozUploadGeozones(geozFilename);
            *geozFilename = 0;
        }
    }
}

// ----------------------------------------------------------------------------

/* encode geozone into buffer */
static void encodeGeozone(Buffer_t *buf, ServerGeozone_t *gz)
{
    UInt16 typeRad = ((gz->type << 13) & 0xE000) | (gz->radius & 0x1FFF);
    // PACKED_GEOZONE_SIZE must match this size
    binBufPrintf(buf, "%2u%2u%6g%6g", (UInt32)gz->zoneID, (UInt32)typeRad, &(gz->pt[0]), &(gz->pt[1]));
}

// ----------------------------------------------------------------------------

/* print GeoZone */
#ifdef PRINT_GEOZONE
static void _printGeozone(ServerGeozone_t *gz)
{
    if (gz) {
        logDEBUG(LOGSRC,"GeoZone : id=%04X", gz->zoneID);
        logDEBUG(LOGSRC,"  Points: (type=%u, radius=%u) 0=%.5f/%.5f, 1=%.5f/%.5f\n", 
            gz->type, gz->radius, 
            gz->pt[0].latitude, gz->pt[0].longitude, 
            gz->pt[1].latitude, gz->pt[1].longitude);
    }
}
#endif

/* parse and upload geozones to client */
static UInt16 _uploadFile(const char *geozFile, utBool performUpload)
{
    // File format:
    //   zoneID,type,radius,lat0,lon0,lat1,lon1
    //   101,0,130,28.1234,-119.4321,28.1256,-119.4367
    if (!geozFile || !*geozFile) {
        // nothing to parse
        logERROR(LOGSRC,"Upload file not specified");
        return -1;
    }

    /* open file */
    FILE *file = ioOpenStream(geozFile, IO_OPEN_READ);
    if (!file) {
        // open error
        logINFO(LOGSRC,"Error openning file: %s", geozFile);
        return -1;
    }

    /* init for parse */
    logINFO(LOGSRC,"Parsing Geozones from file: %s", geozFile);
    char geozVers[32];
    int geozVersLength = sizeof(geozVers);
    memset(geozVers, 0, geozVersLength);

    /* buffer */
    UInt8 buf[PACKET_MAX_PAYLOAD_LENGTH];
    Buffer_t bb, *dst = binBuffer(&bb, buf, sizeof(buf), BUFFER_DESTINATION);
    //binResetBuffer(dst);

    /* read zones */
    int line = 0;
    int usedZones = 0;
    utBool autoSave = utTrue;
    char zoneRecord[80], *zoneFld[16];
    for (;;) {
        line++;
        
        /* read line from file */
        int len = ioReadLine(file, zoneRecord, sizeof(zoneRecord));
        if (len <= 0) {
            // EOF / read error
            logINFO(LOGSRC,"[line %d] EOF: %s", line, geozFile);
            break;
        }

        /* trim record */
        char *zr = strTrim(zoneRecord);

        /* ignore blank records and comments */
        if (!*zr || (*zr == '#')) {
            // ignore blank records and comments
            continue;
        }
        
        /* scan for trailing comments */
        char *c = zr;
        while (*c && (*c != '#')) { c++; }
        if (*c == '#') {
            // trailing comment found
            *c = 0;
        }
        logINFO(LOGSRC,"[line %d] %s", line, zr);

        /* execute directives: "@CLEAR", "@VERSION", "@SAVE", "@NOSAVE" */
        if (*zr == '@') {
            if (strStartsWithIgnoreCase(zr,"@version")) {
                // "@version 1.2"
                char *v = strTrim(zr + 8);
                strncpy(geozVers, v, geozVersLength - 1);
                if (performUpload) {
                    logINFO(LOGSRC,"[line %d] Setting Geozone version: %s [%d]", line, geozVers, strlen(geozVers));
                    protSetPropString(PROP_GEOF_VERSION, geozVers);
                }
            } else
            if (strStartsWithIgnoreCase(zr,"@clear")) {
                if (performUpload) {
                    // "@clear"
                    logINFO(LOGSRC,"[line %d] Clear existing Geozones ...", line);
                    UInt8 rmv = (UInt8)GEOF_CMD_REMOVE;
                    protSetPropBinary(PROP_CMD_GEOF_ADMIN, &rmv, 1);
                    binResetBuffer(dst); // remove anything we may already have in the packet buffer
                    usedZones = 0;
                }
            } else
            if (strStartsWithIgnoreCase(zr,"@save")) {
                // "@save"
                logINFO(LOGSRC,"[line %d] Saving Geozones ...", line);
                if (BUFFER_DATA_LENGTH(dst) > 0) {
                    // flush remaining zones we have in the buffer
                    protSetPropBinary(PROP_CMD_GEOF_ADMIN, BUFFER_PTR(dst), BUFFER_DATA_LENGTH(dst));
                    binResetBuffer(dst);
                }
                // tell client to save to disk
                UInt8 save = (UInt8)GEOF_CMD_SAVE;
                protSetPropBinary(PROP_CMD_GEOF_ADMIN, &save, 1);
                autoSave = utFalse; // file contains an explicit '@save', do not autosave below
            } else
            if (strStartsWithIgnoreCase(zr,"@nosave")) {
                // "@nosave"
                autoSave = utFalse; // file specified that geozones should not be autosaved
            } else {
                logERROR(LOGSRC,"[line %d] Unrecognized directive: %s", line, zr);
            }
            continue;
        }

        /* parse record */
        // zoneID,type,radius,lat0,lon0,lat1,lon1
        strParseArray(zr, zoneFld, 10);
        ServerGeozone_t geozData, *gz = &geozData; // &geozList[usedZones];
        gz->zoneID = (GeoZoneID_t)strParseUInt32(zoneFld[0], (UInt32)NO_ZONE);
        gz->type   = (UInt16)(strParseUInt32(zoneFld[1], GEOF_DUAL_POINT_RADIUS) & 0x7);
        gz->radius = (UInt16)(strParseUInt32(zoneFld[2], 300L) & 0x1FFF); // meters
        gz->pt[0].latitude  = strParseDouble(zoneFld[3],  0.0);
        gz->pt[0].longitude = strParseDouble(zoneFld[4],  0.0);
        gz->pt[1].latitude  = strParseDouble(zoneFld[5],  0.0);
        gz->pt[1].longitude = strParseDouble(zoneFld[6],  0.0);
#ifdef PRINT_GEOZONE
        _printGeozone(gz);
#endif

        /* validate zone */
        if (gz->zoneID <= 0) {
            // NO_ZONE ids are not allowed
            logERROR(LOGSRC,"[line %d] Invalid ZoneID (must be greater than 0)", line);
            continue;
        } else
        if ((gz->type != 0) && (gz->type != 1)) {
            // invalid type
            logERROR(LOGSRC,"[line %d] Invalid Zone type (must be either '0' or '1')", line);
            continue;
        } else
        if ((gz->radius <= 0) || (gz->radius > 0x1FFF)) {
            // this will only occur if radius is 0, since the size is already bounded
            logERROR(LOGSRC,"[line %d] Invalid radius (must be > 0 and <= 8191)", line);
            continue;
        } else
        if ((gz->pt[0].latitude == 0.0) && (gz->pt[0].longitude == 0.0) && (gz->type != 0)) {
            // type==0 is the only type that can have a zero Lat/Lng.
            logERROR(LOGSRC,"[line %d] Invalid Lat/Lng for specified type (first point)", line);
            continue;
        } else
        if ((gz->pt[1].latitude == 0.0) && (gz->pt[1].longitude == 0.0) && (gz->type != 0)) {
            // type==0 is the only type that can have a zero Lat/Lng.
            logERROR(LOGSRC,"[line %d] Invalid Lat/Lng for specified type (second point)", line);
            continue;
        } else
        if (((gz->pt[0].latitude != 0.0) || (gz->pt[0].longitude != 0.0)) && !gpsPointIsValid(&(gz->pt[0]))) {
            // (90 > Lat > -90) or (180 > Lng > -180) range test failed
            logERROR(LOGSRC,"[line %d] Invalid Lat/Lng (first point)", line);
            continue;
        } else
        if (((gz->pt[1].latitude != 0.0) || (gz->pt[1].longitude != 0.0)) && !gpsPointIsValid(&(gz->pt[1]))) {
            // (90 > Lat > -90) or (180 > Lng > -180) range test failed
            logERROR(LOGSRC,"[line %d] Invalid Lat/Lng (second point)", line);
            continue;
        }
        
        /* count zone */
        usedZones++;

        /* packetize and send */
        if (performUpload) {
            if (BUFFER_DATA_LENGTH(dst) == 0) {
                binBufPrintf(dst, "%1x", (UInt32)GEOF_CMD_ADD);
            }
            encodeGeozone(dst, gz);
            if ((BUFFER_DATA_LENGTH(dst) + PACKED_GEOZONE_SIZE) > 255) {
                protSetPropBinary(PROP_CMD_GEOF_ADMIN, BUFFER_PTR(dst), BUFFER_DATA_LENGTH(dst));
                binResetBuffer(dst);
            }
        }

    }
    
    /* close file */
    ioCloseStream(file);

    /* finalize upload  */
    if (performUpload) {
        if (BUFFER_DATA_LENGTH(dst) > 0) {
            // flush remaining zones we have in the buffer
            protSetPropBinary(PROP_CMD_GEOF_ADMIN, BUFFER_PTR(dst), BUFFER_DATA_LENGTH(dst));
            binResetBuffer(dst);
        }
        // tell client to save to disk
        if (autoSave) {
            logINFO(LOGSRC,"Auto-Saving Geozones ...");
            UInt8 save = (UInt8)GEOF_CMD_SAVE;
            protSetPropBinary(PROP_CMD_GEOF_ADMIN, &save, 1);
        }
    }

    /* return the number of loaded zones */
    logINFO(LOGSRC,"Loaded %u Geozones", usedZones);
    return usedZones;

}

// ----------------------------------------------------------------------------

/* parse and upload geozones to client */
utBool geozUploadGeozones(const char *file)
{
    return _uploadFile(file, utTrue);
}

// ----------------------------------------------------------------------------
#endif // INCLUDE_GEOZONE

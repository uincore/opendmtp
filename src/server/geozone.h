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

#ifndef _GEOZONE_H
#define _GEOZONE_H

#include "server/defaults.h"
#include "server/server.h"

#if defined(INCLUDE_GEOZONE)
//#warning Including GeoZone support

// ----------------------------------------------------------------------------
// GeoFence types:

/* geofence types */
// client need not support all of these, however, it must generate an error if
// the server presents a format that the client does not support.
#define GEOF_DUAL_POINT_RADIUS      0       // 2 point/radius zones
#define GEOF_BOUNDED_RECT           1       // pt[0] is NorthWest, pt[1] is SouthEast
#define GEOF_SWEPT_POINT_RADIUS     2       // swept point/radius (point to point) (if implemented)
//#define GEOF_DELTA_RECT           3       // pt[0] is center, pt[1] is delta lat/lon

/* Geozone ID definition */
typedef UInt16                      GeoZoneID_t; // 2 bytes
#define NO_ZONE                     ((GeoZoneID_t)0x0)

/* Geozone structure */
// Note: This is NOT the same as the stucture for the client.  The client structure
// is optimized for size.  The server does not need this optimization.
typedef struct
{
    GeoZoneID_t                     zoneID;     //  2 bytes
    UInt16Pack                      type:3;     // --
    UInt16Pack                      radius:13;  //  2 bytes
    GPSPoint_t                      pt[2];      // 32 bytes (Server use only!)
} ServerGeozone_t;                              // 36 bytes total (server use only!)

// ----------------------------------------------------------------------------
// Property GeoZone admin command types:

#define GEOF_CMD_ADD                0x10
#define GEOF_CMD_REMOVE             0x20
#define GEOF_CMD_SAVE               0x30

// ----------------------------------------------------------------------------

utBool geozUploadGeozones(const char *file);

void geozSetGeozoneFile(const char *file);
void geozUploadGeozonesNow();

// ----------------------------------------------------------------------------

#endif // INCLUDE_GEOZONE
#endif // _GEOZONE_H

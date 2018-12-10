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

#ifndef _GPS_H
#define _GPS_H
#ifdef __cplusplus
extern "C" {
#endif

#include "custom/defaults.h"

#include "tools/stdtypes.h"
#include "tools/gpstools.h"

#include "base/propman.h"
#include "base/events.h"

// ----------------------------------------------------------------------------
// GPS receiver type

#define GPS_RECEIVER_UNKOWN         ""
#define GPS_RECEIVER_GARMIN         "garmin"
#define GPS_RECEIVER_TRIMBLE        "trimble"
#define GPS_RECEIVER_UBLOX          "ublox"
#define GPS_RECEIVER_SIRF           "sirf"

// ----------------------------------------------------------------------------

// This should be set to a value greater than the interval in which the GPS
// Valid(A)/Invalid(V) events are arriving from the GPS receiver.  Typically
// The NMEA-0183 GPRMC events are sent once per second, so this value should
// be a few seconds greater than "1".  This value is used to determine if the
// GPS receiver is no longer working if the current GPS fix becomes "stale".
// If no A/V records have been received from the GPS receiver within the time
// period specified by this value, then we consider the GPS receiver to have
// failed.
#define GPS_EVENT_INTERVAL          30L

// ----------------------------------------------------------------------------

void gpsInitialize(eventAddFtn_t queueEvent);

void gpsSetFixStale(utBool stale);
utBool gpsIsFixStale();

GPS_t *gpsCheckMinimums(GPS_t *gps);

GPS_t *gpsAquire(GPS_t *gps, UInt32 timeoutMS);
GPS_t *gpsGetLastGPS(GPS_t *gps, Int16 maxAgeSec);

// ----------------------------------------------------------------------------

GPSDiagnostics_t *gpsGetDiagnostics(GPSDiagnostics_t *stats);

// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif

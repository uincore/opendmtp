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
//  Main entry point for socket based DMTP sample server.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/27  Martin D. Flynn
//     -Added '-output <file>' option to specify where event packet may be stored.
//  2006/02/05  Martin D. Flynn
//     -Added support for write events in CSV format.
//  2006/04/11  Martin D. Flynn
//     -Force POSIX locale on startup.
// ----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <pthread.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/io.h"
#include "tools/threads.h"

#include "base/props.h"
#include "base/statcode.h"

#include "server/defaults.h"
#include "server/server.h"
#include "server/packet.h"
#include "server/events.h"
#include "server/protocol.h"
#include "server/log.h"
#include "server/upload.h"
#include "server/geozone.h"

// ----------------------------------------------------------------------------

/* custom event format */
static FieldDef_t   CustomFields_50[] = {
    EVENT_FIELD(FIELD_STATUS_CODE   , HI_RES, 0, 2),            // code
    EVENT_FIELD(FIELD_TIMESTAMP     , HI_RES, 0, 4),            // time
    EVENT_FIELD(FIELD_GPS_AGE       , HI_RES, 0, 2),            // GPS fix age
    EVENT_FIELD(FIELD_GPS_POINT     , HI_RES, 0, 8),            // latitude/longitude
    EVENT_FIELD(FIELD_SPEED         , HI_RES, 0, 2),            // speed KPH
    EVENT_FIELD(FIELD_HEADING       , HI_RES, 0, 2),            // heading
    EVENT_FIELD(FIELD_ALTITUDE      , HI_RES, 0, 3),            // altitude
    EVENT_FIELD(FIELD_GEOFENCE_ID   , HI_RES, 0, 2),            // geofence
    EVENT_FIELD(FIELD_STRING        , HI_RES, 0, MAX_ID_SIZE),  // user
    EVENT_FIELD(FIELD_STRING        , HI_RES, 1, MAX_ID_SIZE),  // id
    EVENT_FIELD(FIELD_TOP_SPEED     , HI_RES, 0, 2),            // top speed KPH
    EVENT_FIELD(FIELD_DISTANCE      , HI_RES, 0, 3),            // distance KM
    EVENT_FIELD(FIELD_SEQUENCE      , HI_RES, 0, 2),            // sequence
};
static CustomDef_t CustomPacket_50 = {
    PKT_CLIENT_DMTSP_FORMAT_0,
    (sizeof(CustomFields_50)/sizeof(CustomFields_50[0])),
    CustomFields_50
};

// ----------------------------------------------------------------------------

static char savePacketFile[80] = "./scomserv.dmt";
static utBool saveAsCSV = utFalse;

static char scName[32];
static const char *statusCodeName(UInt16 code)
{
    switch (code) {
        case STATUS_LOCATION:               return "Location";
        case STATUS_WAYMARK:                return "Waymark";
        case STATUS_QUERY:                  return "Query";         // "Ping"
        case STATUS_MOTION_START:           return "StartMotion";
        case STATUS_MOTION_IN_MOTION:       return "InMotion";
        case STATUS_MOTION_STOP:            return "StopMotion";
        case STATUS_MOTION_DORMANT:         return "Dormant";
        case STATUS_MOTION_EXCESS_SPEED:    return "Speeding";
        case STATUS_GEOFENCE_ARRIVE:        return "Arrival";
        case STATUS_GEOFENCE_DEPART:        return "Departure";
        case STATUS_LOGIN:                  return "Login";
        case STATUS_LOGOUT:                 return "Logout";
        case STATUS_ELAPSED_LIMIT_00:       return "Timer0";
        case STATUS_ELAPSED_LIMIT_01:       return "Timer1";
        case STATUS_CONNECT:                return "Connect";
        case STATUS_DISCONNECT:             return "Disconnect";
    }
    sprintf(scName, "0x%04X", code);
    return scName;
}

static void mainHandleEvent(Packet_t *pkt, Event_t *ev)
{
    // STATUS_LOCATION
    // STATUS_MOTION_START
    // STATUS_MOTION_IN_MOTION
    // STATUS_MOTION_STOP
    // STATUS_MOTION_DORMANT
    // STATUS_MOTION_EXCESS_SPEED
    // STATUS_GEOFENCE_ARRIVE
    // STATUS_GEOFENCE_DEPART
    // STATUS_GEOFENCE_VIOLATION
    // STATUS_GEOFENCE_ACTIVE
    // STATUS_GEOFENCE_INACTIVE
    // STATUS_LOGIN
    // STATUS_LOGOUT
    // STATUS_ON
    // STATUS_OFF
    
    /* create CSV formatted record */
    UInt8 csv[120], *c = csv;
    struct tm *tmp = localtime(&(ev->timestamp[0]));
    int hr  = tmp->tm_hour, mn = tmp->tm_min, sc = tmp->tm_sec;
    int dy  = tmp->tm_mday, mo = tmp->tm_mon + 1, yr = 1900 + tmp->tm_year;
    const char *codeName = statusCodeName(ev->statusCode);
    sprintf(c,  "%02d/%02d/%02d", yr, mo, dy);                c += strlen(c);
    sprintf(c, ",%02d:%02d:%02d", hr, mn, sc);                c += strlen(c);
    sprintf(c, ",%s"            , codeName);                  c += strlen(c);
    sprintf(c, ",%.5lf"         , ev->gpsPoint[0].latitude);  c += strlen(c);
    sprintf(c, ",%.5lf"         , ev->gpsPoint[0].longitude); c += strlen(c);
    sprintf(c, ",%.1lf"         , ev->speedKPH);              c += strlen(c);
    sprintf(c, ",%.1lf"         , ev->heading);               c += strlen(c);
    sprintf(c, ",%.0lf"         , ev->altitude);              c += strlen(c);
    sprintf(c, ",%s"            , ev->string[0]);             c += strlen(c);
    sprintf(c, ",%s"            , ev->string[1]);             c += strlen(c);
    sprintf(c, ",%.1lf"         , ev->topSpeedKPH);           c += strlen(c);

    /* print event */
    logINFO(LOGSRC,"Event [%02X]: %s", ev->sequence, csv);

    /* save packet */
    if (pkt && *savePacketFile) {
        if (saveAsCSV) {
            sprintf(c, "\n"); c += strlen(c); // record terminator
            ioAppendFile(savePacketFile, csv, c - csv);
        } else {
            UInt8 buf[PACKET_MAX_ENCODED_LENGTH];
            Buffer_t bb, *dest = binBuffer(&bb, buf, sizeof(buf), BUFFER_DESTINATION);
            int len = pktEncodePacket(dest, pkt, ENCODING_HEX);
            if (len > 0) {
                //logINFO(LOGSRC,"Appending packet to file [%s]", savePacketFile);
                ioAppendFile(savePacketFile, buf, len);
            } else {
                logWARNING(LOGSRC,"Invalid event packet, unable to encode!");
            }
        }
    }

}

// ----------------------------------------------------------------------------

static void mainHandleClientInit()
{
    // nothing to do
    logINFO(LOGSRC,"Client initialization ...");
}

// ----------------------------------------------------------------------------

static void mainHandleError(UInt16 errKey, const UInt8 *errData, UInt16 errDataLen)
{
    logINFO(LOGSRC,"Received client error %04X", errKey);
}

// ----------------------------------------------------------------------------

static char sockPortName[32];

static void _protocolThreadRunnable(void *arg)
{
    protocolLoop(
        sockPortName, utFalse/*PORT_LOG*/,
        utFalse/*KEEP_ALIVE*/, 
        utTrue/*CLIENT_SPEAKS_FIRST*/);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Main entry point

static threadThread_t       protoThread;
static threadMutex_t        protoMutex;
#define PROTO_LOCK          MUTEX_LOCK(&protoMutex);
#define PROTO_UNLOCK        MUTEX_UNLOCK(&protoMutex);

static void _usage(const char *pgm, int exitCode)
{
    fprintf(stdout, "Usage: \n");
    fprintf(stdout, "   %s [options ...]\n", pgm);
    fprintf(stdout, "   Specify one and only one of the following:\n");
    fprintf(stdout, "     [-tcp <port>]          - Server TCP port\n");
    fprintf(stdout, "     [-udp <port>]          - Server UDP port (not yet fully implemented)\n");
    fprintf(stdout, "     [-output <file> [csv]] - Name of file where events packets are to be stored\n");
    fprintf(stdout, "                            - Specify 'csv' to store output file in CSV format\n");
    fprintf(stdout, "Note:\n");
    fprintf(stdout, "   To used this simple server with the socket media transport implementation\n");
    fprintf(stdout, "   of the 'dmtp' client, you must run the client with the '-duplex' option in\n");
    fprintf(stdout, "   order to force all transmissions to occur via TCP.  Packet transmissions\n");
    fprintf(stdout, "   sent by the client via UDP will not be heard by this server.\n");
    fprintf(stdout, "\n");
    exit(exitCode);
}

// main entry point
int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "POSIX");
    setDebugMode(utTrue);

    /* init */
    memset(sockPortName, 0, sizeof(sockPortName));
    threadMutexInit(&protoMutex);

    /* header */
    fprintf(stdout, "Sample server socket transport\n");

    /* command line arguments */
    int i;
    utBool udp = utFalse, tcp = utFalse;
    for (i = 1; i < argc; i++) {
        if (strEquals(argv[i], "-help") || strEquals(argv[i], "-h")) {
            // -h[elp]
            _usage(argv[0], 0);
        } else
        if (strEquals(argv[i], "-udp")) {
            // -udp <port>
            i++;
            if ((i < argc) && (*argv[i] != '-')) {
                strncpy(sockPortName, argv[i], sizeof(sockPortName) - 1);
                int sockPort = (int)strParseInt32(sockPortName, -1L);
                if (sockPort <= 0) {
                    fprintf(stderr, "Invalid UDP port: %s\n", argv[i]);
                    _usage(argv[0], 1);
                }
                udp = utTrue;
            } else {
                fprintf(stderr, "Missing UDP port ...\n");
                _usage(argv[0], 1);
            }
        } else
        if (strEquals(argv[i], "-tcp")) {
            // -tcp <port>
            i++;
            if ((i < argc) && (*argv[i] != '-')) {
                strncpy(sockPortName, argv[i], sizeof(sockPortName) - 1);
                int sockPort = (int)strParseInt32(sockPortName, -1L);
                if (sockPort <= 0) {
                    fprintf(stderr, "Invalid TCP port: %s\n", argv[i]);
                    _usage(argv[0], 1);
                }
                tcp = utTrue;
            } else {
                fprintf(stderr, "Missing TCP port ...\n");
                _usage(argv[0], 1);
            }
        } else
        if (strEquals(argv[i], "-output")) {
            // -output <filename> [csv]
            i++;
            if ((i < argc) && (*argv[i] != '-')) {
                const char *fileName = argv[i];
                saveAsCSV = utFalse;
                // parse 'csv', if specified
                if (((i + 1) < argc) && (*argv[i + 1] != '-')) {
                    i++;
                    const char *fmt = argv[i];
                    saveAsCSV = strEqualsIgnoreCase(fmt, "csv");
                }
                strcpy(savePacketFile, fileName);
            } else {
                fprintf(stderr, "Missing output filename ...\n");
                _usage(argv[0], 1);
            }
        } else
        if (strEquals(argv[i], "-csv")) {
            saveAsCSV = utTrue;
        } else 
        {
            fprintf(stderr, "Invalid option: %s\n", argv[i]);
            _usage(argv[0], 1);
        } 
    }
    
    /* header */
    fprintf(stdout, "Simple Socket Server\n");
    fprintf(stdout, "Events will be saved to '%s' [CSV=%s]\n", savePacketFile, (saveAsCSV?"true":"false"));
    
    /* custom event packet format */
    evAddCustomDefinition(&CustomPacket_50);
    
    /* protocol handlers */
    protocolSetEventHandler(&mainHandleEvent);
    protocolSetClientInitHandler(&mainHandleClientInit);
    //protocolSetPropertyHandler(&mainHandleProperty);
    //protocolSetDiagHandler(&mainHandleDiag);
    protocolSetErrorHandler(&mainHandleError);

    /* start server */
    if (tcp) {
        /* TCP read loop */
        fprintf(stdout, "(Make sure the client sends all packets via TCP to this host:port)\n\n");
        if (threadCreate(&protoThread,&_protocolThreadRunnable,0,"SockServe") == 0) {
            // thread started successfully
        } else {
            logCRITICAL(LOGSRC,"Unable to start Protocol thread");
        }
    } else
    if (udp) {
        /* UDP read loop */
        fprintf(stderr, "UDP support not yet implemented.\n");
        return 1;
    } else {
        /* not recognized */
        fprintf(stderr, "TCP support not specified.\n");
        _usage(argv[0], 1);
    }

    /* wait here */
    while (utTrue) {
        threadSleepMS(60000L);
    }
    return 0;

}

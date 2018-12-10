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
//  Main entry point for sample serial port DMTP server.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/04/11  Martin D. Flynn
//     -Force POSIX locale on startup.
//  2007/01/28  Martin D. Flynn
//     -Added support for sending commands to the client via keyboard entry.
// ----------------------------------------------------------------------------

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/io.h"
#include "tools/threads.h"

#include "base/props.h"     // included for property definitions
#include "base/statcode.h"  // included for status code definitions
#include "base/cmderrs.h"   // included for command error code definitions

#include "server/defaults.h"
#include "server/server.h"
#include "server/packet.h"
#include "server/events.h"
#include "server/protocol.h"
#include "server/log.h"
#include "server/upload.h"
#include "server/cerrors.h"

#include "server/geozone.h"

// ----------------------------------------------------------------------------

#define VERSION                     "1.1.3"

// ----------------------------------------------------------------------------

// keyboard definitions (copied from 'comport.h')
#define KEY_CONTROL_H               '\b'    // ASCII backspace
#define KEY_DELETE                  0x7F    // character delete (keyboard "Backspace")
#define KEY_IsBackspace(K)          (((K) == KEY_CONTROL_H) || ((K) == KEY_DELETE))

// ----------------------------------------------------------------------------
// Place custom event packet definitions here

// ----------------------------------------------------------------------------

static char comPortID[32];
static utBool comPortLog = utFalse;

/* thread function */
static void _protocolThreadRunnable(void *arg)
{
    protocolLoop(
        comPortID, comPortLog,
        utTrue/*KEEP_ALIVE*/, 
        utFalse/*CLIENT_SPEAKS_FIRST*/);
}

// ----------------------------------------------------------------------------

/* display property value received from client */
static void mainHandleProperty(UInt16 propKey, const UInt8 *propData, UInt16 propDataLen)
{
    logINFO(LOGSRC,"Received client property %04X [payload len=%u]", propKey, propDataLen);
    Buffer_t bb, *bf = binBuffer(&bb, (UInt8*)propData, propDataLen, BUFFER_SOURCE);
    switch (propKey) {
        
        case PROP_GEOF_VERSION: {
            // 'propDatalen' may include the terminating '0'
            logINFO(LOGSRC,"Geozone version: %s", propData);
            break;
        }
        
        case PROP_GEOF_COUNT: {
            // geozone size
            UInt32 size = 0L;
            binBufScanf(bf, "%2u", &size);
            logINFO(LOGSRC,"Geozone size: %lu", size);
            break;
        }
        
        case PROP_STATE_GPS_DIAGNOSTIC: {
            // print GPS diagnostics
            // Example packet: 0xE0B0 16 F124 00000000 00000000 00000000 00000000 00000001
            GPSDiagnostics_t gpsDiag = { 0L, 0L, 0L, 0L, 0L };
            binBufScanf(bf, "%4u", &(gpsDiag.lastSampleTime));
            binBufScanf(bf, "%4u", &(gpsDiag.lastValidTime));
            binBufScanf(bf, "%4u", &(gpsDiag.sampleCount_A));
            binBufScanf(bf, "%4u", &(gpsDiag.sampleCount_V));
            binBufScanf(bf, "%4u", &(gpsDiag.restartCount));
            logINFO(LOGSRC,"GPS Diagnostics:");
            logINFO(LOGSRC,"  Last Sample Time :  %lu", gpsDiag.lastSampleTime);
            logINFO(LOGSRC,"  Last Valid Time  :  %lu", gpsDiag.lastValidTime);
            logINFO(LOGSRC,"  'A' Sample Count :  %lu", gpsDiag.sampleCount_A);
            logINFO(LOGSRC,"  'V' Sample Count :  %lu", gpsDiag.sampleCount_V);
            logINFO(LOGSRC,"  Restart Count    :  %lu", gpsDiag.restartCount);
            break;
        }

        case PROP_STATE_QUEUED_EVENTS: {
            UInt32 count = 0L, total = 0L;
            binBufScanf(bf, "%4u%4u", &count, &total);
            logINFO(LOGSRC,"Pending event count: %lu/%lu", count, total);
            break;
        }
        
        default: {
            UInt8 hex[600];
            strEncodeHex(hex, sizeof(hex), propData, propDataLen);
            logINFO(LOGSRC,"Property 0x%04X: %s", propKey, hex);
            break;
        }
        
    }
}

// ----------------------------------------------------------------------------

/* handle diagnostic received from client */
static void mainHandleDiag(UInt16 diagKey, const UInt8 *diagData, UInt16 diagDataLen)
{
    UInt8 hex[800];
    strEncodeHex(hex, sizeof(hex), diagData, diagDataLen);
    logINFO(LOGSRC,"Diagnostic 0x%04X: %s", diagKey, hex);
}

// ----------------------------------------------------------------------------

/* handle error received from client */
static void mainHandleError(UInt16 errKey, const UInt8 *errData, UInt16 errDataLen)
{
    Buffer_t bb, *bf = binBuffer(&bb, (UInt8*)errData, errDataLen, BUFFER_SOURCE);
    switch (errKey) {
        case ERROR_GPS_EXPIRED:
        case ERROR_GPS_FAILURE: {
            // request GPS diagnostics
            protGetPropValue(PROP_STATE_GPS_DIAGNOSTIC);
            protSetNeedsMoreInfo();
        } break;
        case ERROR_PROPERTY_WRITE_ONLY: {
            // attempt to read from a write-only property
            UInt32 propId = 0L;
            binBufScanf(bf, "%2x", &propId);
            logINFO(LOGSRC,"Client write-only property error: prop=0x%04lX", propId);
        } break;
        case ERROR_PROPERTY_READ_ONLY: {
            // attempt to write to a read-only property
            UInt32 propId = 0L;
            binBufScanf(bf, "%2x", &propId);
            logINFO(LOGSRC,"Client read-only property error: prop=0x%04lX", propId);
        } break;
        case ERROR_COMMAND_INVALID: {
            // command was invalid
            UInt32 cmdId  = 0L;
            binBufScanf(bf, "%2x", &cmdId);
            logINFO(LOGSRC,"Client invalid-command error: cmd=0x%04lX", cmdId);
        } break;
        case ERROR_COMMAND_ERROR: {
            // command had an error
            UInt32 cmdId = 0L, cmdErr = 0L;
            binBufScanf(bf, "%2x%2x", &cmdId, &cmdErr);
            if ((cmdErr == COMMAND_OK) || (cmdErr == COMMAND_OK_ACK)) {
                logINFO(LOGSRC,"Client command ACK: cmd=0x%04lX", cmdId);
            } else {
                logINFO(LOGSRC,"Client command error: cmd=0x%04lX err=0x%04lX", cmdId, cmdErr);
            }
        } break;
        default: {
            logINFO(LOGSRC,"Client error %04X", errKey);
        } break;
    }
}

// ----------------------------------------------------------------------------

/* client initialization on connection */
static void mainHandleClientInit()
{
    // send client initialization packets here
}

// ----------------------------------------------------------------------------

static char savePacketFile[80] = "./scomserv.dmt";
static utBool saveAsCSV = utFalse;

static char scName[32];
static const char *statusCodeName(UInt16 code)
{
    // This currently includes only the most common status code names.
    // Status code names should be added as they become needed.
    switch (code) {
        case STATUS_INITIALIZED:            return "Initialized";
        case STATUS_LOCATION:               return "Location";
        case STATUS_WAYMARK:                return "Waymark";
        case STATUS_QUERY:                  return "Query";         // "Ping"
        case STATUS_MOTION_START:           return "StartMotion";
        case STATUS_MOTION_IN_MOTION:       return "InMotion";
        case STATUS_MOTION_STOP:            return "StopMotion";
        case STATUS_MOTION_DORMANT:         return "Dormant";
        case STATUS_MOTION_EXCESS_SPEED:    return "Speeding";
        case STATUS_MOTION_MOVING:          return "Moving";
        case STATUS_GEOFENCE_ARRIVE:        return "Arrival";
        case STATUS_GEOFENCE_DEPART:        return "Departure";
        case STATUS_GEOFENCE_VIOLATION:     return "GFViolation";
        case STATUS_GEOFENCE_ACTIVE:        return "GFActive";
        case STATUS_GEOFENCE_INACTIVE:      return "GFInactive";
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
    
    /* create CSV formatted record */
    UInt8 csv[256], *c = csv;
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
    sprintf(c, ",%.1lf"         , ev->topSpeedKPH);           c += strlen(c);
    sprintf(c, ",%s"            , ev->entity[1]);             c += strlen(c);
    sprintf(c, ",%s"            , ev->entity[0]);             c += strlen(c);

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

/* submit packet based on command arguments */
static Packet_t  cmdPacket;
static void execCommand(const char *cmd, char *cmdFld[])
{
    // add new commands as necessary
    
    /* enpty command specification */
    if (!cmd || !*cmd) {
        // ignore
        return;
    }
    
    /* help */
    if (strEquals(cmd, "help")) {
        logINFO(LOGSRC,"Commands:");
        logINFO(LOGSRC,"  device <id>              - set device id");
        logINFO(LOGSRC,"  loc                      - request STATUS_LOCATION");
        logINFO(LOGSRC,"  ping                     - request STATUS_QUERY");
        logINFO(LOGSRC,"  set8 <prop> <value>      - set 8-bit property value");
        logINFO(LOGSRC,"  set16 <prop> <value>     - set 16-bit property value");
        logINFO(LOGSRC,"  set32 <prop> <value>     - set 32-bit property value");
        logINFO(LOGSRC,"  get <prop>               - get property value");
        logINFO(LOGSRC,"  sf {0|1}                 - Set client 'speakFreely' mode");
        logINFO(LOGSRC,"  rq [<count>]             - Send client a request to speak");
        logINFO(LOGSRC,"  save                     - save properties");
        logINFO(LOGSRC,"  gps                      - request gps diagnostics");
        logINFO(LOGSRC,"  reboot                   - reboot client");
#if defined(INCLUDE_UPLOAD)
        logINFO(LOGSRC,"  upload <file> [<id>]     - upload file to client");
#endif
        return;
    }
    
    /* ping status location */
    if (strEquals(cmd, "loc")) {
        // ping
        logINFO(LOGSRC,"Location ...");
        pktInit(&cmdPacket,PKT_SERVER_SET_PROPERTY,"%2x%2x",(UInt32)PROP_CMD_STATUS_EVENT,(UInt32)STATUS_LOCATION);
        protAddPendingPacket(&cmdPacket);
        return;
    }
    
    /* ping status location */
    if (strEquals(cmd, "ping") || strEquals(cmd, "query")) {
        // ping
        logINFO(LOGSRC,"Ping ...");
        pktInit(&cmdPacket,PKT_SERVER_SET_PROPERTY,"%2x%2x",(UInt32)PROP_CMD_STATUS_EVENT,(UInt32)STATUS_QUERY);
        protAddPendingPacket(&cmdPacket);
        return;
    }

    /* set device id */
    if (strEquals(cmd, "device") || strEquals(cmd, "dev")) {
        // device <id>
        logINFO(LOGSRC,"Set DeviceID (if writable) ...");
        char *dev = cmdFld[1]? cmdFld[1] : "";
        if (!*dev) {
            logERROR(LOGSRC,"Device ID must be specified");
        } else {
            pktInit(&cmdPacket,PKT_SERVER_SET_PROPERTY,"%2x%*s",(UInt32)PROP_STATE_DEVICE_ID,(int)MAX_ID_SIZE,dev);
            protAddPendingPacket(&cmdPacket);
        }
        return;
    }

    /* get/set/save property values */
    if (strEquals(cmd, "save")) {
        // save
        logINFO(LOGSRC,"Save properties ...");
        pktInit(&cmdPacket, PKT_SERVER_SET_PROPERTY, "%2x", (UInt32)PROP_CMD_SAVE_PROPS);
        protAddPendingPacket(&cmdPacket);
        return;
    }
    if (strEquals(cmd, "get")) {
        // get <prop>
        logINFO(LOGSRC,"Get property ...");
        char *propID = cmdFld[1]? cmdFld[1] : "";
        if (!*propID) {
            logERROR(LOGSRC,"Property ID must be specified");
        } else {
            UInt32 prop = strParseHex32(propID, 0L);
            pktInit(&cmdPacket, PKT_SERVER_GET_PROPERTY, "%2x", prop);
            protAddPendingPacket(&cmdPacket);
        }
        return;
    }
    if (strEquals(cmd, "set8")) {
        // set8 <prop> <val>
        logINFO(LOGSRC,"Set 8-bit property ...");
        char *propID = cmdFld[1]? cmdFld[1] : "";
        char *value  = cmdFld[2]? cmdFld[2] : "";
        if (!*propID) {
            logERROR(LOGSRC,"Property ID must be specified");
        } else
        if (!*value) {
            logERROR(LOGSRC,"Value must be specified");
        } else {
            UInt32 p = strParseHex32(propID, 0L);
            UInt32 v = strParseUInt32(value, 0L);
            pktInit(&cmdPacket, PKT_SERVER_SET_PROPERTY, "%2x%1x", p, v);
            protAddPendingPacket(&cmdPacket);
        }
        return;
    }
    if (strEquals(cmd, "set16")) {
        // set16 <prop> <val>
        logINFO(LOGSRC,"Set 16-bit property ...");
        char *propID = cmdFld[1]? cmdFld[1] : "";
        char *value  = cmdFld[2]? cmdFld[2] : "";
        if (!*propID) {
            logERROR(LOGSRC,"Property ID must be specified");
        } else
        if (!*value) {
            logERROR(LOGSRC,"Value must be specified");
        } else {
            UInt32 p = strParseHex32(propID, 0L);
            UInt32 v = strParseUInt32(value, 0L);
            pktInit(&cmdPacket, PKT_SERVER_SET_PROPERTY, "%2x%2x", p, v);
            protAddPendingPacket(&cmdPacket);
        }
        return;
    }
    if (strEquals(cmd, "set32")) {
        // set32 <prop> <val>
        logINFO(LOGSRC,"Set 32-bit property ...");
        char *propID = cmdFld[1]? cmdFld[1] : "";
        char *value  = cmdFld[2]? cmdFld[2] : "";
        if (!*propID) {
            logERROR(LOGSRC,"Property ID must be specified");
        } else
        if (!*value) {
            logERROR(LOGSRC,"Value must be specified");
        } else {
            UInt32 p = strParseHex32(propID, 0L);
            UInt32 v = strParseUInt32(value, 0L);
            pktInit(&cmdPacket, PKT_SERVER_SET_PROPERTY, "%2x%4x", p, v);
            protAddPendingPacket(&cmdPacket);
        }
        return;
    }
    
    /* set 'speakFreely' mode */
    if (strEquals(cmd, "sf")) {
        // sf <maxEvents>
        char *mode = cmdFld[1]? cmdFld[1] : "";
        if (!*mode) {
            protSetSpeakFreelyMode(utTrue,-1);
        } else {
            int maxEvents = (int)strParseInt32(mode, -1L);
            utBool sfMode = (maxEvents >= 0)? utTrue : utFalse;
            protSetSpeakFreelyMode(sfMode, maxEvents);
        }
        pktInit(&cmdPacket, PKT_SERVER_EOB_DONE, "%1x", (UInt32)0L);
        protAddPendingPacket(&cmdPacket);
        return;
    }
        
    /* request to speak */
    if (strEquals(cmd, "rq")) {
        // rq [<count>]
        char *cntStr = cmdFld[1]? cmdFld[1] : "0";
        UInt32 cnt = strParseUInt32(cntStr, 0L);
        pktInit(&cmdPacket, PKT_SERVER_EOB_DONE, "%1x", cnt);
        protAddPendingPacket(&cmdPacket);
        return;
    }

    /* get GPS diagnostics */
    if (strEquals(cmd, "gps")) {
        // gps
        logINFO(LOGSRC,"Get GPS Diagnostics ...");
        UInt32 prop = (UInt32)PROP_STATE_GPS_DIAGNOSTIC;
        pktInit(&cmdPacket, PKT_SERVER_GET_PROPERTY, "%2x", prop);
        protAddPendingPacket(&cmdPacket);
        return;
    }
    
    /* reboot the client (if supported by the client) */
    if (strEquals(cmd, "reboot")) {
        // reboot
        logINFO(LOGSRC,"Reboot client ...");
        UInt32 key  = (UInt32)PROP_CMD_RESET;
        UInt32 type = 0L; // cold-reset
        UInt8  auth[] = { 'n', 'o', 'w' };
        int authLen = 3;
        pktInit(&cmdPacket, PKT_SERVER_SET_PROPERTY, "%2x%1x%*b", key, type, authLen, auth);
        protAddPendingPacket(&cmdPacket);
        return;
    }

    /* upload file to client */
#if defined(INCLUDE_UPLOAD)
    if (strEquals(cmd, "upload")) {
        // upload <localFile> [<clientFile>]
        char *fn = cmdFld[1]? cmdFld[1] : "";       // local file
        char *cf = cmdFld[2]? cmdFld[2] : (char*)0; // client file
        if (!ioIsFile(fn)) {
            logERROR(LOGSRC,"Upload file does not exist: %s", fn);
        } else {
            // if 'cf' is null, then 'fn' is expected to be a Base64 encoded file
            protSetPendingFileUpload(fn, cf);
        }
        return;
    }
#endif

    /* unrecognized command */
    logERROR(LOGSRC,"Unrecognized command: %s", cmd);

}

// ----------------------------------------------------------------------------

/* read a line of text from stdin */
// Once the first character is hit, logging is suspended until the entire line is 
// entered.  To prevent logging from being suspended indefinitely, if no character 
// is pressed within a 15 second window, the input buffer will be reset and logging 
// will be resumed.  [Note: The timer is reset after each pressed character.]
static char *_readLine(char *cmdLine, int cmdLineLen)
{
    UInt32 timeoutMS = 15000L; // 15 seconds
    int bp = 0;
    while (utTrue) {

        /* wait for available character */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        struct timeval tv;
        tv.tv_sec  = timeoutMS / 1000L;
        tv.tv_usec = (timeoutMS % 1000L) * 1000L;
        select(STDIN_FILENO + 1, &rfds, 0, 0, &tv);
        if (!FD_ISSET(STDIN_FILENO, &rfds)) {
            // timeout (don't let the logging be suspended for too long)
            if (bp > 0) {
                fprintf(stdout, "     <timeout - input reset>\n\n");
                bp = 0;
                logSetSuspend(utFalse);
            }
            continue; 
        }

        /* get a single character */
        int ch = fgetc(stdin);
        if (ch < 0) { 
            return (char*)0; 
        }

        /* end of line? */
        if ((ch == '\r') || (ch == '\n')) {
            if (bp > 0) { // <-- only if something is in the buffer
                fprintf(stdout, "\n\n");   // ignore returned error codes
                ioFlushStream(stdout);
                break;
            }
            continue;
        }

        /* backspace? */
        if (KEY_IsBackspace(ch)) {
            if (bp > 0) { // <-- only if something is in the buffer
                fprintf(stdout, "\b \b");   // ignore returned error codes
                ioFlushStream(stdout);
                bp--;
                if (bp == 0) {
                    // we've backspaced over first character, resume logging
                    logSetSuspend(utFalse); 
                }
            }
            continue;
        }

        /* save input character */
        if (bp < cmdLineLen - 1) {
            if ((ch != ' ') || (bp > 0)) { // ignore spaces at start of command line
                //fprintf(stdout, "<ch:0x%02X>", ch); 
                //ioFlushStream(stdout);
                if (ch >= ' ') { // ignore non-printable ASCII characters
                    if (bp == 0) {
                        // suspend logging on first character entered
                        logSetSuspend(utTrue);
                    }
                    fprintf(stdout, "%c", ch);
                    ioFlushStream(stdout);
                    cmdLine[bp++] = ch; 
                }
            }
            continue;
        }

        /* overflow */
        fprintf(stdout, "<overflow>\n");   // ignore returned error codes
        ioFlushStream(stdout);

    }
    
    /* terminate line */
    cmdLine[bp] = 0; // terminate
    
    /* resume logging */
    logSetSuspend(utFalse);
    
    /* trim and return */
    return strTrim(cmdLine);
}

// ----------------------------------------------------------------------------
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
    fprintf(stdout, "   %s ...\n", pgm);
    fprintf(stdout, "    [-comlog]          - Enable commPort data logging\n");
    fprintf(stdout, "    [-com <port>]      - Server serial port\n");
    fprintf(stdout, "\n");
    exit(exitCode);
}

// main entry point
int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "POSIX");
    setDebugMode(utTrue);

    /* init */
    threadMutexInit(&protoMutex);
    memset(comPortID, 0, sizeof(comPortID));
    /* header */
    fprintf(stdout, "Sample serial port server [Version %s]\n", VERSION);

    /* command line arguments */
    int i;
    utBool comLog = utFalse;
    for (i = 1; i < argc; i++) {
        if (strEquals(argv[i], "-help") || strEquals(argv[i], "-h")) {
            // -h[elp]
            _usage(argv[0], 0);
        } else
        if (strEquals(argv[i], "-comlog")) {
            // -comlog
            comLog = utTrue;
        } else
        if (strEquals(argv[i], "-com")) {
            // -com <port>
            i++;
            if ((i < argc) && (*argv[i] != '-')) {
                strncpy(comPortID, argv[i], sizeof(comPortID) - 1);
                comPortLog = comLog;
            } else {
                fprintf(stderr, "Missing serial port ...\n");
                _usage(argv[0], 1);
            }
        } else
        {
            fprintf(stderr, "Invalid option: %s\n", argv[i]);
            _usage(argv[0], 1);
        } 
    }

    /* supplied com port? */
    if (!*comPortID) {
        fprintf(stderr, "Please supply a comport\n");
        _usage(argv[0], 1);
    }

    /* thread initializer */
    // this must be called before threads are created
    threadInitialize();

    /* custom event packet format */
    // (add custom event packet definitions here)
    /* protocol handlers */
    protocolSetEventHandler(&mainHandleEvent);
    protocolSetClientInitHandler(&mainHandleClientInit);
    protocolSetPropertyHandler(&mainHandleProperty);
    protocolSetDiagHandler(&mainHandleDiag);
    protocolSetErrorHandler(&mainHandleError);

    /* protocol thread */
    if (threadCreate(&protoThread,&_protocolThreadRunnable,0,"SComServe") == 0) {
        // thread started successfully
    } else {
        logCRITICAL(LOGSRC,"Unable to start Protocol thread");
    }

    /* reset stdin (remove buffering) */
    ioResetSTDIN();

    /* loop here */
    char cmdLine[512];
    while (utTrue) {

        /* get command line */
        char *cmdTrim = _readLine(cmdLine, sizeof(cmdLine));
        if (!cmdTrim) {
            printf("Read error in stdin\n");
            exit(1);
        }
        
        /* parse command */
        char *cmdFld[32];
        memset(cmdFld, 0, sizeof(cmdFld));
        strParseArray_sep(cmdTrim, cmdFld, 20, ' ');
        char *cmd = cmdFld[0];

        /* check for exit/quit */
        if (strEquals(cmd,"exit") || strEquals(cmd,"quit")) {
            break;
        }

        /* parse and execute command */
        execCommand(cmd, cmdFld);

    }
    return 0;

}

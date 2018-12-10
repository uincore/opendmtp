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
//  Parse and display DMTP packets from a file.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/23  Martin D. Flynn
//     -Change CSV output date/time format for easier integration into mapping services.
//     -Added output XML format for Google Maps.
//  2006/02/05  Martin D. Flynn
//     -Moved date/time to beginning of CSV record.
//  2006/02/18  Martin D. Flynn
//     -Added option to output data in GPX format.
//     -Fixed CSV header (moved 'code' heading after 'time')
//  2006/04/11  Martin D. Flynn
//     -Force POSIX locale on startup.
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#define SKIP_TRANSPORT_MEDIA_CHECK // only if TRANSPORT_MEDIA not used in this file 
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <locale.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"

#include "base/packet.h"
#include "base/statcode.h"

#include "events.h"
#include "log.h"
#include "parsfile.h"

// ----------------------------------------------------------------------------

#define FORMAT_CSV          1
#define FORMAT_GPX          2
#define FORMAT_GOOGLE_XML   3

#define MODE_HEADER         0
#define MODE_DATA           1
#define MODE_FOOTER         2

// ----------------------------------------------------------------------------

static char scName[32];
static const char *statusCodeName(UInt16 code)
{
    switch (code) {
        case STATUS_MOTION_START:           return "StartMotion";
        case STATUS_MOTION_IN_MOTION:       return "InMotion";
        case STATUS_MOTION_STOP:            return "StopMotion";
        case STATUS_MOTION_DORMANT:         return "Dormant";
        case STATUS_MOTION_EXCESS_SPEED:    return "Speeding";
        case STATUS_GEOFENCE_ARRIVE:        return "Arrival";
        case STATUS_GEOFENCE_DEPART:        return "Departure";
    }
    sprintf(scName, "0x%04X", code);
    return scName;
}

// ----------------------------------------------------------------------------

static void printCSV(int mode, Event_t *er)
{
    
    /* header/footer */
    if (mode == MODE_HEADER) {
        char hdr[600], *h = hdr;
        sprintf(h,  "date");      h += strlen(h);
        sprintf(h, ",time");      h += strlen(h);
        sprintf(h, ",code");      h += strlen(h);
        sprintf(h, ",latitude");  h += strlen(h);
        sprintf(h, ",longitude"); h += strlen(h);
        sprintf(h, ",speed");     h += strlen(h);
        sprintf(h, ",heading");   h += strlen(h);
      //sprintf(h, ",alt");       h += strlen(h);
      //sprintf(h, ",seq");       h += strlen(h);
        fprintf(stdout, "%s\n", hdr);
        return;
    } else 
    if (mode == MODE_FOOTER) {
        return;
    } else 
    if ((mode == MODE_DATA) && er) {
        struct tm *tmp = localtime(&er->timestamp[0]);
        int hr  = tmp->tm_hour, mn = tmp->tm_min, sc = tmp->tm_sec;
        int dy  = tmp->tm_mday, mo = tmp->tm_mon + 1, yr = 1900 + tmp->tm_year;
        const char *codeName = statusCodeName(er->statusCode);
        char cvs[600], *c = cvs;
        sprintf(c,  "%02d/%02d/%02d", yr, mo, dy);        c += strlen(c);
        sprintf(c, ",%02d:%02d:%02d", hr, mn, sc);        c += strlen(c);
        sprintf(c, ",%s"    , codeName);                  c += strlen(c);
        sprintf(c, ",%.5lf" , er->gpsPoint[0].latitude);  c += strlen(c);
        sprintf(c, ",%.5lf" , er->gpsPoint[0].longitude); c += strlen(c);
        sprintf(c, ",%.1lf" , er->speedKPH);              c += strlen(c);
        sprintf(c, ",%.1lf" , er->heading);               c += strlen(c);
      //sprintf(c, ",%.0lf" , er->altitude);              c += strlen(c);
      //sprintf(c, ",%04lX" , er->sequence);              c += strlen(c);
        fprintf(stdout, "%s\n", cvs);
        return;
    }
    
}

// ----------------------------------------------------------------------------

static void printGPX(int mode, Event_t *er)
{
    // Docs:
    //   http://www.topografix.com/gpx_manual.asp
    //   http://bikewashington.org/routes/shenandoah/route.gpx
    // Validate:
    //   SaxCount.exe -v=always -n -s -f my_gpx_file.gpx
    
    /* header/footer */
    if (mode == MODE_HEADER) { // header
        UInt32 nowTime = utcGetTimeSec();
        struct tm *tmp = gmtime((time_t*)&nowTime);
        int hr  = tmp->tm_hour, mn = tmp->tm_min, sc = tmp->tm_sec;
        int dy  = tmp->tm_mday, mo = tmp->tm_mon + 1, yr = 1900 + tmp->tm_year;
        fprintf(stdout, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\" standalone=\"yes\"?>\n");
        fprintf(stdout, "<gpx\n");
        fprintf(stdout, "  version=\"1.0\"\n");
        fprintf(stdout, "  creator=\"ParseFile - http://www.opendmtp.org\"\n");
        fprintf(stdout, "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
        fprintf(stdout, "  xmlns=\"http://www.topografix.com/GPX/1/0\"\n");
        fprintf(stdout, "  xsi:schemaLocation=\"http://www.topografix.com/GPX/1/0 http://www.topografix.com/GPX/1/0/gpx.xsd\">\n");
        fprintf(stdout, " <time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>\n", yr, mo, dy, hr, mn, sc); // UTC ISO 8601
        fprintf(stdout, " <trk>\n");
        fprintf(stdout, " <trkseg>\n");
        return;
    } else 
    if (mode == MODE_FOOTER) { // footer
        fprintf(stdout, " </trkseg>\n");
        fprintf(stdout, " </trk>\n");
        fprintf(stdout, "</gpx>\n");
        return;
    } else
    if ((mode == MODE_DATA) && er) { // data point
        struct tm *tmp = gmtime((time_t*)&er->timestamp[0]);
        int hr  = tmp->tm_hour, mn = tmp->tm_min, sc = tmp->tm_sec;
        int dy  = tmp->tm_mday, mo = tmp->tm_mon + 1, yr = 1900 + tmp->tm_year;
        double lat = er->gpsPoint[0].latitude;
        double lon = er->gpsPoint[0].longitude;
        fprintf(stdout, "  <trkpt lat=\"%.6lf\" lon=\"%.6lf\">\n", lat, lon);
        if (evIsFieldSet(er, FIELD_ALTITUDE)) {
            fprintf(stdout, "    <ele>%.1lf</ele>\n", er->altitude);
        }
        fprintf(stdout, "    <time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>\n", yr, mo, dy, hr, mn, sc); // ISO 8601
        if (evIsFieldSet(er, FIELD_HEADING)) {
            // only available in <trkpt>
            fprintf(stdout, "    <course>%.1lf</course>\n", er->heading); // degrees
        }
        if (evIsFieldSet(er, FIELD_SPEED)) {
            // only available in <trkpt>
            fprintf(stdout, "    <speed>%.1lf</speed>\n", (double)((er->speedKPH * 1000.0) / 3600.0)); // (m/s)
        }
        if (evIsFieldSet(er, FIELD_GPS_MAG_VARIATION)) {
            fprintf(stdout, "    <magvar>%.1lf</magvar>\n", er->gpsMagVariation); // degrees
        }
        if (evIsFieldSet(er, FIELD_GPS_GEOID_HEIGHT)) {
            fprintf(stdout, "    <geoidheight>%.1lf</geoidheight>\n", er->gpsGeoidHeight); // meters
        }
        fprintf(stdout, "    <sym>%s</sym>\n", statusCodeName(er->statusCode));
        if (evIsFieldSet(er, FIELD_GPS_QUALITY) || evIsFieldSet(er, FIELD_GPS_TYPE)) {
            if (er->gpsQuality == 1) {
                char *s = (er->gps2D3D == 2)? "2d" : ((er->gps2D3D == 3)?"3d" : "none");
                fprintf(stdout, "    <fix>%s</fix>\n", s); // 'none'|'2d'|'3d'
            } else {
                char *s = (er->gpsQuality == 2)? "dgps" : ((er->gpsQuality == 3)? "pps" : "none");
                fprintf(stdout, "    <fix>%s</fix>\n", s); // 'none'|'dgps'|'pps'
            }
        }
        if (evIsFieldSet(er, FIELD_GPS_SATELLITES)) {
            fprintf(stdout, "    <sat>%lu</sat>\n", er->gpsSatellites); // count
        }
        if (evIsFieldSet(er, FIELD_GPS_HDOP)) {
            fprintf(stdout, "    <hdop>%.1lf</hdop>\n", er->gpsHDOP); // dec
        }
        if (evIsFieldSet(er, FIELD_GPS_VDOP)) {
            fprintf(stdout, "    <vdop>%.1lf</vdop>\n", er->gpsVDOP); // dec
        }
        if (evIsFieldSet(er, FIELD_GPS_PDOP)) {
            fprintf(stdout, "    <pdop>%.1lf</pdop>\n", er->gpsPDOP); // dec
        }
        if (evIsFieldSet(er, FIELD_GPS_DGPS_UPDATE)) {
            fprintf(stdout, "    <ageofdgpsdata>%lu</ageofdgpsdata>\n", er->gpsDgpsUpdate); // sec
        }
        // <dgpsid> dgpsStationType </dgpsid>       // n/a
        fprintf(stdout, "  </trkpt>\n");
        return;
    }
    
}

// ----------------------------------------------------------------------------

static void printGoogleXML(int mode, Event_t *er)
{
    
    /* header/footer */
    if (mode == MODE_HEADER) {
        fprintf(stdout, "<markers>\n");
        return;
    } else 
    if (mode == MODE_FOOTER) {
        fprintf(stdout, "</markers>\n");
        return;
    } else
    if ((mode == MODE_DATA) && er) {
        struct tm *tmp = localtime(&er->timestamp[0]);
        int hr  = tmp->tm_hour, mn = tmp->tm_min, sc = tmp->tm_sec;
        int dy  = tmp->tm_mday, mo = tmp->tm_mon + 1, yr = 1900 + tmp->tm_year;
        char xml[600], *c = xml;
        sprintf(c, "  <marker"); c += strlen(c);
        sprintf(c, " name=\"%s\""            , statusCodeName(er->statusCode)); c += strlen(c);
        sprintf(c, " lat=\"%.5lf\""          , er->gpsPoint[0].latitude); c += strlen(c);
        sprintf(c, " lon=\"%.5lf\""          , er->gpsPoint[0].longitude); c += strlen(c);
        sprintf(c, " kph=\"%.1lf\""          , er->speedKPH); c += strlen(c);
        sprintf(c, " head=\"%.1lf\""         , er->heading); c += strlen(c);
      //sprintf(c, " alt=\"%.0lf\""          , er->altitude); c += strlen(c);
        sprintf(c, " utc=\"%ld\""            , er->timestamp[0]); c += strlen(c);
        sprintf(c, " date=\"%02d/%02d/%02d\"", yr, mo, dy); c += strlen(c);
        sprintf(c, " time=\"%02d:%02d:%02d\"", hr, mn, sc); c += strlen(c);
      //sprintf(c, " seq=\"%04lX\"" , er->sequence); c += strlen(c);
        sprintf(c, "/>"); c += strlen(c);
        fprintf(stdout, "%s\n", xml);
        return;
    }
    
}

// ----------------------------------------------------------------------------

static void _usage(const char *pgm, int exitCode)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "  %s [options]\n", pgm);
    fprintf(stderr, "    [-help]        - display this help and exit\n");
    fprintf(stderr, "    [-file <file>] - parse packets from specified file\n");
    fprintf(stderr, "    [-csv]         - output points in CSV format (default)\n");
    fprintf(stderr, "    [-gpx]         - output points in GPX format\n");
    fprintf(stderr, "    [-google]      - output points in XML format for Google Maps\n");
    fprintf(stderr, "\n");
    exit(exitCode);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Main entry point

// main entry point
int main(int argc, char *argv[])
{
    const char *fileName = (char*)0;
    int printFormat = FORMAT_CSV;
    setlocale(LC_ALL, "POSIX");
    setDebugMode(utTrue);

    /* command line arguments */
    int i;
    for (i = 1; i < argc; i++) {
        if (strEquals(argv[i], "-help") || strEquals(argv[i], "-h")) {
            // -h[elp]
            _usage(argv[0], 0);
        } else
        if (strEquals(argv[i], "-file")) {
            // -file <file>
            i++;
            if ((i < argc) && (*argv[i] != '-')) {
                fileName = argv[i];
            } else {
                fprintf(stderr, "Missing file name ...\n");
                _usage(argv[0], 1);
            }
        } else
        if (strEquals(argv[i], "-csv")) {
            printFormat = FORMAT_CSV;
        } else
        if (strEquals(argv[i], "-gpx")) {
            printFormat = FORMAT_GPX;
        } else
        if (strEquals(argv[i], "-google")) {
            printFormat = FORMAT_GOOGLE_XML;
        } else {
            fprintf(stderr, "Invalid option: %s\n", argv[i]);
            _usage(argv[0], 1);
        } 
    }
    
    /* file specified? */
    if (!fileName || !*fileName) {
        _usage(argv[0], 1);
    }
    
    /* open file */
    if (!parseOpen(fileName)) {
        exit(1);
    }
    
    /* header */
    switch (printFormat) {
        case FORMAT_CSV:
            printCSV(MODE_HEADER, (Event_t*)0);
            break;
        case FORMAT_GPX:
            printGPX(MODE_HEADER, (Event_t*)0);
            break;
        case FORMAT_GOOGLE_XML:
            printGoogleXML(MODE_HEADER, (Event_t*)0);
            break;
    } 

    /* read packets */
    Packet_t _packet, *pkt = &_packet;
    while (utTrue) {
        int err = parseReadPacket(pkt);
        if (err != SRVERR_OK) { break; }
        Event_t er;
        if (evParseEventPacket(pkt, &er)) {

            /* data record */
            switch (printFormat) {
                case FORMAT_CSV:
                    printCSV(MODE_DATA, &er);
                    break;
                case FORMAT_GPX:
                    printGPX(MODE_DATA, &er);
                    break;
                case FORMAT_GOOGLE_XML:
                    printGoogleXML(MODE_DATA, &er);
                    break;
            } 
            
        } else {
            
            //fprintf(stderr, "Skipping non-Event [%04X] ...\n", pkt->hdrType);
            
        }
    }
    
    /* footer */
    switch (printFormat) {
        case FORMAT_CSV:
            printCSV(MODE_FOOTER, (Event_t*)0);
            break;
        case FORMAT_GPX:
            printGPX(MODE_FOOTER, (Event_t*)0);
            break;
        case FORMAT_GOOGLE_XML:
            printGoogleXML(MODE_FOOTER, (Event_t*)0);
            break;
    } 
    
    /* end of output */
    return 0;
    
}

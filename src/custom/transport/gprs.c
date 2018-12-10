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
//  GPRS data transport.
// Notes:
//  - This implementation supports UDP/TCP communications with a DMTP server
//  and was designed to work with the BlueTree M2M Express wireless GPRS modem
//  (which is really a Wavecom modem internally and thus may work with any 
//  Wavecom modem, but no other modem has been tested).
// ---
// Change History:
//  2006/01/23  Martin D. Flynn
//     -Initial release
//  2007/01/28  Martin D. Flynn
//     -Initial support for Window CE (not yet fully functional).  On most
//      Windows CE platforms this module may not be neccessary as the Windows CE
//      environment may already handle GPRS/CDMA connections, in which case the
//      'socket' transport media may be used.
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

// ----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
//#include <unistd.h>
//#include <fcntl.h>
#include <ctype.h>
//#include <errno.h>
//#include <termios.h>
//#include <sys/select.h>
//#include <time.h>
#include <stdlib.h>
//#include <pthread.h>

#include "custom/log.h"

#include "custom/transport/gprs.h"

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/bintools.h"
#include "tools/utctools.h"
#include "tools/comport.h"
#include "tools/buffer.h"
#include "tools/threads.h"

#include "base/props.h"
#include "base/propman.h"
#include "base/packet.h"

// ----------------------------------------------------------------------------
// Note: This implementation was tested to work with the Following modem(s):
//  - BlueTree M2M Express GPRS (internal WaveCom modem)

// ----------------------------------------------------------------------------

#define DNS_NULL                    "0.0.0.0"
#define MODEM_SPEED_BPS             BPS_115200      // default speed

void blueInitialize();
void blueResetConnection();

const char *blueGetDataHost();
Int16 blueGetDataPort();

const char *blueGetDNS(int ndx);
const char *blueGetApnName();
const char *blueGetApnServer();
const char *blueGetApnUser();
const char *blueGetApnPassword();

utBool blueIsOpen();
utBool blueOpenCom();
void blueCloseCom();
int blueConnect();

utBool blueOpenTCPSocket();
utBool blueIsTCPSocketOpen();
utBool blueCloseSocket();

int blueWrite(const char *buf, int bufLen);
int blueWriteString(const char *msg);
int blueWriteStringFmt(const char *fmt, ...);

int blueRead(char *resp, int respLen, long timeoutMS);
int blueReadString(char *resp, int respLen, long timeoutMS);
utBool blueSendDatagram(char *buf, int bufLen);

utBool bluePing(const char *server);

// ----------------------------------------------------------------------------

/* transport structure */
typedef struct {
    TransportType_t             type;
    utBool                      isOpen;
    // add whatever is needed to provide platform specific transport requirements
} GprsTransport_t;

static GprsTransport_t          gprsXport;

// ----------------------------------------------------------------------------

static TransportFtns_t          gprsXportFtns = { MEDIA_GPRS, "XportGPRS" };

// The size of the datagram buffer is arbitrary, however the amount of 
// data transmitted in a single datagram should not be larger than the MTU
// to avoid possible fragmentation which could result in a higher possibility
// of data loss.
static UInt8                    gprsDatagramData[2000];
static Buffer_t                 gprsDatagramBuffer;

// ----------------------------------------------------------------------------

static const char *gprs_transportTypeName(TransportType_t type)
{
    switch (type) {
        case TRANSPORT_NONE     : return "None";
        case TRANSPORT_SIMPLEX  : return "Simplex";
        case TRANSPORT_DUPLEX   : return "Duplex";
        default                 : return "Unknown";
    }
}

// ----------------------------------------------------------------------------

/* flush input buffer */
static void gprs_transportReadFlush()
{
    // NO-OP
}

/* return true if transport is open */
static utBool gprs_transportIsOpen()
{
    // this may be called by threads other than the protocol thread.
    return gprsXport.isOpen;
}

/* close transport */
static utBool gprs_transportClose(utBool sendUDP)
{
    if (gprsXport.isOpen) {
        logDEBUG(LOGSRC,"%s Transport close ...\n\n", gprs_transportTypeName(gprsXport.type));
        utBool rtn = utTrue;

        /* check for datagram transmission */
        if (sendUDP && (gprsXport.type == TRANSPORT_SIMPLEX)) {
            // transmit queued datagram
            Buffer_t *bf = &gprsDatagramBuffer;
            UInt8 *data = BUFFER_PTR(bf);
            int dataLen = BUFFER_DATA_LENGTH(bf);
            utBool ok = blueSendDatagram((char*)data, dataLen);
            rtn = ok;
        }

        /* transport is closed */
        gprsXport.type   = TRANSPORT_NONE;
        gprsXport.isOpen = utFalse;
        
        /* reset buffers */
        binResetBuffer(&gprsDatagramBuffer);
        
        return rtn;
        
    } else {
        
        /* wasn't open */
        return utFalse;
        
    }
}

/* open transport */
static utBool gprs_transportOpen(TransportType_t type)
{
    
    /* close, if already open */
    if (gprsXport.isOpen) {
        // it shouldn't already be open
        logWARNING(LOGSRC,"Transport seems to still be open!");
        gprs_transportClose(utFalse);
    }

    /* establish ISP connection */
    // we stay here until we can connect (or until timeout)
    UInt32 connectTimoutSec = MINUTE_SECONDS(5);
    TimerSec_t connectErrorTimer = utcGetTimer();
    while (utTrue) {
        int err = blueConnect();
        if (err > 0) {
            // success
            break;
        } else
        if (err < 0) {
            // critical error (unable to open serial port)
            return utFalse;
        } else
        if (utcIsTimerExpired(connectErrorTimer,connectTimoutSec)) {
            return utFalse;
        }
        logWARNING(LOGSRC,"Retrying connect ...");
        threadSleepMS(5000L);
    }
    
    /* open TCP socket */
    if (type == TRANSPORT_DUPLEX) {
        // Duplex
        if (blueOpenTCPSocket()) {
            // socket is ready writing/reading
            gprsXport.type = type;
            gprsXport.isOpen = utTrue;
            // fall through to 'true' return below
        } else {
            // If we can't open the socket, then the connection is suspect.
            // close and reopen connection
            logINFO(LOGSRC,"Unable to open socket");
            blueResetConnection();
            threadSleepMS(1000L);
            return utFalse;
        }
    } else {
        // Simplex
        gprsXport.type = type;
        gprsXport.isOpen = utTrue;
        // fall through to 'true' return below
    }
    
    /* reset buffers and return success */
    binResetBuffer(&gprsDatagramBuffer);
    logDEBUG(LOGSRC,"Openned %s Transport ...\n", gprs_transportTypeName(type));
    return gprsXport.isOpen;
    
}

// ----------------------------------------------------------------------------

static int _transportRead(UInt8 *buf, int bufLen)
{
    
    /* transport not open? */
    if (!gprsXport.isOpen) {
        logERROR(LOGSRC,"Transport is not open");
        return -1;
    }

    /* cannot read when connecting via Simplex */
    if (gprsXport.type == TRANSPORT_SIMPLEX) {
        logERROR(LOGSRC,"Cannot read from Simplex transport");
        return -1;
    }
    
    /* no data was requested */
    if (bufLen == 0) {
        return 0;
    }
    
    /* return bytes read */
    int readLen = 0;
    readLen = blueRead((char*)buf, bufLen, 5000L);
    return readLen;
    
}

/* read packet from transport */
static int gprs_transportReadPacket(UInt8 *buf, int bufLen)
{
    UInt8 *b = buf;
    int readLen, len;
    
    /* check minimum 'bufLen' */
    if (bufLen < PACKET_HEADER_LENGTH) {
        logERROR(LOGSRC,"Read overflow");
        return -1;
    }
    
    /* read header */
    // This will read the header/type/length of a binary encoded packet, or
    // the '$' and 2 ASCII hex digits of an ASCII encoded packet.
    len = _transportRead(b, PACKET_HEADER_LENGTH);
    if (len < 0) {
        logERROR(LOGSRC,"Read error");
        return -1;
    } else
    if (len == 0) {
        logERROR(LOGSRC,"Timeout");
        return 0;
    }

    /* make sure entire header has been read */
    if (len < PACKET_HEADER_LENGTH) {
        // partial packet read (just close socket)
        logERROR(LOGSRC,"Read error [len=%d, expected %d]", len, PACKET_HEADER_LENGTH);
        return -1;
    }
    b += PACKET_HEADER_LENGTH;
    
    /* read payload */
    if (buf[0] == PACKET_ASCII_ENCODING_CHAR) {
        // ASCII encoded, read until '\r'
        for (; (b - buf) < bufLen; b++) {
            len = _transportRead(b, 1);
            if (len < 0) {
                logERROR(LOGSRC,"Read error");
                return -1;
            } else
            if (len == 0) {
                // timeout (partial packet read)
                logERROR(LOGSRC,"Timeoout");
                return 0;
            }
            if (*b == PACKET_ASCII_ENCODING_EOL) {
                *b = 0;
                break;
            }
        }
        if ((b - buf) < bufLen) {
            readLen = (b - buf);
        } else {
            // overflow (just close socket) - (unlikely to occur)
            logERROR(LOGSRC,"Read overflow");
            return -1;
        }
    } else
    if (buf[PACKET_HEADER_LENGTH - 1] != 0) {
        // read 'b[2]' bytes
        UInt16 payloadLen = (UInt16)buf[PACKET_HEADER_LENGTH - 1];
        len = _transportRead(b, payloadLen);
        if (len < 0) {
            logERROR(LOGSRC,"Read error");
            return -1;
        }
        if (len < payloadLen) {
            // timeout (partial packet read)
            logERROR(LOGSRC,"Timeout");
            //ClientPacketType_t hdrType = CLIENT_HEADER_TYPE(buf[0],buf[1]);
            //protocolQueueError(0,"%2x%2x", (UInt32)ERROR_PACKET_LENGTH, (UInt32)hdrType);
            return -1;
        }
        readLen = PACKET_HEADER_LENGTH + len;
    } else {
        readLen = PACKET_HEADER_LENGTH;
    }
    
    /* return lengtht */
    return readLen;
    
}

// ----------------------------------------------------------------------------

/* write packet to transport */
static int gprs_transportWritePacket(const UInt8 *buf, int bufLen)
{
    
    /* transport not open? */
    if (!gprsXport.isOpen) {
        logERROR(LOGSRC,"Transport is not open");
        return -1;
    }

    /* nothing specified to write */
    if (bufLen == 0) {
        return 0;
    }

    /* write data per transport type */
    int len = 0;
    switch (gprsXport.type) {
        case TRANSPORT_SIMPLEX:
            // queue data until the close
            len = binBufPrintf(&gprsDatagramBuffer, "%*b", bufLen, buf);
            return len;
        case TRANSPORT_DUPLEX:
            len = blueWrite((char*)buf, bufLen);
            return len;
        default:
            logERROR(LOGSRC,"Unknown transport type %d\n", gprsXport.type);
            return -1;
    }
    
}

// ----------------------------------------------------------------------------

static utBool bluePowerOnReset = utTrue;

/* initialize transport */
const TransportFtns_t *gprs_transportInitialize()
{
    
    /* init transport structure */
    memset(&gprsXport, 0, sizeof(GprsTransport_t));
    gprsXport.type   = TRANSPORT_NONE;
    gprsXport.isOpen = utFalse;
    
    /* init datagram buffer */
    binBuffer(&gprsDatagramBuffer, gprsDatagramData, sizeof(gprsDatagramData), BUFFER_DESTINATION);
    
    /* open bluetree modem */
    blueInitialize();
    blueOpenCom();
    
    /* DEBUG ONLY: test connection */
    //logWARNING(LOGSRC,"Testing Connection ....");
    //bluePowerOnReset = utTrue;
    //blueConnect();

    /* init function structure */
    gprsXportFtns.xportIsOpen       = &gprs_transportIsOpen;
    gprsXportFtns.xportOpen         = &gprs_transportOpen;
    gprsXportFtns.xportClose        = &gprs_transportClose;
    gprsXportFtns.xportReadFlush    = &gprs_transportReadFlush;
    gprsXportFtns.xportReadPacket   = &gprs_transportReadPacket;
    gprsXportFtns.xportWritePacket  = &gprs_transportWritePacket;
    return &gprsXportFtns;

}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

const char *blueGetDataHost()
{
    return propGetString(PROP_COMM_HOST,"");
}

Int16 blueGetDataPort()
{
    return (int)propGetUInt32(PROP_COMM_PORT,0L);
}

const char *blueGetDNS(int ndx)
{
    switch (ndx) {
        case 1: return propGetString(PROP_COMM_DNS_1,"");
        case 2: return propGetString(PROP_COMM_DNS_2,"");
    }
    return DNS_NULL;
}

const char *blueGetApnName()
{
    return propGetString(PROP_COMM_APN_NAME,"");
}

const char *blueGetApnServer()
{
    return propGetString(PROP_COMM_APN_SERVER,"");
}

const char *blueGetApnUser()
{
    return propGetString(PROP_COMM_APN_USER,"");
}

const char *blueGetApnPassword()
{
    return propGetString(PROP_COMM_APN_PASSWORD,"");
}

// ----------------------------------------------------------------------------

#define RESULT_OK_VERBOSE           "OK"
#define RESULT_NOCARRIER_VERBOSE    "NO CARRIER"
#define RESULT_ERROR_VERBOSE        "ERROR"

#define RESULT_GPRS_ACTIVATION      "Ok_Info_GprsActivation"
#define RESULT_TCP_ACTIVATION       "Ok_Info_WaitingForData"
#define RESULT_UDP_ACTIVATION       "Ok_Info_WaitingForData"
#define RESULT_SOCKET_CLOSED        "Ok_Info_SocketClosed"

#define RESULT_CME_ERROR_1          "#CME ERROR:"
#define RESULT_COMMAND_SYNTAX       "#CME ERROR: 34819"
#define RESULT_COMMAND_TOO_LONG     "#CME ERROR: 34881"
#define RESULT_ALREADY_CONNECTED    "#CME ERROR: 35840"
#define RESULT_NOT_REGISTERED       "#CME ERROR: 35865"
#define RESULT_INVALID_EVENT        "#CME ERROR: 35866"
#define RESULT_INACTV_PHYS_LAYER    "#CME ERROR: 35867"
#define RESULT_SERIVCE_RUNNING      "#CME ERROR: 37123"
#define RESULT_OPEN_SESSION_FAILED  "#CME ERROR: 38016"
#define RESULT_DNS_LOOKUP_FAILED    "#CME ERROR: 38027"
#define RESULT_OPEN_GPRS_FAILED     "#CME ERROR: 49155"

#define RESULT_CME_ERROR_2          "+CME ERROR:"
#define RESULT_SIM_NOT_INSERTED     "+CME ERROR: 10"
#define RESULT_SIM_PIN_REQUIRED     "+CME ERROR: 11"
#define RESULT_SIM_PUK_REQUIRED     "+CME ERROR: 12"
#define RESULT_SIM_FAILURE          "+CME ERROR: 13"
#define RESULT_IN_PROGRESS          "+CME ERROR: 515"

static Int32 CME_ERROR(const char *cmeErr) 
{
    if (strStartsWithIgnoreCase(cmeErr, RESULT_CME_ERROR_1)) {
        const char *cme = cmeErr + strlen(RESULT_CME_ERROR_1);
        while (*cme && isspace(*cme)) { cme++; }
        return strParseInt32(cme, 1L);
    } else
    if (strStartsWithIgnoreCase(cmeErr, RESULT_CME_ERROR_2)) {
        const char *cme = cmeErr + strlen(RESULT_CME_ERROR_2);
        while (*cme && isspace(*cme)) { cme++; }
        return strParseInt32(cme, 1L);
    }
    return 0L;
}

// ----------------------------------------------------------------------------

/* error responses */
#define CME_UNKNOWN                         1L     // unknown/unparsable error
#define CME_SIM_NOT_INSERTED               10L     // SIM not inserted
#define CME_SIM_PIN_REQUIRED               11L     // SIM PIN required
#define CME_SIM_PUK_REQUIRED               12L     // SIM PUK required
#define CME_SIM_FAILURE                    13L     // SIM failure
#define CME_IN_PROGRESS                   515L     // Operation in progress
#define CME_COMMAND_SYNTAX              34819L     // Bad Command: Syntax error
#define CME_COMMAND_TOO_LONG            34881L     // Bad Command: Command too long
#define CME_ALREADY_CONNECTED           35840L     // already connected
#define CME_NOT_REGISTERED              35865L     // Not registered on network
#define CME_INVALID_EVENT               35866L     // Invalid event during activation process
#define CME_PHYS_LAYER_CONN_INACTIVE    35867L     // Physical layer connection is currently not active
#define CME_GPRS_ABORTED                35868L     // Check APN
#define CME_SERVICE_RUNNING             37123L     // Service is running (can't set parm)
#define CME_SESSION_CLOSED_BY_PEER      37966L     // Session closed by peer
#define CME_DNS_ERROR                   38027L     // DNS lookup failed
#define CME_TCP_OPEN_ERROR              38016L     // TCP open failed
#define CME_GPRS_SESSION_FAILED         49155L     // Open GPRS session request failed

static const char *CME_ERROR_DESC(Int32 code)
{
    switch (code) {
        case CME_UNKNOWN                 : return "Unparsable error code";
        // "#CME ERROR:"
        case CME_COMMAND_SYNTAX          : return "Command syntax error";
        case CME_COMMAND_TOO_LONG        : return "Command too long";
        case CME_ALREADY_CONNECTED       : return "Already Connected";
        case CME_NOT_REGISTERED          : return "Not registered on network";
        case CME_INVALID_EVENT           : return "Invalid event during activation process";
        case CME_PHYS_LAYER_CONN_INACTIVE: return "Physical layer connection is currently not active";
        case CME_GPRS_ABORTED            : return "Check APN";
        case CME_SERVICE_RUNNING         : return "Service is running (can't set parameter)";
        case CME_DNS_ERROR               : return "DNS lookup failed";
        case CME_TCP_OPEN_ERROR          : return "TCP open failed";
        case CME_GPRS_SESSION_FAILED     : return "Open GPRS session request failed";
        // "+CME ERROR:"
        case CME_SIM_NOT_INSERTED        : return "SIM not inserted";
        case CME_SIM_PIN_REQUIRED        : return "SIM PIN required";
        case CME_SIM_PUK_REQUIRED        : return "SIM PUK required";
        case CME_SIM_FAILURE             : return "SIM Failure";
        case CME_IN_PROGRESS             : return "Operation in progress";
    }
    return "(Error Description Not Found)";
}

#define CME_ERROR_CHECK(RESP)   cmeErrorCheck_(__FILE__, __LINE__, RESP)
static utBool cmeErrorCheck_(const char *ftn, int line, const char *resp)
{
    Int32 cme = CME_ERROR(resp);
    if (cme > 0L) {
        if (cme > CME_UNKNOWN) {
            logWarning_(ftn, line, "[%ld] %s", (long)cme, CME_ERROR_DESC(cme));
        } else {
            logWarning_(ftn, line, "[?] %s", resp);
        }
        return utTrue;
    } else {
        return utFalse;
    }
}

#define ERROR_CHECK(RESP)   errorCheck_(__FILE__, __LINE__, RESP)
static utBool errorCheck_(const char *ftn, int line, const char *resp)
{
    if (strEqualsIgnoreCase(resp, RESULT_ERROR_VERBOSE)) {
        logWarning_(ftn, line, "%s", RESULT_ERROR_VERBOSE);
        return utTrue;
    } else
    if (strEqualsIgnoreCase(resp, RESULT_NOCARRIER_VERBOSE)) { 
        logWarning_(ftn, line, "%s", RESULT_NOCARRIER_VERBOSE);
        return utTrue;
    } else {
        return utFalse;
    }
}

// ----------------------------------------------------------------------------

/* control characters */
#define ASCII_ETX                   3
#define CONTROL_C                   ASCII_ETX
static char ETX_STRINGZ[] = { CONTROL_C, CONTROL_C, CONTROL_C, 0 };

// ----------------------------------------------------------------------------

// The Out-Of-Range timeout is checked first
// This probably shouldn't be less that a few hours, since this can occur under
// normal conditions when the modem is _really_ out of range of a GSM/GPRS towner.
#define MAX_OUTOFRANGE_TIMEOUT      MINUTE_SECONDS(180) // 3 hours

// Once the signal strength is finally valid, the 'Not Registered' timer is checked.
// This timeout can be relatively small.  This timeout would indicate that we've
// been receiving a satisfactory signal strength, but we've still not been able to
// register on the network.
#define MAX_UNREGISTERED_TIMEOUT    MINUTE_SECONDS(60) // 1 hour

// ----------------------------------------------------------------------------

static ComPort_t            blueComPort;

static utBool               blueTCPSocketOpen       = utFalse;

//static utBool               bluePowerOnReset        = utFalse;
static utBool               blueConnectionReset     = utTrue;
static utBool               blueStartupReset        = utTrue;

//static utBool               blueCOPS                = utFalse;

static TimerSec_t           blueLastComErrorTimer   = 0L;
static TimerSec_t           firstNotRegisteredTimer = 0L;
static TimerSec_t           firstOutOfRangeTimer    = 0L;

static UInt32               blueLastATZErrorCount   = 0L;

static char                 blueModemModel[80];
static char                 ipAddress[32];

// ----------------------------------------------------------------------------

void blueInitialize()
{
    comPortInitStruct(&blueComPort);

    /* set startup-reset flags */
    bluePowerOnReset        = utFalse;
    blueConnectionReset     = utTrue;
    blueStartupReset        = utTrue;
    blueLastComErrorTimer   = 0L;
    firstNotRegisteredTimer = 0L;
    firstOutOfRangeTimer    = 0L;
    blueLastATZErrorCount   = 0L;

}

// ----------------------------------------------------------------------------

void blueResetConnection()
{
    blueConnectionReset = utTrue;
}


// ----------------------------------------------------------------------------

#define BLUE_READLINE(C, RT, TMO, R, RLen) _blueReadLine(__FILE__, __LINE__, RT, TMO, R, RLen)

static utBool _blueReadLine(const char *ftn, int line, int retry, long timeoutMS, char *resp, int respLen)
{
    ComPort_t *com = &blueComPort;
    while (utTrue) {
        comPortFlushWhitespace(com, 100L);
        int len = comPortReadLine(com, resp, respLen, timeoutMS);
        if (len > 0) {
            // data was read
            break; 
        } else
        if (len < 0) {
            // ComPort error
            logWarning_(ftn, line, "Comport error");
            return utFalse; 
        } else {
            // timeout
            //logWarning_(ftn, line, "Comport timeout");
        }
        if (--retry <= 0) {
            // timeout
            logWarning_(ftn, line, "Comport read timeout");
            return utFalse; 
        }
    }
    return utTrue;
}

// ----------------------------------------------------------------------------

static utBool _blueSetDNS(const char *dns_1, const char *dns_2)
{
    ComPort_t *com = &blueComPort;
    char resp[128];
        
    /* DNS server #1 [ AT#DNSSERV1="0.0.0.0" ] */
    // Expect "OK" | "ERROR"
    if (dns_1 && *dns_1) {
        comPortWriteATFmt(com, "#DNSSERV1=\"%s\"", dns_1);
        while (utTrue) {
            if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))       { return utFalse; }
            if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))            { break; }
            if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))              { 
                if (strEqualsIgnoreCase(resp, RESULT_SERIVCE_RUNNING)) {
                    // just closing the socket may be sufficient
                    blueConnectionReset = utTrue;
                }
                return utFalse; 
            }
        }
        comPortFlush(com, 100L);
    }
        
    /* DNS server #2 [ AT#DNSSERV2="0.0.0.0" ] */
    // Expect "OK" | "ERROR"
    if (dns_2 && *dns_2) {
        comPortWriteATFmt(com, "#DNSSERV2=\"%s\"", dns_2);
        while (utTrue) {
            if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))  { return utFalse; }
            if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
            if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { 
                if (strEqualsIgnoreCase(resp, RESULT_SERIVCE_RUNNING)) {
                    // just closing the socket may be sufficient
                    blueConnectionReset = utTrue;
                }
                return utFalse; 
            }
        }
        comPortFlush(com, 100L);
    }
    
    return utTrue;
    
}

// ----------------------------------------------------------------------------

utBool blueOpenTCPSocket()
{
    ComPort_t *com = &blueComPort;
    char resp[600];
    const char *server = blueGetDataHost();
    int port = blueGetDataPort();
    logDEBUG(LOGSRC,"Openning TCP socket [%s:%d]", server, port);
    
    /* init open false */
    blueTCPSocketOpen = utFalse;
    
    /* set DNS */       
    const char *dns_1 = blueGetDNS(1);
    const char *dns_2 = blueGetDNS(2);
    if ((dns_1 && *dns_1) || (dns_2 && *dns_2)) {
        logDEBUG(LOGSRC,"Setting DNS: %s, %s", dns_1, dns_2);
        if (!_blueSetDNS(dns_1, dns_2)) { return utFalse; }
    }
    
    /* TCP server [ AT#TCPSERV="<host>" ] */
    // Expect "OK"
    comPortWriteATFmt(com, "#TCPSERV=\"%s\"", server);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);
    
    /* TCP port [ AT#TCPPORT=<port> ] */
    // Expect "OK"
    comPortWriteATFmt(com, "#TCPPORT=%d", port);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);
    
    /* open [ AT#OTCP ] */
    // Expect "Ok_Info_WaitingForData"
    comPortWriteAT(com, "#OTCP");
    while (utTrue) {
        // long timeout
        if (!BLUE_READLINE(com, 6, 20000L, resp, sizeof(resp))) { return utFalse; }
      //if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { return utFalse; } // <-- should not get this
        if (strEqualsIgnoreCase(resp, RESULT_TCP_ACTIVATION))   { break; } // success
        if (strEqualsIgnoreCase(resp, RESULT_SOCKET_CLOSED))    { return utFalse; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         {
            if (strEqualsIgnoreCase(resp, RESULT_INACTV_PHYS_LAYER))  {
                // TCP stack not initialized?
                // if we get here we need to close the current connection and start over.
                blueConnectionReset = utTrue;
            } else
            if (strEqualsIgnoreCase(resp, RESULT_DNS_LOOKUP_FAILED))  {
                // DNS failed to locate our server
                // if we get here we need to close the current connection and start over.
                blueConnectionReset = utTrue;
            }
            return utFalse;
        }
    }
    comPortFlush(com, 100L);

    /* socket is open */
    blueTCPSocketOpen = utTrue;
    return utTrue;
    
}

utBool blueIsTCPSocketOpen()
{
    return blueTCPSocketOpen;
}

// ----------------------------------------------------------------------------

/* close whatever type of socket is open (TCP/IDP) */
utBool blueCloseSocket()
{
    ComPort_t *com = &blueComPort;
    char resp[128];
    blueTCPSocketOpen = utFalse;
    comPortWrite(com, (UInt8*)ETX_STRINGZ, strlen(ETX_STRINGZ));
    while (utTrue) {
        if (!BLUE_READLINE(com, 3, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_SOCKET_CLOSED))  {
            logDEBUG(LOGSRC,"Socket closed");
            return utTrue; 
        }
    }
}

// ----------------------------------------------------------------------------

static char *_blueStartConnection()
{
    ComPort_t *com = &blueComPort;
    char resp[128];
    
    /* reset ipAddress */
    *ipAddress = 0;
    
    /* connect [ AT#CONNECTIONSTART ] */
    // Expect: "<ipAddr>","Ok_Info_GprsActivation" | "#CME ERROR: 35865"
    comPortWriteAT(com, "#CONNECTIONSTART");
    while (utTrue) {
        if (!BLUE_READLINE(com, 3, 10000L, resp, sizeof(resp)))       { return (char*)0; }
        if (strEqualsIgnoreCase(resp, RESULT_GPRS_ACTIVATION))        { break; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))             { break; }
        if (strStartsWithIgnoreCase(resp, RESULT_ALREADY_CONNECTED))  { break; }
        if (strStartsWithIgnoreCase(resp, RESULT_NOT_REGISTERED))     {
            // Registration was supposed to have been verified above.  If we get 
            // this error here, then we must have a serious problem.  Reset modem.
            logERROR(LOGSRC,"Connection failed with 'Not Registered', sending reset ...");
            bluePowerOnReset = utTrue;
            return (char*)0; 
        }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp)) { return (char*)0; }
        if (isdigit(resp[0])) {
            logDEBUG(LOGSRC,"[IP] %s", resp);
            strcpy(ipAddress, resp);
        }
    }
    comPortFlush(com, 100L);
    
    /* Get IP address [ AT#DISPLAYIP ]*/
    // Expect: '#MY IP: "10.80.71.42"','#PEER IP: "0.0.0.0",'#GATEWAY IP: "0.0.0.0",'OK' | '#CME ERROR: 35867'
    if (!*ipAddress || logIsLevel(SYSLOG_DEBUG)) {
        // Either we didn't get the IP address above, or we are at debug logging level
        // The most likely case is that we are currently in debug logging mode
        comPortWriteAT(com, "#DISPLAYIP");
        while (utTrue) {
            if (!BLUE_READLINE(com, 2, 1000L, resp, sizeof(resp)))  { return (char*)0; }
            if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
            if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         {
                if (strEqualsIgnoreCase(resp, RESULT_INACTV_PHYS_LAYER))  {
                    // if we get here we need to close the current connection and start over.
                    blueConnectionReset = utTrue;
                }
                return (char*)0;
            }
            logDEBUG(LOGSRC,"[IP] %s", resp);
            if (!*ipAddress && strStartsWithIgnoreCase(resp, "#MY IP: \"")) {
                char *ip = resp + 9; // first character after first quote
                const char *x = strLastIndexOfChar(ip, '\"');
                if (x == (char*)0) { x = ip + strlen(ip); }
                // save IP address
                strncpy(ipAddress, ip, (x - ip));
                ipAddress[x - ip] = 0;
            }
        }
        comPortFlush(com, 100L);
    }
    
    /* Get DNS addresses [ AT#VDNS ]*/
    // Expect: '#DNSSERV1: "X.X.X.X"','#DNSSERV2: "X.X.X.X", 'OK'
    if (logIsLevel(SYSLOG_DEBUG)) {
        // Show iff we are at debug logging level
        comPortWriteAT(com, "#VDNS");
        while (utTrue) {
            // ignore any errors
            if (!BLUE_READLINE(com, 2, 1000L, resp, sizeof(resp)))  { break; }
            if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
            if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { break; }
            logDEBUG(LOGSRC,"[DNS] %s", resp);
        }
        comPortFlush(com, 100L);
    }

    /* return IP address */
    return ipAddress;
    
}

static void _blueStopConnection()
{
    ComPort_t *com = &blueComPort;
    char resp[128];
    
    /* clear/reset ipAddress */
    *ipAddress = 0;
    
    /* close open socket */
    blueCloseSocket();

    /* disconnect [ AT#CONNECTIONSTOP ] */
    // Expect: "OK"
    comPortWriteAT(com, "#CONNECTIONSTOP");
    while (utTrue) {
        // ignore any errors
        if (!BLUE_READLINE(com, 3, 5000L, resp, sizeof(resp)))  { break; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { break; } // possible 35866
    }
    comPortFlush(com, 100L);

}

// ----------------------------------------------------------------------------

static utBool _blueInitModem()
{
    ComPort_t *com = &blueComPort;
    logDEBUG(LOGSRC,"Initializing ... (reset=%u)", (UInt16)bluePowerOnReset);
    char resp[256];
    
    /* assert DTR */
    //comPortSetDTR(com, utTrue);
    //comPortSetRTS(com, utTrue);

    /* discard existing characters */
    comPortFlush(com, 100L);

    /* software power-on reset */
    // This should be used sparingly.  The modem will need to re-aquire any 
    // tower signal, and thus may take a few seconds/minutes.
    if (bluePowerOnReset) {
        //bluePowerOnReset = utFalse;

        /* reset */
        logWARNING(LOGSRC,"Issuing 'Power-On' reset ...");
        comPortWriteAT(com, "+CFUN=1");
        threadSleepMS(1000L);
        // besides the "OK", this command does produce some cruft
        while (utTrue) {
            if (!BLUE_READLINE(com, 2, 2000L, resp, sizeof(resp)))  { return utFalse; }
            if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
            if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
            //logDEBUG(LOGSRC,"[Reset] %s", resp);
        }
        comPortFlush(com, 300L); // clear out slag
        firstOutOfRangeTimer    = 0L; // reset
        firstNotRegisteredTimer = 0L; // reset
        blueStartupReset = utTrue;

        /* clear power-on reset */
        bluePowerOnReset = utFalse;

        /* query for required PIN */
    }

    /* stop any current connection */
    if (blueConnectionReset) {
        blueConnectionReset = utFalse;
        // This may cause a "Comport read timeout" warning
        _blueStopConnection();
        blueStartupReset = utTrue;
    }

    /* reset & turn off echo */
    comPortWriteAT(com, "ZE0");
    while (utTrue) {
        if (!BLUE_READLINE(com, 2, 1000L, resp, sizeof(resp)))  {
            // read error, or timeout
            // If we get here too many consecutive times, then one of the following may have occurred:
            //  1) The modem was disconnected (nothing we can do about this)
            //  2) The modem is powered-off (nothing we can do about this)
            //  3) We've openned the wrong serial port
            //  4) Something is wrong with the serial port configuration (BPS, etc)
            //  5) The platform has a bug in their implementation of serial ports.
            if (++blueLastATZErrorCount > 5L) {
                // close comPort (reopen on next connection attempt)
                blueLastATZErrorCount = 0L; // reset counter
                blueCloseCom();
                // This case can occur on Cygwin (for #5 above).  I have found that closing the
                // serial port and re-openning it on the next connection attempt appear to fix
                // the problem (however comPortReopen doesn't work in this case).
            }
            return utFalse;
        }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        //logDEBUG(LOGSRC,"[Init] %s", resp);
    }
    comPortFlush(com, 100L);
    blueLastATZErrorCount = 0L; // reset counter

    /* enable "#CME ERROR: <>" messages */
    comPortWriteAT(com, "+CMEE=1");
    while (utTrue) {
        if (!BLUE_READLINE(com, 2, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        //logDEBUG(LOGSRC,"[CME] %s", resp);
    }
    comPortFlush(com, 100L);

    /* get modem model */
    // As far as I can tell, this command is not essential to communication
    // Expect: "MULTIBAND  G850  1900"
    comPortWriteAT(com, "+GMM");
    while (utTrue) {
        if (!BLUE_READLINE(com, 2, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        memset(blueModemModel, 0, sizeof(blueModemModel));
        strncpy(blueModemModel, resp, sizeof(blueModemModel) - 1);
        //logDEBUG(LOGSRC,"[Model] %s", resp);
    }
    comPortFlush(com, 100L);

    /* start-up reset */
    // This is used for startup initialization
    if (blueStartupReset) {
        blueStartupReset = utFalse;
        
        /* start TCP/IP stack [ AT+WOPEN=1 ] */
        // This needs to be done only once for each modem
        // Expect: "OK"
        logINFO(LOGSRC,"Restarting TCP/IP stack ...");
        comPortWriteAT(com, "+WOPEN=1");
        while (utTrue) {
            if (!BLUE_READLINE(com, 4, 1000L, resp, sizeof(resp)))  { return utFalse; }
            if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
            if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        }
        comPortFlush(com, 100L);
        
    }

    /* signal strength [ AT+CSQ ] */
    // Expect: "+CSQ: 99,99";
    int sigStrength = -1;
    comPortWriteAT(com, "+CSQ");
    while (utTrue) {
        if (!BLUE_READLINE(com, 2, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        if (strStartsWithIgnoreCase(resp, "+CSQ: ")) {
            char *s = resp + 6;
            sigStrength = (int)strParseInt32(s, -1);
            if (sigStrength >= 99) {
                logWARNING(LOGSRC,"[Signal out-of-range] %s", resp);
                if (firstOutOfRangeTimer <= 0L) {
                    firstOutOfRangeTimer = utcGetTimer();
                } else 
                if (utcIsTimerExpired(firstOutOfRangeTimer,MAX_OUTOFRANGE_TIMEOUT)) {
                    // we've been getting this result too many time, reset modem
                    logERROR(LOGSRC,"Too many 'Out-Of-Range' errors, sending reset ...");
                    firstOutOfRangeTimer = 0L; // reset
                    bluePowerOnReset = utTrue;
                }
                return utFalse;
            } else {
                logINFO(LOGSRC,"Signal Strength: %d/31", sigStrength);
            }
        }
    }
    comPortFlush(com, 100L);
    if (firstOutOfRangeTimer > 0L) {
        // If this is the first ok signal strength (after an bad one),
        // then reset the 'Not Registered' timer also.
        firstOutOfRangeTimer    = 0L; // clear
        firstNotRegisteredTimer = 0L;
    }
    if (sigStrength <= 1) {
        logWARNING(LOGSRC,"[Weak signal] %s", resp);
        return utFalse;
    }
    
    /* set for automatic registration */
    /* check if registered */
    // Expect: "+CREG: 0,1" | "+CREG: 0,2" | "+CREG: 0,5"
    // Note: "+CREG: 0,0" means that it's not registered, and it's not even trying to BE registered. 
    utBool registered = utFalse;
    comPortWriteAT(com, "+CGREG?"); // "+CREG?"/"+CGREG?"
    while (utTrue) {
        if (!BLUE_READLINE(com, 3, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        if (strStartsWithIgnoreCase(resp, "+CGREG: ") ||
            strStartsWithIgnoreCase(resp, "+CREG: ")    ) {
            char *s = resp + 6;                // skip shortest header length
            while (*s && (*s != ' ')) { s++; }  // find first space
            while (*s && (*s == ' ')) { s++; }  // skip spaces
            if (isdigit(*s)) { s++; }           // skip first digit ("0")
            if (*s == ',') { s++; }             // skip ","
            if (*s == '1') {
                registered = utTrue;            // home network
            } else 
            if (*s == '5') {
                registered = utTrue;            // roaming
            }
            if (!registered) {
                // If were here, we've passed the signal strength test, so we should be 
                // registered, or will be soon.
                // If we return here too many times, we should reset the modem.
                logWARNING(LOGSRC,"[Not registered on network] %s", resp);
                if (firstNotRegisteredTimer <= 0L) {
                    firstNotRegisteredTimer = utcGetTimer();
                } else 
                if (utcIsTimerExpired(firstNotRegisteredTimer,MAX_UNREGISTERED_TIMEOUT)) {
                    // we've been getting this result too many times, reset modem
                    logERROR(LOGSRC,"Too many 'Not Registered' errors, sending reset ...");
                    firstNotRegisteredTimer = 0L; // reset
                    bluePowerOnReset = utTrue;
                }
                return utFalse;
            }
        }
    }
    comPortFlush(com, 100L);
    firstNotRegisteredTimer = 0L; // clear

    /* advanced diagnostics [ AT+CCED=0,1 ] */
    // As far as I can tell, this command is not essential to communication
    // Expect: "+CCED: 310,380,1914,3059,42,163,23,,,0,,,0"
    comPortWriteAT(com, "+CCED=0,1");
    while (utTrue) {
        if (!BLUE_READLINE(com, 3, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);
    
    /* get APN info */
    int apnCID = 1;
    const char *apnName = blueGetApnName();
    const char *apnServ = blueGetApnServer();
    const char *apnUser = blueGetApnUser();
    const char *apnPass = blueGetApnPassword();
    logINFO(LOGSRC,"APN: name=%s serv=%s user=%s pass=%s", apnName, apnServ, apnUser, apnPass);

    /* specify ISP [ AT+CGDCONT=1,"IP","proxy" ] */
	//    Rogers Wireless
    //      APN: name=internet.com serv= user=wapuser1 pass=wap 
    //      DNS: dns1=207.181.101.4 dns2=207.181.101.5
	//    AT&T Wireless     
    //      APN: name=proxy serv=proxy user= pass=
    //      APN: name=public serv=public user= pass=
	//    Cingular Wireless 
    //      APN: name=isp.cingular serv= user=ISP@CINGULARGPRS.COM pass=CINGULAR1
    //      DNS: dns1=66.209.10.201 dns2=66.209.10.202
	//    T-Mobile
    //      APN: name=internet3.voicestream.com serv= user= pass=
    //      DNS: dns1=216.155.175.40 dns2=216.155.175.41
	//    Microcell
    //      APN: name=internet.fido.ca serv= user=fido pass=fido
	//    Entel PCS
    //      APN: name=imovil.entelpcs.cl serv= user=entelpcs pass=entelpcs
    // Expect: "OK"
    if (apnName && *apnName) {
        comPortWriteATFmt(com, "+CGDCONT=%d,\"IP\",\"%s\"", apnCID, apnName);
    } else {
        comPortWriteATFmt(com, "+CGDCONT=%d,\"IP\"", apnCID);
    }
    while (utTrue) {
        if (!BLUE_READLINE(com, 3, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        //logDEBUG(LOGSRC,"[ISP] %s", resp);
    }
    comPortFlush(com, 100L);

    /* GPRS mode #1 ("ATTach") [ AT+CGATT=1 ]*/
    // Expect: "OK" | "ERROR"
    comPortWriteAT(com, "+CGATT=1");
    while (utTrue) {
        if (!BLUE_READLINE(com, 3, 2000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        //logDEBUG(LOGSRC,"[GPRS1] %s", resp);
    }
    comPortFlush(com, 100L);

    /* GPRS mode #2 [ AT#GPRSMODE=1 ]*/
    // Expect: "OK" | "ERROR"
    comPortWriteAT(com, "#GPRSMODE=1");
    while (utTrue) {
        if (!BLUE_READLINE(com, 3, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (strEqualsIgnoreCase(resp, RESULT_ERROR_VERBOSE))    {
            // An error here likely means that the TCP/IP stack has not been previously 
            // started with the command "AT+WOPEN=1".  Normally, this should not occur
            // since "AT+WOPEN=1" should be automatically called each time the GPRS
            // comport is openned. Possibly, a new modem was installed since the comport
            // was openned (a highly unlikely occurance, but possible), so the best we
            // can hope for at this time is to re-attempt to initialize the startup
            // commands.
            blueStartupReset = utTrue;
            return utFalse;
        }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        //logDEBUG(LOGSRC,"[GPRS2] %s", resp);
    }
    comPortFlush(com, 100L);

    /* APN server [ AT#APNSERV="" ] */
    // Expect "OK" | "ERROR"
    comPortWriteATFmt(com, "#APNSERV=\"%s\"", apnServ);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1500L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);

    /* APN [ AT#APNUN="" ] */
    // Expect "OK" | "ERROR"
    comPortWriteATFmt(com, "#APNUN=\"%s\"", apnUser);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1500L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);

    /* APN password [ AT#APNPW="" ] */
    // Expect "OK" | "ERROR"
    comPortWriteATFmt(com, "#APNPW=\"%s\"", apnPass);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1500L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);

    /* clear DNS? */
    const char *dns_1 = blueGetDNS(1);
    const char *dns_2 = blueGetDNS(2);
    if ((!dns_1 || !*dns_1) && (!dns_2 || !*dns_2)) {
        logDEBUG(LOGSRC,"Clearing DNS: %s, %s", DNS_NULL, DNS_NULL);
        if (!_blueSetDNS(DNS_NULL, DNS_NULL)) { return utFalse; }
    }

    return utTrue;
}

// ----------------------------------------------------------------------------

/* return true if modem is open */
utBool blueIsOpen()
{
    return comPortIsOpen(&blueComPort);
}

/* open modem serial port */
utBool blueOpenCom()
{
    ComPort_t *com = &blueComPort;
    comPortInitStruct(com);

    /* port config */
    const char *portName = propGetString(PROP_CFG_XPORT_PORT, "0");
    UInt32 portBPS = propGetUInt32(PROP_CFG_XPORT_BPS, 0L);
    if (portBPS == 0L) { portBPS = MODEM_SPEED_BPS; }
    
    /* open port */
    if (comPortOpen(com,portName,portBPS,"8N1",utFalse) == (ComPort_t*)0) {
        if (utcIsTimerExpired(blueLastComErrorTimer,900L)) {
            blueLastComErrorTimer = utcGetTimer();
            logCRITICAL(LOGSRC,"Unable to open GPRS port: %s", portName); //, strerror(errno));
        }
        threadSleepMS(1000L);
        return utFalse;
    }
    
    /* comport logging? */
    if (propGetBoolean(PROP_CFG_XPORT_DEBUG,utFalse)) {
        comPortSetDebugLogger(com, 0);
    }
    
    /* reset */
    blueConnectionReset = utTrue;
    
    /* return success */
    threadSleepMS(500L); // wait
    return utTrue;
    
}

void blueCloseCom()
{
    ComPort_t *com = &blueComPort;
    if (comPortIsOpen(com)) {
        _blueStopConnection();
        comPortClose(com);
    }
}

// ----------------------------------------------------------------------------

int blueConnect()
{
    
    /* open port? */
    if (!blueIsOpen() && !blueOpenCom()) {
        // unable to open port (error already printed)
        return -1;
    }

    /* set DTR/RTS true */
    ComPort_t *com = &blueComPort;
    comPortSetDTR(com, utTrue);
    comPortSetRTS(com, utTrue);

    /* flush anything currently in the buffers */
    comPortFlush(com, 0L);

    /* init & precheck connectability */
    if (!_blueInitModem()) {
        return 0;
    }

    /* get connected ip address */
    char *ip = _blueStartConnection();
    if (!ip) {
        // no ip address, still unable to connect
        return 0;
    }
    logINFO(LOGSRC,"IP Address: %s\n", ip);
    
    return 1;
}

// ----------------------------------------------------------------------------

/* write string (open socket assumed) */
int blueWrite(const char *buf, int bufLen)
{
    ComPort_t *com = &blueComPort;
    if (bufLen > 0) {
        return comPortWrite(com, (UInt8*)buf, bufLen);
    }
    return bufLen;
}

/* write string (open socket assumed) */
int blueWriteString(const char *msg)
{
    ComPort_t *com = &blueComPort;
    int msgLen = strlen(msg);
    if (msgLen > 0) {
        logDEBUG(LOGSRC,"Sending: %s", msg);
        comPortWriteString(com, msg);
        if (msg[msgLen - 1] != '\r') {
            comPortWrite(com, (UInt8*)"\r", 1);
            msgLen++;
        }
    }
    return msgLen;
}

/* write formatted string (open socket assumed) */
int blueWriteStringFmt(const char *fmt, ...)
{
    char out[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(out, fmt, ap);
    va_end(ap);
    return blueWriteString(out);
}

// ----------------------------------------------------------------------------

static char  readBuffer[PACKET_MAX_ENCODED_LENGTH] = { 0 };
static int   readLen = 0;
static int   readPtr = 0;

/* read bytes (open socket assumed) */
int blueRead(char *resp, int respLen, long timeoutMS)
{
    
    /* refill buffer */
    if ((readLen <= 0) || (readPtr >= readLen)) {
        //logINFO(LOGSRC,"Filling String buffer ...");
        readLen = blueReadString(readBuffer, sizeof(readBuffer) - 1, timeoutMS);
        if (readLen < 0) {
            logINFO(LOGSRC,"Read error ...");
            return -1;
        } else
        if (strEqualsIgnoreCase(readBuffer, RESULT_SOCKET_CLOSED))  { 
            blueTCPSocketOpen = utFalse;
            readLen = 0;
            return -1;
        }
        readBuffer[readLen++] = '\r'; // re-add line terminator
        readBuffer[readLen] = 0; // terminate
        readPtr = 0;
        //logINFO(LOGSRC,"Read: %s", readBuffer);
    }
    
    /* return requested data */
    int maxLen = respLen;
    if (maxLen > (readLen - readPtr)) { maxLen = readLen - readPtr; }
    memcpy(resp, &readBuffer[readPtr], maxLen);
    readPtr += maxLen;
    return maxLen;

}

/* read string (open socket assumed) */
int blueReadString(char *resp, int respLen, long timeoutMS)
{
    //ComPort_t *com = &blueComPort;
    char buff[PACKET_MAX_ENCODED_LENGTH];
    
    /* no space to hold data */
    if (respLen <= 0) {
        return 0;
    }
    
    /* already have data in the read buffer? */
    if (readPtr < readLen) {
        int maxLen = respLen;
        if (maxLen > (readLen - readPtr)) { maxLen = readLen - readPtr; }
        memcpy(resp, &readBuffer[readPtr], maxLen);
        readPtr += maxLen;
        if (resp[maxLen - 1] == '\r') { maxLen--; }
        resp[maxLen] = 0;
        return maxLen;
    }
    
    /* read response from server */
    int  R = (timeoutMS < 0L)? 2 : 1;
    long T = labs(timeoutMS) / (long)R;
    if (!BLUE_READLINE(com, R, T, buff, sizeof(buff)))    {
        // socket may still be open
        return -1; 
    }
    if (strEqualsIgnoreCase(buff, RESULT_SOCKET_CLOSED))  { 
        blueTCPSocketOpen = utFalse;
        return -1;
    }
    //logDEBUG(LOGSRC,"Response: %s", buff);
    
    /* return response */
    int buffLen = strlen(buff);
    if (buffLen < respLen) {
        strcpy(resp, buff);
    } else {
        buffLen = respLen - 1; // truncate
        strncpy(resp, buff, buffLen);
        resp[buffLen] = 0;
    }
    
    return buffLen;
}

// ----------------------------------------------------------------------------

/* write datagram (open connection assumed) */
utBool blueSendDatagram(char *buf, int bufLen)
{
    ComPort_t *com = &blueComPort;
    char resp[600];
    const char *server = blueGetDataHost();
    int port = blueGetDataPort();
    logDEBUG(LOGSRC,"Openning UDP socket [%s:%d]", server, port);

    /* set DNS */       
    const char *dns_1 = blueGetDNS(1);
    const char *dns_2 = blueGetDNS(2);
    if ((dns_1 && *dns_1) || (dns_2 && *dns_2)) {
        logDEBUG(LOGSRC,"Setting DNS: %s, %s", dns_1, dns_2);
        if (!_blueSetDNS(dns_1, dns_2)) { return utFalse; }
    }
    
    /* UDP server [ AT#UDPSERV="<host>" ] */
    // Expect "OK"
    comPortWriteATFmt(com, "#UDPSERV=\"%s\"", server);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);
    
    /* UDP port [ AT#UDPPORT=<port> ] */
    // Expect "OK"
    comPortWriteATFmt(com, "#UDPPORT=%d", port);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);
    
    /* open [ AT#OUDP ] */
    // Expect "Ok_Info_WaitingForData"
    comPortWriteAT(com, "#OUDP");
    while (utTrue) {
        // long timeout (since this is a UDP conection, this should occur quickly)
        if (!BLUE_READLINE(com, 6, 20000L, resp, sizeof(resp))) { return utFalse; }
      //if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { return utFalse; } // <-- should not get this
        if (strEqualsIgnoreCase(resp, RESULT_UDP_ACTIVATION))   { break; } // success
        if (strEqualsIgnoreCase(resp, RESULT_SOCKET_CLOSED))    { return utFalse; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         {
            if (strEqualsIgnoreCase(resp, RESULT_INACTV_PHYS_LAYER))  {
                // TCP stack not initialized?
                // if we get here we need to close the current connection and start over.
                blueConnectionReset = utTrue;
            } else
            if (strEqualsIgnoreCase(resp, RESULT_DNS_LOOKUP_FAILED))  {
                // DNS failed to locate our server
                // if we get here we need to close the current connection and start over.
                blueConnectionReset = utTrue;
            }
            return utFalse;
        }
    }
    comPortFlush(com, 100L);
    
    /* write data */
    int writeLen = blueWrite(buf, bufLen);
    logDEBUG(LOGSRC,"Datagram written [%d]", writeLen);

    /* close socket */
    blueCloseSocket();
    
    /* datagram written */
    return utTrue;

}

// ----------------------------------------------------------------------------

utBool bluePing(const char *server)
{
    ComPort_t *com = &blueComPort;
    char resp[128];
    int pingDelay = 1;
    int pingNum = 5;
    
    /* Ping delay [ AT#PINGDELAY=1 ] */
    // Expect "OK
    comPortWriteATFmt(com, "#PINGDELAY=%d", pingDelay);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);
    
    /* Ping delay [ AT#PINGNUM=5 ] */
    // Expect "OK
    comPortWriteATFmt(com, "#PINGNUM=%d", pingNum);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);
    
    /* Ping delay [ AT#PINGREMOTE=server ] */
    // Expect "OK
    comPortWriteATFmt(com, "#PINGREMOTE=\"%s\"", server);
    while (utTrue) {
        if (!BLUE_READLINE(com, 1, 1000L, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
    }
    comPortFlush(com, 100L);
    
    /* Ping delay [ AT#PING ] */
    // Expect "OK
    comPortWriteAT(com, "#PING");
    while (utTrue) {
        Int32 tmout = pingDelay * 1000L;
        if (!BLUE_READLINE(com, 3, tmout, resp, sizeof(resp)))  { return utFalse; }
        if (strEqualsIgnoreCase(resp, RESULT_OK_VERBOSE))       { break; }
        if (ERROR_CHECK(resp) || CME_ERROR_CHECK(resp))         { return utFalse; }
        logINFO(LOGSRC,"Ping: %s\n", resp);
    }
    comPortFlush(com, 100L);
    
    return utTrue;

}

// ----------------------------------------------------------------------------

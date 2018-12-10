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
//  Socket based transport support..
// Notes:
//  - This implementation supports UDP/TCP communications with a DMTP server
//  using the standard library 'socket' functions.  The embedded platform on
//  which this module is install may require other initialization methods for 
//  setting up the connection with the DMTP server.
//  - 
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "custom/log.h"

#include "custom/transport/socket.h"

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/bintools.h"
#include "tools/utctools.h"
#include "tools/threads.h"  // for 'threadSleepMS'
#include "tools/comport.h"
#include "tools/sockets.h"

#include "base/props.h"
#include "base/propman.h"
#include "base/packet.h"

#if defined(TARGET_WINCE)
#  include <winsock2.h>
#  include <ras.h>
#  include <raserror.h>
#  include "custom/wince/wcegprs.h"
#  if !defined(DELAY_AFTER_CONNECTION_FAILED)
#    define DELAY_AFTER_CONNECTION_FAILED     (MINUTE_SECONDS(4) * 1000L) // ms
#  endif
#endif

// ----------------------------------------------------------------------------

/* transport structure */
typedef struct {
    TransportType_t             type;
    utBool                      isOpen;
    Socket_t                    sock;
#if defined(TARGET_WINCE)
    HRASCONN                    rasConn;
    utBool                      rasDisconnect;
    utBool                      isFirst;            
#endif
} SocketTransport_t;

static SocketTransport_t        sockXport;

// ----------------------------------------------------------------------------

static TransportFtns_t          sockXportFtns = { MEDIA_SOCKET, "XportSocket" };

// The size of the datagram buffer is arbitrary, however the amount of 
// data transmitted in a single datagram should not be larger than the MTU
// to avoid possible fragmentation which could result in a higher possibility
// of data loss.
static UInt8                    sockDatagramData[2000];
static Buffer_t                 sockDatagramBuffer;

// ----------------------------------------------------------------------------

static const char *socket_transportTypeName(TransportType_t type)
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
static void socket_transportReadFlush()
{
    // NO-OP
}

/* return true if transport is open */
static utBool socket_transportIsOpen()
{
    // this may be called by threads other than the protocol thread.
    return sockXport.isOpen;
}

/* close transport */
static utBool socket_transportClose(utBool sendUDP)
{
    if (sockXport.isOpen) {
        logDEBUG(LOGSRC,"%s Transport close ...\n\n", socket_transportTypeName(sockXport.type));
        utBool rtn = utTrue;

        /* check for datagram transmission */
        if (sendUDP && (sockXport.type == TRANSPORT_SIMPLEX)) {
            // transmit queued datagram
            Buffer_t *bf = &sockDatagramBuffer;
            UInt8 *data = BUFFER_PTR(bf);
            int dataLen = BUFFER_DATA_LENGTH(bf);
            int err = socketWriteUDP(&(sockXport.sock), data, dataLen);
            rtn = (err < 0)? utFalse : utTrue;
        }
        
        /* close socket */
        socketCloseClient(&(sockXport.sock));
        
        /* transport is closed */
        sockXport.type   = TRANSPORT_NONE;
        sockXport.isOpen = utFalse;
        
        /* reset buffers */
        binResetBuffer(&sockDatagramBuffer);
        
        return rtn;
        
    } else {
        
        /* wasn't open */
        return utFalse;
        
    }
}

/* open transport */
static utBool socket_transportOpen(TransportType_t type)
{
    logINFO(LOGSRC,"Starting socket transport ...");

    /* close, if already open */
    if (sockXport.isOpen) {
        // it shouldn't already be open
        logWARNING(LOGSRC,"Transport seems to still be open!");
        socket_transportClose(utFalse);
    }

    /* get host:port */
    const char *host = propGetString(PROP_COMM_HOST, "");
    int port = (int)propGetUInt32(PROP_COMM_PORT, 0L);
    if (!host || !*host || (port <= 0)) {
        // If this is true, the client will NEVER connect.
        logCRITICAL(LOGSRC,"Transport host/port not specified ...\n");
        threadSleepMS(MINUTE_SECONDS(30) * 1000L);
        return utFalse;
    }
    //logINFO(LOGSRC,"transportOpen: host=%s port=%d", host, port);
    
#if defined(TARGET_WINCE)

    /* make sure network connection is established */
    utBool forceDisconnectFirst = sockXport.rasDisconnect;
    sockXport.rasDisconnect = utFalse;
    if (!wceGprsConnect(&(sockXport.rasConn),forceDisconnectFirst)) {
        logDEBUG(LOGSRC,"Connection failed: retry delay [%lu sec] ...", DELAY_AFTER_CONNECTION_FAILED/1000L);
        threadSleepMS(DELAY_AFTER_CONNECTION_FAILED); // wait before attempting to reconnect
        return utFalse;
    }
    // NOTE: Just because we now have confirmation that we have a connection with
    // our named entry (PROP_COMM_CONNECTION) doesn't mean that the following will
    // actually use that connection for routing Internet access. I have found that
    // while I have an Internet-enabled ActiveSync connection with my laptop, it
    // is my laptop that will be used as the Internet gateway.  While this can
    // possibly be disabled through ActiveSync, this just confirms that the WinCE
    // device may use other connections for the gateway rather than our specified
    // named connection.
    
#endif

    /* open */
    int err = 0;
    switch (type) {
        case TRANSPORT_SIMPLEX:
            err = socketOpenUDPClient(&(sockXport.sock), host, port);
            break;
        case TRANSPORT_DUPLEX:
            err = socketOpenTCPClient(&(sockXport.sock), host, port);
            break;
        default:
            logCRITICAL(LOGSRC,"Transport not SIMPLEX or DUPLEX: %d", type);
            // This is likely an implementation error
            threadSleepMS(MINUTE_SECONDS(20) * 1000L);
            return utFalse;
    }
    if (err != COMERR_SUCCESS) {
        if (err == COMERR_SOCKET_HOST) {
            // possible DNS issue, or invalid IP address
            logERROR(LOGSRC,"Can't resolve (check DNS) [%d]: %s", err, host);
        } else
        if (err == COMERR_SOCKET_CONNECT) {
            logERROR(LOGSRC,"Can't connect [%d]: %s:%d", err, host, port);
        } else {
            logWARNING(LOGSRC,"Error openning transport [%d]", err);
        }
        // transport error
        threadSleepMS(5000L);
        return utFalse;
    }
    sockXport.type = type;
    sockXport.isOpen = utTrue;
    // fall through to 'true' return below

    /* reset buffers and return success */
    binResetBuffer(&sockDatagramBuffer);
    logDEBUG(LOGSRC,"Openned %s Transport ...\n", socket_transportTypeName(type));
    return sockXport.isOpen;
    
}

// ----------------------------------------------------------------------------

static int _transportRead(UInt8 *buf, int bufLen)
{
    
    /* transport not open? */
    if (!sockXport.isOpen) {
        logERROR(LOGSRC,"Transport is not open");
        return -1;
    }

    /* cannot read when connecting via Simplex */
    if (sockXport.type == TRANSPORT_SIMPLEX) {
        logERROR(LOGSRC,"Cannot read from Simplex transport");
        return -1;
    }
    
    /* no data was requested */
    if (bufLen == 0) {
        return 0;
    }
    
    /* return bytes read */
#if defined(TARGET_WINCE)
    long readTimeoutMS = 10000L; // GPRS connection
#else
    long readTimeoutMS = -1L;
#endif
    int readLen = 0;
    readLen = socketReadTCP(&(sockXport.sock), buf, bufLen, readTimeoutMS);
    return readLen;
    
}

/* read packet from transport */
static int socket_transportReadPacket(UInt8 *buf, int bufLen)
{
    UInt8 *b = buf;
    int readLen, len;
    
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
            // overflow (just close socket) - unlikely
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
    
    /* decrypt */
    //if (*buf == PACKET_ASCII_ENCRYPTED_CHAR) {
    //    readLen = strDecryptAscii(buf, readLen);
    //}

    /* return length */
    return readLen;
    
}

// ----------------------------------------------------------------------------

/* write packet to transport */
static int socket_transportWritePacket(const UInt8 *buf, int bufLen)
{
    
    /* transport not open? */
    if (!sockXport.isOpen) {
        logERROR(LOGSRC,"Transport is not open");
        return -1;
    }

    /* nothing specified to write */
    if (bufLen == 0) {
        return 0;
    }
    
    /* encrypt */
    //UInt8 newBuf[PACKET_MAX_ENCODED_LENGTH];
    //if (*buf == PACKET_ASCII_ENCODING_CHAR) {
    //    bufLen = strEncryptAscii(newBuf, buf, bufLen);
    //    buf = newBuf;
    //}

    /* write data per transport type */
    int len = 0;
    switch (sockXport.type) {
        case TRANSPORT_SIMPLEX:
            // queue data until the close
            len = binBufPrintf(&sockDatagramBuffer, "%*b", bufLen, buf);
            return len;
        case TRANSPORT_DUPLEX:
            len = socketWriteTCP(&(sockXport.sock), buf, bufLen);
            if (len < 0) {
                logERROR(LOGSRC,"Socket write error: %d", len);
                return -1;
            }
            return len;
        default:
            logERROR(LOGSRC,"Unknown transport type %d\n", sockXport.type);
            return -1;
    }
    
}

// ----------------------------------------------------------------------------

/* initialize transport */
const TransportFtns_t *socket_transportInitialize()
{
    
    /* init transport structure */
    memset(&sockXport, 0, sizeof(SocketTransport_t));
    sockXport.type    = TRANSPORT_NONE;
    sockXport.isOpen  = utFalse;
#if defined(TARGET_WINCE)
    sockXport.rasConn = NULL;
    sockXport.rasDisconnect = utFalse;
    sockXport.isFirst = utTrue;
#endif

    /* init datagram buffer */
    binBuffer(&sockDatagramBuffer, sockDatagramData, sizeof(sockDatagramData), BUFFER_DESTINATION);

    /* other initialization */
#if defined(TARGET_WINCE)
    wceGprsInitialize();
#endif

    /* init function structure */
    sockXportFtns.xportIsOpen       = &socket_transportIsOpen;
    sockXportFtns.xportOpen         = &socket_transportOpen;
    sockXportFtns.xportClose        = &socket_transportClose;
    sockXportFtns.xportReadFlush    = &socket_transportReadFlush;
    sockXportFtns.xportReadPacket   = &socket_transportReadPacket;
    sockXportFtns.xportWritePacket  = &socket_transportWritePacket;
    return &sockXportFtns;
    
}

// ----------------------------------------------------------------------------

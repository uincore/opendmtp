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
//  Server transport support for socket DMTP communications.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/27  Martin D. Flynn
//     -Set default port to 31000
// ----------------------------------------------------------------------------

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
#include "tools/sockets.h"

#include "server/defaults.h"
#include "server/server.h"
#include "server/serrors.h"
#include "server/packet.h"
#include "server/protocol.h"
#include "server/log.h"

// ----------------------------------------------------------------------------

#define DEFAULT_SERVER_PORT     31000 // this default may change at any time

static Socket_t         serverSocket;

static PacketEncoding_t clientPacketEncoding = ENCODING_HEX;

// ----------------------------------------------------------------------------

void serverInitialize()
{
    
    /* init server port */
    socketInitStruct(&serverSocket, (char*)0, 0, SOCK_STREAM);
    
}

// ----------------------------------------------------------------------------

utBool serverIsOpen()
{
    return socketIsOpenClient(&serverSocket);
}

utBool serverOpen(const char *portName, utBool portLog)
{
    logINFO(LOGSRC,"Waiting for client socket ...");
    int sockPort = (int)strParseInt32(portName, (Int32)DEFAULT_SERVER_PORT);

    /* close, if already open */
    if (serverIsOpen()) {
        // it shouldn't already be open
        logWARNING(LOGSRC,"Server port seems to still be open!");
        serverClose();
    }
    
    /* open server port */
    if (!socketIsOpenServer(&serverSocket)) {
        if (sockPort < 0) {
            logWARNING(LOGSRC,"Invalid socket port: %d", sockPort);
            return utFalse;
        }
        int err = socketOpenTCPServer(&serverSocket, sockPort);
        if (err < 0) {
            logWARNING(LOGSRC,"Unable to open server socket: %d", sockPort);
            return utFalse;
        }
    }
    
    /* wait for incoming client */
    if (socketAcceptTCPClient(&serverSocket) < 0) {
        logWARNING(LOGSRC,"Unable to accept client socket");
        return utFalse;
    }

    /* return successful */
    return utTrue;
    
}

utBool serverClose()
{
    if (serverIsOpen()) {
        socketCloseClient(&serverSocket);
        //socketCloseServer(&serverSocket);
        return utTrue;
    } else {
        return utFalse;
    }
}

// ----------------------------------------------------------------------------

/* flush input buffer */
void serverReadFlush()
{
    // flush (not necessary)
}

// ----------------------------------------------------------------------------

static int _serverRead(UInt8 *buf, int bufLen)
{
    
    /* no data was requested */
    if (bufLen == 0) {
        logERROR(LOGSRC,"NULL buffer");
        return 0;
    }
    
    /* port open? */
    if (!serverIsOpen()) {
        logERROR(LOGSRC,"Server port not open!!!");
        return -1;
    }
    
    /* return bytes read */
    int readLen = 0;
    readLen = socketReadTCP(&serverSocket, buf, bufLen, 3000L);
    return readLen;
    
}

static int _serverReadPacketBuffer(UInt8 *buf, int bufLen)
{
    UInt8 *b = buf;
    int readLen = 0, len;
    
    /* read header */
    // This will read the header/type/length of a binary encoded packet, or
    // the '$' and 2 ASCII hex digits of an ASCII encoded packet.
    len = _serverRead(b, PACKET_HEADER_LENGTH);
    if (len < 0) {
        logERROR(LOGSRC,"Read error (packet header)");
        return SRVERR_TRANSPORT_ERROR;
    } else
    if (len == 0) {
        //logERROR(LOGSRC,"Timeout (packet header)");
        return SRVERR_TIMEOUT;
    }

    /* make sure entire header has been read */
    if (len < PACKET_HEADER_LENGTH) {
        // partial packet read (just close socket)
        logERROR(LOGSRC,"Read error [len=%d, expected %d]", len, PACKET_HEADER_LENGTH);
        return SRVERR_TRANSPORT_ERROR;
    }
    b += PACKET_HEADER_LENGTH;
    
    /* read payload */
    if (buf[0] == PACKET_ASCII_ENCODING_CHAR) {
        // ASCII encoded, read until '\r'
        for (; (b - buf) < bufLen; b++) {
            len = _serverRead(b, 1);
            if (len < 0) {
                logERROR(LOGSRC,"Read error");
                return SRVERR_TRANSPORT_ERROR;
            } else
            if (len == 0) {
                // timeout (partial packet read)
                logERROR(LOGSRC,"Timeoout");
                return SRVERR_TIMEOUT;
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
            return SRVERR_PACKET_LENGTH;
        }
    } else
    if (buf[PACKET_HEADER_LENGTH - 1] != 0) {
        // read 'b[2]' bytes
        UInt16 payloadLen = (UInt16)buf[PACKET_HEADER_LENGTH - 1];
        len = _serverRead(b, payloadLen);
        if (len < 0) {
            logERROR(LOGSRC,"Read error");
            return SRVERR_TRANSPORT_ERROR;
        }
        if (len < payloadLen) {
            // timeout / transport-error (partial packet read)
            logERROR(LOGSRC,"Timeout (expected=%d, read=%d)", payloadLen, len);
            return SRVERR_TRANSPORT_ERROR;
        }
        readLen = PACKET_HEADER_LENGTH + len;
    } else {
        readLen = PACKET_HEADER_LENGTH;
    }
    
    /* return length */
    return SRVERR_OK;
    
}

static int _serverParsePacket(Packet_t *pkt, const UInt8 *pktBuf)
{
    
    /* clear packet */
    memset(pkt, 0, sizeof(Packet_t));
    
    /* parse header/data */
    if (*pktBuf == PACKET_ASCII_ENCODING_CHAR) {
        // The packet is assumed to be null-terminated (ie. '\r' was replaced with '0')
        
        /* print packet */
        //logINFO(LOGSRC,"==> %2d] %s\n", strlen(pktBuf), pktBuf); 
        
        /* parse */
        int pktBufLen;
        if (!cksumIsValidCharXOR(pktBuf, &pktBufLen)) {
            // checksum failed: ERROR_PACKET_CHECKSUM
            logERROR(LOGSRC,"Invalid packet checksum");
            serverWriteError("%2x%2x", (UInt32)NAK_PACKET_CHECKSUM, (UInt32)0);
            return SRVERR_CHECKSUM_FAILED;
        } else
        if (pktBufLen < 5) {
            // invalid length: ERROR_PACKET_LENGTH
            logERROR(LOGSRC,"Invalid packet length");
            serverWriteError("%2x%2x", (UInt32)NAK_PACKET_LENGTH, (UInt32)0);
            return SRVERR_PARSE_ERROR;
        } else {
            UInt8 buf[2];
            int hlen = strParseHex(pktBuf + 1, 4, buf, sizeof(buf));
            if (hlen != 2) {
                // header was not parsable: ERROR_PACKET_HEADER
                logERROR(LOGSRC,"Header not parsable");
                serverWriteError("%2x%2x", (UInt32)NAK_PACKET_HEADER, (UInt32)0);
                return SRVERR_PARSE_ERROR;
            } else {
                pkt->hdrType = CLIENT_HEADER_TYPE(buf[0],buf[1]);
                logERROR(LOGSRC,"Header %04X", pkt->hdrType);
                if (pktBufLen > 6) { // $E0FF:XXXX...
                    // encoded character, plus data
                    if (pktBuf[5] == ENCODING_BASE64_CHAR) {
                        int len = (UInt8)base64Decode(pktBuf + 6, pktBufLen - 6, pkt->data, sizeof(pkt->data));
                        pkt->dataLen = (len >= 0)? (UInt8)len : 0;
                    } else
                    if (pktBuf[5] == ENCODING_HEX_CHAR) {
                        int len = (UInt8)strParseHex(pktBuf + 6, pktBufLen - 6, pkt->data, sizeof(pkt->data));
                        pkt->dataLen = (len >= 0)? (UInt8)len : 0;
                    } else
                    if (pktBuf[5] == ENCODING_CSV_CHAR) {
                        // unsupported encoding: ERROR_PACKET_ENCODING
                        // parsing CSV encoded packets is not supported in this implementation
                        logWARNING(LOGSRC,"CSV parsing is not supported.\n");
                        serverWriteError("%2x%2x", (UInt32)NAK_PACKET_ENCODING, (UInt32)pkt->hdrType);
                        return SRVERR_PARSE_ERROR;
                    } else {
                        // unrecognized encoding: ERROR_PACKET_ENCODING
                        serverWriteError("%2x%2x", (UInt32)NAK_PACKET_ENCODING, (UInt32)pkt->hdrType);
                        return SRVERR_PARSE_ERROR;
                    }
                }
            }
        }
        
    } else
    if (*pktBuf == PACKET_HEADER_BASIC) {
        // The packet header is assumed to be a valid length.
        
        /* print packet */
        //UInt8 hex[PACKET_MAX_ENCODED_LENGTH];
        //UInt16 len = (UInt16)pktBuf[2] + 3;
        //logINFO(LOGSRC,"==> %2d] 0x%s\n", len, strEncodeHex(hex, sizeof(hex), pktBuf, len)); 
        
        /* parse into packet */
        pkt->hdrType = CLIENT_HEADER_TYPE(pktBuf[0],pktBuf[1]);
        //logINFO(LOGSRC,"Header %04X", pkt->hdrType);
        pkt->dataLen = (UInt8)pktBuf[2];
        if (pkt->dataLen > 0) {
            memcpy(pkt->data, pktBuf + 3, (int)pkt->dataLen);
        }
        
    } else {
        
        // invalid header: ERROR_PACKET_HEADER
        logERROR(LOGSRC,"Invalid packet header");
        ClientPacketType_t hdrType = CLIENT_HEADER_TYPE(pktBuf[0],pktBuf[1]);
        serverWriteError("%2x%2x", (UInt32)NAK_PACKET_HEADER, (UInt32)hdrType);
        return SRVERR_PARSE_ERROR;
        
    }
    
    /* return packet */
    return SRVERR_OK;
}

int serverReadPacket(Packet_t *pkt)
{
    UInt8 buf[600];
    
    /* read client packet */
    int err = _serverReadPacketBuffer(buf, sizeof(buf));
    if (err != SRVERR_OK) {
        // timeout/error
        return err;
    }
    
    /* parse packet */
    err = _serverParsePacket(pkt, buf);
    //logINFO(LOGSRC,"Client Packet HEADER=%04X", pkt->hdrType);
    return err;

}

// ----------------------------------------------------------------------------

static int _serverWrite(const UInt8 *buf, int bufLen)
{
    
    /* nothing specified to write */
    if (bufLen == 0) {
        return 0;
    }

    /* write packet */
    int len = 0;
    len = socketWriteTCP(&serverSocket, buf, bufLen);
    return len;
    
}

utBool serverWritePacket(Packet_t *pkt)
{
    UInt8 buf[PACKET_MAX_ENCODED_LENGTH];
    Buffer_t bb, *dest = binBuffer(&bb, buf, sizeof(buf), BUFFER_DESTINATION);
    int len = pktEncodePacket(dest, pkt, clientPacketEncoding);
    if (len > 0) {
        if (*buf == PACKET_ASCII_ENCODING_CHAR) {
            logINFO(LOGSRC,"[TX] %.*s", len - 1, buf); 
        } else {
            UInt8 hex[PACKET_MAX_ENCODED_LENGTH];
            logINFO(LOGSRC,"[TX] 0x%s", strEncodeHex(hex, sizeof(hex), buf, len)); 
        }
        int wrtLen = _serverWrite(buf, len);
        if (wrtLen == len) {
            return utTrue;
        } else {
            return utFalse;
        }
    } else {
        logWARNING(LOGSRC,"Invalid packet\n");
        return utFalse;
    }
}

utBool serverWritePacketFmt(ServerPacketType_t pktType, const char *fmt, ...)
{
    Packet_t pkt;
    va_list ap;
    va_start(ap, fmt);
    pktVInit(&pkt, pktType, fmt, ap);
    va_end(ap);
    return serverWritePacket(&pkt);
}

utBool serverWriteError(const char *fmt, ...)
{
    Packet_t pkt;
    va_list ap;
    va_start(ap, fmt);
    pktVInit(&pkt, PKT_SERVER_ERROR, fmt, ap);
    va_end(ap);
    return serverWritePacket(&pkt);
}

// ----------------------------------------------------------------------------

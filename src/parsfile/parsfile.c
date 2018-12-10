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
//  Read/Parse DMTP packets from a file buffer.
// ---
// Change History:
//  2006/07/13  Martin D. Flynn
//     -Initial release
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#define SKIP_TRANSPORT_MEDIA_CHECK // only if TRANSPORT_MEDIA not used in this file 
#include "custom/defaults.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/base64.h"
#include "tools/checksum.h"
#include "tools/io.h"

#include "base/packet.h"

#include "log.h"
#include "parsfile.h"

// ----------------------------------------------------------------------------

static FILE *parseFile = (FILE*)0;

// ----------------------------------------------------------------------------

utBool parseIsOpen()
{
    return (parseFile)? utTrue : utFalse;
}

utBool parseOpen(const char *fileName)
{
    
    /* close, if already open */
    if (parseIsOpen()) {
        // it shouldn't already be open
        fprintf(stderr, "File seems to still be open!\n");
        parseClose();
    }
    
    /* open */
    parseFile = ioOpenStream(fileName, IO_OPEN_READ);
    if (!parseFile) {
        fprintf(stderr, "Unable to open file\n");
        return utFalse;
    }

    /* return successful */
    return utTrue;
    
}

utBool parseClose()
{
    if (parseIsOpen()) {
        ioCloseStream(parseFile);
        parseFile = (FILE*)0;
        return utTrue;
    } else {
        return utFalse;
    }
}

// ----------------------------------------------------------------------------

static long _parseRead(UInt8 *buf, int bufLen)
{
    
    /* no data was requested */
    if (bufLen == 0) {
        fprintf(stderr, "NULL buffer");
        return 0;
    }
    
    /* port open? */
    if (!parseIsOpen()) {
        fprintf(stderr, "Parse file not open!!!");
        return -1;
    }
    
    /* return bytes read */
    long readLen = 0;
    readLen = ioReadStream(parseFile, buf, bufLen);
    return readLen;
    
}

static int _parseReadPacketBuffer(UInt8 *buf, int bufLen)
{
    UInt8 *b = buf;
    long readLen = 0, len;
    
    /* read header */
    // This will read the header/type/length of a binary encoded packet, or
    // the '$' and 2 ASCII hex digits of an ASCII encoded packet.
    len = _parseRead(b, PACKET_HEADER_LENGTH);
    if (len < 0) {
        return SRVERR_TRANSPORT_ERROR;
    } else
    if (len == 0) {
        // timeout/eof
        return SRVERR_TIMEOUT;
    }

    /* make sure entire header has been read */
    if (len < PACKET_HEADER_LENGTH) {
        // partial packet read (just close socket)
        return SRVERR_TRANSPORT_ERROR;
    }
    b += PACKET_HEADER_LENGTH;
    
    /* read payload */
    if (buf[0] == PACKET_ASCII_ENCODING_CHAR) {
        // ASCII encoded, read until '\r'
        for (; (b - buf) < bufLen; b++) {
            len = _parseRead(b, 1);
            if (len < 0) {
                return SRVERR_TRANSPORT_ERROR;
            } else
            if (len == 0) {
                // timeout (partial packet read)
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
            return SRVERR_PACKET_LENGTH;
        }
    } else
    if (buf[PACKET_HEADER_LENGTH - 1] != 0) {
        // read 'b[2]' bytes
        UInt16 payloadLen = (UInt16)buf[PACKET_HEADER_LENGTH - 1];
        len = _parseRead(b, payloadLen);
        if (len < 0) {
            return SRVERR_TRANSPORT_ERROR;
        }
        if (len < payloadLen) {
            // timeout / transport-error (partial packet read)
            return SRVERR_TRANSPORT_ERROR;
        }
        readLen = PACKET_HEADER_LENGTH + len;
    } else {
        readLen = PACKET_HEADER_LENGTH;
    }
    
    /* return length */
    return SRVERR_OK;
    
}

static int _parseParsePacket(Packet_t *pkt, const UInt8 *pktBuf)
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
            return SRVERR_CHECKSUM_FAILED;
        } else
        if (pktBufLen < 5) {
            // invalid length: ERROR_PACKET_LENGTH
            return SRVERR_PARSE_ERROR;
        } else {
            UInt8 buf[2];
            int hlen = strParseHex(pktBuf + 1, 4, buf, sizeof(buf));
            if (hlen != 2) {
                // header was not parsable: ERROR_PACKET_HEADER
                return SRVERR_PARSE_ERROR;
            } else {
                pkt->hdrType = CLIENT_HEADER_TYPE(buf[0],buf[1]);
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
                        return SRVERR_PARSE_ERROR;
                    } else {
                        // unrecognized encoding: ERROR_PACKET_ENCODING
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
        pkt->dataLen = (UInt8)pktBuf[2];
        if (pkt->dataLen > 0) {
            memcpy(pkt->data, pktBuf + 3, (int)pkt->dataLen);
        }
        
    } else {
        
        // invalid header: ERROR_PACKET_HEADER
        return SRVERR_PARSE_ERROR;
        
    }
    
    /* return packet */
    return SRVERR_OK;
}

int parseReadPacket(Packet_t *pkt)
{
    UInt8 buf[600];
    
    /* read client packet */
    int err = _parseReadPacketBuffer(buf, sizeof(buf));
    if (err == SRVERR_TIMEOUT) {
        // eof
        return err;
    } else
    if (err != SRVERR_OK) {
        fprintf(stderr, "Packet read error: %d\n", err);
        return err;
    }
    
    /* parse packet */
    err = _parseParsePacket(pkt, buf);
    if (err != SRVERR_OK) {
        fprintf(stderr, "Packet parse error: %d\n", err);
    }
    return err;

}

// ----------------------------------------------------------------------------

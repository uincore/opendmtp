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
//  Server DMTP packet handler
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
// ----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/bintools.h"
#include "tools/base64.h"
#include "tools/checksum.h"

#include "server/packet.h"
#include "server/log.h"

// ----------------------------------------------------------------------------

/* initialize a format buffer with the packet payload */
FmtBuffer_t *pktFmtBuffer(FmtBuffer_t *bf, Packet_t *pkt)
{
    binFmtBuffer(bf, pkt->data, sizeof(pkt->data), pkt->dataFmt, sizeof(pkt->dataFmt));
    return bf;
}

// ----------------------------------------------------------------------------

/* create/initialize a packet with the specified values */
int pktInit(Packet_t *pkt, ServerPacketType_t pktType, const char *fmt, ...)
{
    if (pkt) {
        va_list ap;
        va_start(ap, fmt);
        int rtn = pktVInit(pkt, pktType, fmt, ap);
        va_end(ap);
        return rtn;
    } else {
        logERROR(LOGSRC,"Null packet");
        return PKTERR_NULL_PACKET;
    }
}

/* create/initialize a packet with the specified argument list */
// All packet initialization should occur here
int pktVInit(Packet_t *pkt, ServerPacketType_t pktType, const char *fmt, va_list ap)
{
    if (pkt) {
        memset(pkt, 0, sizeof(Packet_t));
        pkt->hdrType  = pktType;
        if (fmt && *fmt) {
            FmtBuffer_t bb, *bf = pktFmtBuffer(&bb, pkt);
            int len = binFmtVPrintf(bf, fmt, ap);
            if (len < 0) {
                pkt->dataLen = 0;
                return PKTERR_BIN_PRINTF; // internal error: invalid format, or buffer overflow
            } else {
                pkt->dataLen = BUFFER_DATA_LENGTH(bf); // same as 'len'
                return pkt->dataLen;
            }
        }
        return 0;
    } else {
        return PKTERR_NULL_PACKET;
    }
}

// ----------------------------------------------------------------------------

/* encode the specified packet into a CSV string */
static int _pktEncodeCSVPacket(Buffer_t *dest, Packet_t *pkt)
{
    UInt16 oldLen = BUFFER_DATA_LENGTH(dest);
    Buffer_t bsrc, *src = binBuffer(&bsrc, pkt->data, pkt->dataLen, BUFFER_SOURCE);

    /* header */
    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c", PACKET_ASCII_ENCODING_CHAR);
    binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%04X", (UInt16)pkt->hdrType);
    binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));

    /* CSV (default) */
    const char *v = pkt->dataFmt;
    int err = 0;
    for (;;) {
        
        /* find start of next format */
        while (*v && (*v != '%')) { v++; }
        if (!*v) {
            break; 
        }
        v++;
        
        /* 1+ digits must follow */
        int len = 0;
        if (isdigit(*v)) {
            const char *d = v;
            while (*d && isdigit(*d)) { d++; }
            len = atoi(v);
            v = d;
        } else {
            // invalid format (not a digit)
            err = PKTERR_BIN_FORMAT_DIGIT;
            break;
        }

        /* check remaining data length */
        if (len > BUFFER_DATA_LENGTH(src)) {
            // overflow
            logERROR(LOGSRC,"Length [%d] greater that available data [%d] ...\n", len, BUFFER_DATA_LENGTH(src));
            err = PKTERR_OVERFLOW;
            break;
        }
        
        /* next char must be one of 'iuxsbgz' */
        UInt32 i;
        UInt8 tmp[PACKET_MAX_ENCODED_LENGTH];
        GPSPoint_t gp;
        int actualLen = 0;
        switch (*v) {
            case 'x': case 'X': // unsigned
            case 'u': case 'U': // unsigned
            case 'i': case 'I': // signed
                i = binDecodeInt32(BUFFER_DATA(src), len, ((*v == 'i')||(*v == 'I')));
                if ((*v == 'i')||(*v == 'I')) {
                    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c%ld", ENCODING_CSV_CHAR, i);
                } else
                if ((*v == 'x')||(*v == 'X')) {
                    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c0x%0*lX", ENCODING_CSV_CHAR, len*2, i);
                } else {
                    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c%lu", ENCODING_CSV_CHAR, i);
                }
                binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
                binAdvanceBuffer(src, len);
                break;
            case 's': case 'S':
                actualLen = strLength(BUFFER_DATA(src), len); // lessor of field, or string length
                snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c%.*s", ENCODING_CSV_CHAR, actualLen, BUFFER_DATA(src));
                binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest))); // advance by string length
                binAdvanceBuffer(src, ((actualLen<len)?(actualLen+1):len)); // string length + terminator
                break;
            case 'b': case 'B':
                strEncodeHex(tmp, sizeof(tmp), BUFFER_DATA(src), len);
                snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c0x%s", ENCODING_CSV_CHAR, tmp);
                binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
                binAdvanceBuffer(src, len); // advance length of binary field
                break;
            case 'g': case 'G':
                if ((len >= 6) && (len < 8)) {
                    gpsPointDecode6(&gp, BUFFER_DATA(src));
                    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c%.4lf%c%.4lf", ENCODING_CSV_CHAR, gp.latitude, ENCODING_CSV_CHAR, gp.longitude);
                } else
                if (len >= 8) {
                    gpsPointDecode8(&gp, BUFFER_DATA(src));
                    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c%.6lf%c%.6lf", ENCODING_CSV_CHAR, gp.latitude, ENCODING_CSV_CHAR, gp.longitude);
                } else {
                    // invalid format
                    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c%c", ENCODING_CSV_CHAR, ENCODING_CSV_CHAR);
                }
                binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
                binAdvanceBuffer(src, len); // advance length of GPS field
                break;
            case 'z': case 'Z': // zero filled
                binAdvanceBuffer(src, len); // advance length of ZERO field
                break;
            default:
                // invalid format (unrecognized type)
                err = PKTERR_BIN_FORMAT_CHAR;
                break;
        }

    }
    
    /* error? */
    if (err < 0) {
        return err;
    }

    return BUFFER_DATA_LENGTH(dest) - oldLen;
}

/* encode the specified packet into a hex string */
static int _pktEncodeHEXPacket(Buffer_t *dest, Packet_t *pkt)
{
    UInt16 oldLen = BUFFER_DATA_LENGTH(dest);
    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c", PACKET_ASCII_ENCODING_CHAR);
    binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%04X", (UInt16)pkt->hdrType);
    binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
    if (pkt->dataLen > 0) {
        char hex[PACKET_MAX_ENCODED_LENGTH]; // 256 * 2 == 512
        snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c", ENCODING_HEX_CHAR);
        binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
        strEncodeHex(hex, sizeof(hex), pkt->data, pkt->dataLen);
        snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%s", hex);
        binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
    }
    return BUFFER_DATA_LENGTH(dest) - oldLen;
}

/* encode the specified packet into a Base64 string */
static int _pktEncodeB64Packet(Buffer_t *dest, Packet_t *pkt)
{
    UInt16 oldLen = BUFFER_DATA_LENGTH(dest);
    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c", PACKET_ASCII_ENCODING_CHAR);
    binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%04X", (UInt16)pkt->hdrType);
    binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
    if (pkt->dataLen > 0) {
        char b64[PACKET_MAX_ENCODED_LENGTH]; // ((256 + 2) / 3) * 4 == 344
        snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%c", ENCODING_BASE64_CHAR);
        binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
        base64Encode(b64, sizeof(b64), pkt->data, pkt->dataLen);
        snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%s", b64);
        binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));
    }
    return BUFFER_DATA_LENGTH(dest) - oldLen;
}

/* encode the specified packet into the provided buffer */
int pktEncodePacket(Buffer_t *dest, Packet_t *pkt, PacketEncoding_t encoding)
{
    // Approximate "Safe" minimum 'strLen' values 
    // (assuming all 255 bytes of the data payload are utilized, and the checksum is included):
    // Base64: (6 + (((255 + 2) / 3) * 4) + 3 + 1)         = 350
    // Hex   : (6 + (2 * 255) + 3 + 1)                     = 520
    // CSV   : (6 + ((2 * 255) + (3 * (#Fields))) + 3 + 1) = ~580 (20 fields)
    
    /* encoding */
    // Packet encoding override can occur here if needed
    PacketEncoding_t enc = encoding;

    /* binary encoding special case */
    if (ENCODING_VALUE(enc) == ENCODING_BINARY) {
        if ((3 + pkt->dataLen) <= BUFFER_DATA_SIZE(dest)) {
            int len = binPrintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "%2x%1x%*b", 
                (UInt32)pkt->hdrType, (UInt32)pkt->dataLen, (int)pkt->dataLen, pkt->data);
            if (len >= 0) {
                binAdvanceBuffer(dest, len);
                return BUFFER_DATA_LENGTH(dest);
            } else {
                return PKTERR_OVERFLOW;
            }
        } else {
            return PKTERR_OVERFLOW;
        }
    }
    
    /* ascii encode */
    UInt8 *str = BUFFER_DATA(dest);        // point to start of ASCII encoded record
    int len = 0; // return length of this encoded packet (or an error)
    switch (ENCODING_VALUE(enc)) {
            
        case ENCODING_CSV:
            if (pkt->dataFmt && *(pkt->dataFmt)) {
                len = _pktEncodeCSVPacket(dest, pkt); // format present, encode as CSV
            } else {
                len = _pktEncodeHEXPacket(dest, pkt); // format missing, encode as HEX
            }
            break;
        
        case ENCODING_BASE64:
            len = _pktEncodeB64Packet(dest, pkt);
            break;
            
        default: // default to hex
        case ENCODING_HEX:
            len = _pktEncodeHEXPacket(dest, pkt);
            break;

    }
    
    /* error? */
    if (len < 0) {
        return len;
    }
    // we assume that the packet has been properly encoded at this point

    /* include checksum? */
    if (ENCODING_IS_CHECKSUM(enc)) {
        if (3 > BUFFER_DATA_SIZE(dest)) {
            // internal error: not likely
            logERROR(LOGSRC,"'3' greater that available data ...\n");
            return PKTERR_OVERFLOW;
        } else {
            ChecksumXOR_t cksum = 0x00;
            cksumCalcCharXOR(str + 1, &cksum);
            sprintf(&str[len], "*%02X", (UInt16)cksum);
            binAdvanceBuffer(dest, 3);
        }
    }
    
    /* ascii line terminator */
    snprintf(BUFFER_DATA(dest), BUFFER_DATA_SIZE(dest), "\r"); // \r\n
    binAdvanceBuffer(dest, strlen(BUFFER_DATA(dest)));

    /* return total length */
    return BUFFER_DATA_LENGTH(dest);
    
}

// ----------------------------------------------------------------------------

/* print/log a packet */
void pktPrintPacket(Packet_t *pkt, const char *msg, PacketEncoding_t encoding)
{
    UInt8 buf[PACKET_MAX_ENCODED_LENGTH];
    Buffer_t bb, *dest = binBuffer(&bb, buf, sizeof(buf), BUFFER_DESTINATION);
    if (!msg) { msg = ""; }
    int len = pktEncodePacket(dest, pkt, encoding);
    if (len >= 0) {
        if (*buf == PACKET_ASCII_ENCODING_CHAR) {
            logDEBUG(LOGSRC,"%s %.*s [%d]", msg, len - 1, buf, len); 
        } else {
            UInt8 hex[PACKET_MAX_ENCODED_LENGTH];
            logDEBUG(LOGSRC,"%s 0x%s [%d]", msg, strEncodeHex(hex, sizeof(hex), buf, len), len); 
        }
    } else {
        logWARNING(LOGSRC,"%s <InvalidPacket> [%d]", msg, len);
    }
}

// ----------------------------------------------------------------------------

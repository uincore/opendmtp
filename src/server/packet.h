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

#ifndef _PACKET_H
#define _PACKET_H

#include "tools/stdtypes.h"
#include "tools/bintools.h"
#include "tools/gpstools.h"

#include "server/defaults.h"

// ----------------------------------------------------------------------------

typedef UInt8 PacketType_t;

// ----------------------------------------------------------------------------

#define PACKET_ASCII_ENCODING_CHAR      '$'     // 0x24
#define PACKET_ASCII_ENCODING_EOL       '\r'    // '\n'    // '\r'

#define PACKET_HEADER_BASIC             ((UInt8)0xE0)   // header
#define PACKET_HEADER_CLIENT_BASIC      ((UInt8)0xE0)   // header
#define PACKET_HEADER_SERVER_BASIC      ((UInt8)0xE0)   // header
#define PACKET_HEADER_LENGTH            3

#define CLIENT_HEADER_TYPE(H,T)         (ClientPacketType_t)(((H) << 8) | ((T) & 0xFF))
#define CLIENT_PACKET_HEADER(HT)        (UInt8)(((HT) >> 8) & 0xFF) // PACKET_HEADER_CLIENT_BASIC
#define CLIENT_PACKET_TYPE(HT)          (UInt8)((HT) & 0xFF)

#define SERVER_HEADER_TYPE(H,T)         (((H) << 8) | ((T) & 0xFF))
#define SERVER_PACKET_HEADER(HT)        (UInt8)(((HT) >> 8) & 0xFF) // PACKET_HEADER_SERVER_BASIC
#define SERVER_PACKET_TYPE(HT)          (UInt8)((HT) & 0xFF)

// ----------------------------------------------------------------------------
// Client originated packets

#define PKT_CLIENT_HEADER               ((UInt16)PACKET_HEADER_CLIENT_BASIC << 8)
enum ClientPacketType_enum {
    
    // dialog packets
    PKT_CLIENT_EOB_DONE                 = PKT_CLIENT_HEADER|0x00,    // End of block/transmission, "no more to say"
    PKT_CLIENT_EOB_MORE                 = PKT_CLIENT_HEADER|0x01,    // End of block/transmission, "I have more to say"
    
    // identification packets
    PKT_CLIENT_UNIQUE_ID                = PKT_CLIENT_HEADER|0x11,    // Unique identifier
    PKT_CLIENT_ACCOUNT_ID               = PKT_CLIENT_HEADER|0x12,    // Account identifier
    PKT_CLIENT_DEVICE_ID                = PKT_CLIENT_HEADER|0x13,    // Device identifier

    // standard fixed format event packets
    PKT_CLIENT_FIXED_FMT_STD            = PKT_CLIENT_HEADER|0x30,    // Standard GPS
    PKT_CLIENT_FIXED_FMT_HIGH           = PKT_CLIENT_HEADER|0x31,    // High Resolution GPS
    PKT_CLIENT_FIXED_FORMAT_F           = PKT_CLIENT_HEADER|0x3F,    // (last fixed format)

    // DMT service provider formats
    PKT_CLIENT_DMTSP_FORMAT_0           = PKT_CLIENT_HEADER|0x50,    // Service provider format #0
    PKT_CLIENT_DMTSP_FORMAT_1           = PKT_CLIENT_HEADER|0x51,    // Service provider format #1
    PKT_CLIENT_DMTSP_FORMAT_2           = PKT_CLIENT_HEADER|0x52,    // Service provider format #2
    PKT_CLIENT_DMTSP_FORMAT_3           = PKT_CLIENT_HEADER|0x53,    // Service provider format #3
    PKT_CLIENT_DMTSP_FORMAT_4           = PKT_CLIENT_HEADER|0x54,    // Service provider format #4
    PKT_CLIENT_DMTSP_FORMAT_5           = PKT_CLIENT_HEADER|0x55,    // Service provider format #5
    PKT_CLIENT_DMTSP_FORMAT_6           = PKT_CLIENT_HEADER|0x56,    // Service provider format #6
    PKT_CLIENT_DMTSP_FORMAT_7           = PKT_CLIENT_HEADER|0x57,    // Service provider format #7
    PKT_CLIENT_DMTSP_FORMAT_8           = PKT_CLIENT_HEADER|0x58,    // Service provider format #8
    PKT_CLIENT_DMTSP_FORMAT_9           = PKT_CLIENT_HEADER|0x59,    // Service provider format #9
    PKT_CLIENT_DMTSP_FORMAT_A           = PKT_CLIENT_HEADER|0x5A,    // Service provider format #A
    PKT_CLIENT_DMTSP_FORMAT_B           = PKT_CLIENT_HEADER|0x5B,    // Service provider format #B
    PKT_CLIENT_DMTSP_FORMAT_C           = PKT_CLIENT_HEADER|0x5C,    // Service provider format #C
    PKT_CLIENT_DMTSP_FORMAT_D           = PKT_CLIENT_HEADER|0x5D,    // Service provider format #D
    PKT_CLIENT_DMTSP_FORMAT_E           = PKT_CLIENT_HEADER|0x5E,    // Service provider format #E
    PKT_CLIENT_DMTSP_FORMAT_F           = PKT_CLIENT_HEADER|0x5F,    // Service provider format #F

    // custom format event packets
    PKT_CLIENT_CUSTOM_FORMAT_0          = PKT_CLIENT_HEADER|0x70,    // Custom format #0
    PKT_CLIENT_CUSTOM_FORMAT_1          = PKT_CLIENT_HEADER|0x71,    // Custom format #1
    PKT_CLIENT_CUSTOM_FORMAT_2          = PKT_CLIENT_HEADER|0x72,    // Custom format #2
    PKT_CLIENT_CUSTOM_FORMAT_3          = PKT_CLIENT_HEADER|0x73,    // Custom format #3
    PKT_CLIENT_CUSTOM_FORMAT_4          = PKT_CLIENT_HEADER|0x74,    // Custom format #4
    PKT_CLIENT_CUSTOM_FORMAT_5          = PKT_CLIENT_HEADER|0x75,    // Custom format #5
    PKT_CLIENT_CUSTOM_FORMAT_6          = PKT_CLIENT_HEADER|0x76,    // Custom format #6
    PKT_CLIENT_CUSTOM_FORMAT_7          = PKT_CLIENT_HEADER|0x77,    // Custom format #7
    PKT_CLIENT_CUSTOM_FORMAT_8          = PKT_CLIENT_HEADER|0x78,    // Custom format #8
    PKT_CLIENT_CUSTOM_FORMAT_9          = PKT_CLIENT_HEADER|0x79,    // Custom format #9
    PKT_CLIENT_CUSTOM_FORMAT_A          = PKT_CLIENT_HEADER|0x7A,    // Custom format #A
    PKT_CLIENT_CUSTOM_FORMAT_B          = PKT_CLIENT_HEADER|0x7B,    // Custom format #B
    PKT_CLIENT_CUSTOM_FORMAT_C          = PKT_CLIENT_HEADER|0x7C,    // Custom format #C
    PKT_CLIENT_CUSTOM_FORMAT_D          = PKT_CLIENT_HEADER|0x7D,    // Custom format #D
    PKT_CLIENT_CUSTOM_FORMAT_E          = PKT_CLIENT_HEADER|0x7E,    // Custom format #E
    PKT_CLIENT_CUSTOM_FORMAT_F          = PKT_CLIENT_HEADER|0x7F,    // Custom format #F

    // Property packet
    PKT_CLIENT_PROPERTY_VALUE           = PKT_CLIENT_HEADER|0xB0,    // Property value
    
    // Custom format packet
    PKT_CLIENT_FORMAT_DEF_24            = PKT_CLIENT_HEADER|0xCF,    // Custom format definition (24 bit field def)

    // Diagnostic/Error packets
    PKT_CLIENT_DIAGNOSTIC               = PKT_CLIENT_HEADER|0xD0,    // Diagnostic codes
    PKT_CLIENT_ERROR                    = PKT_CLIENT_HEADER|0xE0,    // Error codes
    
};
typedef enum ClientPacketType_enum ClientPacketType_t;

// ----------------------------------------------------------------------------
// Server originated packets

#define PKT_SERVER_HEADER               ((UInt16)PACKET_HEADER_SERVER_BASIC << 8)
enum ServerPacketType_enum {
    
    /* End-Of-Block packets */
    PKT_SERVER_EOB_DONE                 = PKT_SERVER_HEADER|0x00,    // ""       : End of transmission, query response
    PKT_SERVER_EOB_SPEAK_FREELY         = PKT_SERVER_HEADER|0x01,    // ""       : End of transmission, speak freely

    // Acknowledge packet
    PKT_SERVER_ACK                      = PKT_SERVER_HEADER|0xA0,    // "%*u"    : Acknowledge

    // Property packets
    PKT_SERVER_GET_PROPERTY             = PKT_SERVER_HEADER|0xB0,    // "%2u"    : Get property
    PKT_SERVER_SET_PROPERTY             = PKT_SERVER_HEADER|0xB1,    // "%2u%*b" : Set property
    // File upload packets
    PKT_SERVER_FILE_UPLOAD              = PKT_SERVER_HEADER|0xC0,    // "%1x%3u%*b" : File upload

    // Error packets
    PKT_SERVER_ERROR                    = PKT_SERVER_HEADER|0xE0,    // "%2u"    : NAK/Error codes
    
    // End-Of-Transmission
    PKT_SERVER_EOT                      = PKT_SERVER_HEADER|0xFF,    // ""       : End transmission (socket will be closed)

};
typedef enum ServerPacketType_enum ServerPacketType_t;

// ----------------------------------------------------------------------------

typedef struct {
    ServerPacketType_t      pktType;
    char                    *format;
    UInt16                  flags;
} ServerPacketEntry_t;

// ----------------------------------------------------------------------------

#define PACKET_MAX_ENCODED_LENGTH       600 // (with excess) largest possible ASCII encoded packet

/* set Packet payload configuration */
// set to maximum number of separate fields used in packets (should be at least 10)
#define PACKET_MAX_FIELD_COUNT          16
// set to maximum size of largest possible payload (must be at least 32, but no larger than 255)
#define PACKET_MAX_PAYLOAD_LENGTH       255 // (binary packet size)

// ----------------------------------------------------------------------------

#define ENCODING_BASE64_CHAR        '='
#define ENCODING_HEX_CHAR           ':'
#define ENCODING_CSV_CHAR           ','

#define ENCODING_ASCII_CKSUM_       (0x8000)
#define ENCODING_IS_CHECKSUM(E)     (((E) & ENCODING_ASCII_CKSUM_) != 0)
#define ENCODING_IS_ASCII(E)        (ENCODING_VALUE(E) != ENCODING_BINARY)
#define ENCODING_VALUE(E)           ((E) & 0x0F)
#define ENCODING_CHECKSUM(E)        (ENCODING_VALUE(E) | ENCODING_ASCII_CKSUM_)

#define ENCODING_MASK(E)            (1 << ENCODING_VALUE(E))

#define ENCODING_BINARY             0    // server must support
#define ENCODING_BINARY_MASK        ENCODING_MASK(ENCODING_BINARY)

#define ENCODING_BASE64             1    // server must support
#define ENCODING_BASE64_CKSUM       ENCODING_CHECKSUM(ENCODING_BASE64)
#define ENCODING_BASE64_MASK        ENCODING_MASK(ENCODING_BASE64)

#define ENCODING_HEX                2    // server must support
#define ENCODING_HEX_CKSUM          ENCODING_CHECKSUM(ENCODING_HEX)
#define ENCODING_HEX_MASK           ENCODING_MASK(ENCODING_HEX)

#define ENCODING_CSV                3    // server support optional
#define ENCODING_CSV_CKSUM          ENCODING_CHECKSUM(ENCODING_CSV)
#define ENCODING_CSV_MASK           ENCODING_MASK(ENCODING_CSV)

#define ENCODING_UNDEFINED          (0xFFFF)

#define ENCODING_REQUIRED_MASK      (ENCODING_BINARY_MASK | ENCODING_BASE64_MASK | ENCODING_HEX_MASK)
#define ENCODING_ALL_MASK           (ENCODING_REQUIRED_MASK | ENCODING_CSV_MASK)

typedef UInt16 PacketEncoding_t;

// ----------------------------------------------------------------------------
// Internal packet function error codes

#define PKTERR_NULL_PACKET          -301    // internal
#define PKTERR_BIN_PRINTF           -302    // internal: buffer overflow or invalid format
#define PKTERR_ENCODING             -303    // internal: packet encoding error
#define PKTERR_OVERFLOW             -304    // internal: buffer overflow
#define PKTERR_BIN_FORMAT_DIGIT     -331    // internal: invalid format digit
#define PKTERR_BIN_FORMAT_CHAR      -332    // internal: invalid format char

// ----------------------------------------------------------------------------

typedef struct {
    ClientPacketType_t  hdrType;    // packet type (ie. EVENT_FIXED_FORMAT_TYPE)
    char                dataFmt[(PACKET_MAX_FIELD_COUNT * 3) + 3]; // null terminated
    UInt8               dataLen;    // length of data in payload
    UInt8               data[PACKET_MAX_PAYLOAD_LENGTH];
} Packet_t;

// ----------------------------------------------------------------------------

FmtBuffer_t *pktFmtBuffer(FmtBuffer_t *bf, Packet_t *pkt);

int pktInit(Packet_t *pkt, ServerPacketType_t pktType, const char *fmt, ...);
int pktVInit(Packet_t *pkt, ServerPacketType_t pktType, const char *fmt, va_list ap);

int pktEncodePacket(Buffer_t *dest, Packet_t *pkt, PacketEncoding_t encoding);

void pktPrintPacket(Packet_t *pkt, const char *msg, PacketEncoding_t encoding);

// ----------------------------------------------------------------------------

#endif

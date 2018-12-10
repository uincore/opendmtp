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

#ifndef _TRANSPORT_H
#define _TRANSPORT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "custom/defaults.h"
#include "tools/stdtypes.h"

// ----------------------------------------------------------------------------
// Transport Media

// There are no external dependencies on these enum values, thus they may change
// at any time as new media support is added.
enum TransportMedia_enum {
    MEDIA_UNKNOWN               = 0,
    MEDIA_FILE                  = 1,
    MEDIA_SOCKET                = 2,
    MEDIA_SERIAL                = 3,
    MEDIA_GPRS                  = 4,
};

typedef enum TransportMedia_enum TransportMedia_t;

// ----------------------------------------------------------------------------
// Transport type

enum TransportType_enum {
    TRANSPORT_NONE              = 0,
    TRANSPORT_SIMPLEX           = 1,
    TRANSPORT_DUPLEX            = 2
};

typedef enum TransportType_enum TransportType_t;

// ----------------------------------------------------------------------------
// Transport attributes

typedef struct {
    TransportMedia_t    media;
    const char          *name;
    utBool              (*xportIsOpen)(void);
    utBool              (*xportOpen)(TransportType_t type);
    utBool              (*xportClose)(utBool sendUDP);
    int                 (*xportReadPacket)(UInt8 *buf, int bufLen);
    void                (*xportReadFlush)(void);
    int                 (*xportWritePacket)(const UInt8 *buf, int bufLen);
} TransportFtns_t;

// ----------------------------------------------------------------------------
// Primary transport initializer

/* primary transport initialization */
#if   defined(TRANSPORT_MEDIA_SOCKET)
#  define XPORT_INIT_PRIMARY    socket_transportInitialize
#elif defined(TRANSPORT_MEDIA_FILE)
#  define XPORT_INIT_PRIMARY    file_transportInitialize
#elif defined(TRANSPORT_MEDIA_SERIAL)
#  define XPORT_INIT_PRIMARY    serial_transportInitialize
#elif defined(TRANSPORT_MEDIA_GPRS)
#  define XPORT_INIT_PRIMARY    gprs_transportInitialize
#endif

// ----------------------------------------------------------------------------
// Socket transport
#if defined(TRANSPORT_MEDIA_SOCKET)
#  include "custom/transport/socket.h"
#endif

// ----------------------------------------------------------------------------
// File transport
#if defined(TRANSPORT_MEDIA_FILE)
#  include "custom/transport/file.h"
#endif

// ----------------------------------------------------------------------------
// GPRS modem transport
#if defined(TRANSPORT_MEDIA_GPRS)
#  include "custom/transport/gprs.h"
#endif

// ----------------------------------------------------------------------------
// Serial/Bluetooth transport
#if defined(TRANSPORT_MEDIA_SERIAL) || defined(SECONDARY_SERIAL_TRANSPORT)
#  include "custom/transport/serial.h"
#endif

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif

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
//  Machine interface to 'file' data transport.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "custom/log.h"

#include "custom/transport/file.h"

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/bintools.h"
#include "tools/io.h"

#include "base/props.h"
#include "base/propman.h"
#include "base/packet.h"

// ----------------------------------------------------------------------------

/* transport structure */
typedef struct {
    TransportType_t             type;
    utBool                      isOpen;
    FILE                        *file;
} FileTransport_t;

static FileTransport_t          fileXport;

// ----------------------------------------------------------------------------

static TransportFtns_t          fileXportFtns = { MEDIA_FILE, "XportFile" };

// The size of the datagram buffer is arbitrary, however the amount of 
// data transmitted in a single datagram should not be larger than the MTU
// to avoid possible fragmentation which could result in a higher possibility
// of data loss.
static UInt8                    fileDatagramData[2000];
static Buffer_t                 fileDatagramBuffer;

// ----------------------------------------------------------------------------

static const char *file__transportTypeName(TransportType_t type)
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
static void file_transportReadFlush()
{
    // NO-OP
}

/* return true if transport is open */
static utBool file_transportIsOpen()
{
    // this may be called by threads other than the protocol thread.
    return fileXport.isOpen;
}

/* close transport */
static utBool file_transportClose(utBool sendUDP)
{
    if (fileXport.isOpen) {
        logDEBUG(LOGSRC,"%s Transport close ...\n\n", file__transportTypeName(fileXport.type));
        utBool rtn = utTrue;

        /* check for datagram transmission */
        if (sendUDP && (fileXport.type == TRANSPORT_SIMPLEX)) {
            // write queued datagram to file
            Buffer_t *bf = &fileDatagramBuffer;
            UInt8 *data = BUFFER_PTR(bf);
            int dataLen = BUFFER_DATA_LENGTH(bf);
            long len = ioWriteStream(fileXport.file, data, dataLen);
            rtn = (len < dataLen)? utFalse : utTrue;
        }
        
        /* close file */
        ioCloseStream(fileXport.file);
        fileXport.file = (FILE*)0;
        
        /* transport is closed */
        fileXport.type   = TRANSPORT_NONE;
        fileXport.isOpen = utFalse;
        
        /* reset buffers */
        binResetBuffer(&fileDatagramBuffer);
        
        return rtn;
        
    } else {
        
        /* wasn't open */
        return utFalse;
        
    }
}

/* open transport */
static utBool file_transportOpen(TransportType_t type)
{
    
    /* close, if already open */
    if (fileXport.isOpen) {
        // it shouldn't already be open
        logWARNING(LOGSRC,"Transport seems to still be open!");
        file_transportClose(utFalse);
    }

    // flat-file transport (should be TRANSPORT_SIMPLEX only)
    const char *outFileName = propGetString(PROP_CFG_XPORT_PORT, "");
    if (!outFileName || !*outFileName) { outFileName = "dmtpdata.dmt"; }
    switch (type) {
        case TRANSPORT_SIMPLEX:
            fileXport.file = ioOpenStream(outFileName, IO_OPEN_APPEND);
            break;
        case TRANSPORT_DUPLEX:
            logWARNING(LOGSRC,"'Duplex' should be disabled for file transport!");
            fileXport.file = ioOpenStream(outFileName, IO_OPEN_APPEND);
            break;
        default:
            return utFalse;
    }
    if (!fileXport.file) {
        return utFalse;
    }
    fileXport.type   = type;
    fileXport.isOpen = utTrue;
    // fall through to 'true' return below

    /* reset buffers and return success */
    binResetBuffer(&fileDatagramBuffer);
    logDEBUG(LOGSRC,"Openned %s Transport ...\n", file__transportTypeName(type));
    return fileXport.isOpen;
    
}

// ----------------------------------------------------------------------------

/* read packet from transport */
static int file_transportReadPacket(UInt8 *buf, int bufLen)
{
    /* return bytes read */
    int readLen = 0;
    // file transport should be Simplex only (ie. we shouldn't be here)
    // try to handle this case by alternating between returning an ACK and EOT packets
    static UInt32 alt = 0L;
    buf[0] = PACKET_HEADER_BASIC;
    buf[1] = (alt++ & 1L)? PKT_SERVER_EOT : PKT_SERVER_ACK; // odd=EOT, even=ACK
    buf[2] = 0;
    readLen = 3;
    return readLen;
}

// ----------------------------------------------------------------------------

/* write packet to transport */
static int file_transportWritePacket(const UInt8 *buf, int bufLen)
{
    
    /* transport not open? */
    if (!fileXport.isOpen) {
        logERROR(LOGSRC,"Transport is not open");
        return -1;
    }

    /* nothing specified to write */
    if (bufLen == 0) {
        return 0;
    }

    /* write data per transport type */
    int len = 0;
    switch (fileXport.type) {
        case TRANSPORT_SIMPLEX:
            // queue data until the close
            len = binBufPrintf(&fileDatagramBuffer, "%*b", bufLen, buf);
            return len;
        case TRANSPORT_DUPLEX:
            // file transport should be Simplex only, but handle this case anyway
            len = ioWriteStream(fileXport.file, buf, bufLen);
            return len;
        default:
            logERROR(LOGSRC,"Unknown transport type %d\n", fileXport.type);
            return -1;
    }
    
}

// ----------------------------------------------------------------------------

/* initialize transport */
const TransportFtns_t *file_transportInitialize()
{
    
    /* init transport structure */
    memset(&fileXport, 0, sizeof(FileTransport_t));
    fileXport.type   = TRANSPORT_NONE;
    fileXport.isOpen = utFalse;
    
    /* init datagram buffer */
    binBuffer(&fileDatagramBuffer, fileDatagramData, sizeof(fileDatagramData), BUFFER_DESTINATION);

    /* init function structure */
    fileXportFtns.xportIsOpen       = &file_transportIsOpen;
    fileXportFtns.xportOpen         = &file_transportOpen;
    fileXportFtns.xportClose        = &file_transportClose;
    fileXportFtns.xportReadFlush    = &file_transportReadFlush;
    fileXportFtns.xportReadPacket   = &file_transportReadPacket;
    fileXportFtns.xportWritePacket  = &file_transportWritePacket;
    return &fileXportFtns;

}

// ----------------------------------------------------------------------------

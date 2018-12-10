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
//  DMTP communication protocol manager
//  Manages the dialog between the client and server during connections.
// Notes:
//  - Thread support would be a useful addition to this module.  This would allow
//  an immediate return to the main GPS checking loop without blocking.  This
//  reference implementation will block until data communication has been made
//  and data has been transferred.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/02/09  Martin D. Flynn
//     -Send ERROR_GPS_EXPIRED/ERROR_GPS_FAILURE is GPS fix has expired
//  2006/04/02  Martin D. Flynn
//     -Clear 'sendIdentification' after sending UniqueID
//  2006/05/07  Martin D. Flynn
//     -Add support for uploading GeoZones
//  2006/05/26  Martin D. Flynn
//     -Allow UniqueID to be >= 4
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
//     -Fix 'tight loop' issue when 'serial/bluetooth' transport goes down
//      (propagate error up from 'protocolReadServerPacket').
//     -Return ERROR_COMMAND_ERROR for PROP_ERROR_COMMAND_ERROR on 'set' property.
//     -Switched to generic thread access methods in 'tools/threads.h'
//     -Flush server input buffers on received EOB-done (serial transport only)
//     -Separated all protocol variables into a separate structure to allow
//      multiple simultaneous connecting protocols.
//     -Changed PKT_SERVER_GET_PROPERTY to retrieve a single property value.
//      The remaining bytes behind the property key can now be used as arguments
//      on the property 'get' (via 'PROP_REFRESH_GET').
//     -Made some significant changes to this module to allow multiple virtual
//      protocol "instances".  (While this would have been easier in 'C++', this
//      has been implemented in 'C' to allow compiling on any 'C' capable platform.)
//  2007/02/05  Martin D. Flynn
//     -Fixed Fletcher checksum packet generation that inappropriately used
//      "sizeof(ChecksumFletcher_t)" to determine the length of the checksum value.
//      This created a compiler dependency that could return a length of 4, instead
//      of the corrent value '2'. (Thanks to Tomasz Rostanski for catching this!).
//  2007/04/28  Martin D. Flynn
//     -Moved the following functions from 'events.c' to this modules to support 
//      dual transport: evGetEventQueue, evGetHighestPriority,
//      evEnableOverwrite, evAcknowledgeFirst, evAcknowledgeToSequence
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "custom/log.h"
#include "custom/transport.h"
#include "custom/gps.h"

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/base64.h"
#include "tools/checksum.h"

#include "base/propman.h"
#include "base/events.h"
#include "base/cerrors.h"
#include "base/serrors.h"
#include "base/pqueue.h"
#include "base/accting.h"
#include "base/packet.h"
#include "base/protocol.h"

#if defined(ENABLE_UPLOAD)
#  include "base/upload.h"
#endif

#include "modules/motion.h"

// ----------------------------------------------------------------------------
// thread control

#ifdef PROTOCOL_THREAD
//#warning Protocol thread support enabled
#define PROTOCOL_LOCK(PV)       MUTEX_LOCK(&((PV)->protocolMutex));
#define PROTOCOL_UNLOCK(PV)     MUTEX_UNLOCK(&((PV)->protocolMutex));
#define PROTOCOL_WAIT(PV)       CONDITION_WAIT(&((PV)->protocolCond), &((PV)->protocolMutex));
#define PROTOCOL_NOTIFY(PV)     CONDITION_NOTIFY(&((PV)->protocolCond));
#else
//#warning Protocol thread support disabled
#define PROTOCOL_LOCK(PV)
#define PROTOCOL_UNLOCK(PV)
#define PROTOCOL_WAIT(PV)
#define PROTOCOL_NOTIFY(PV)
#endif

// ----------------------------------------------------------------------------

/* severe error counts */
#define MAX_SEVERE_ERRORS               10
#define EXCESSIVE_SEVERE_ERRORS         15

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// '_protoGetVars' allows separating the protocol handler into separate virtual
// instances. Ideally, this should be a C++ class, but this has been implemented
// in 'C' to allow this module to compile on any 'C' compatible platform.

static ProtocolVars_t   protoVars[MAX_SIMULTANEOUS_PROTOCOLS];

static ProtocolVars_t *_protoGetVars(const char *fn, int line, int protoNdx)
{
    if (protoNdx <= 0) {
        // primary protocol
        return &protoVars[0];
    } else
    if (protoNdx < MAX_SIMULTANEOUS_PROTOCOLS) {
        // secondary protocol, etc.
        //logINFO(LOGSRC,"Secondary protocol #%d [%s:%d] ...", protoNdx, fn, line);
        return &protoVars[protoNdx];
    } else {
        // invalid index, return last protocol
        //logWARNING(LOGSRC,"Secondary protocol out-of-bounds #%d [%s:%d] ...", protoNdx, fn, line);
        return &protoVars[MAX_SIMULTANEOUS_PROTOCOLS - 1];
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

#if defined(SECONDARY_SERIAL_TRANSPORT)
PacketQueue_DEFINE(secondaryQueue,SECONDARY_EVENT_QUEUE_SIZE);
#endif

/* get event queue (evGetEventQueue) */
static PacketQueue_t *_protocolGetEventQueue(ProtocolVars_t *pv)
{
#if defined(SECONDARY_SERIAL_TRANSPORT)
    return pv->isPrimary? evGetEventQueue() : &secondaryQueue;
#else
    return evGetEventQueue();
#endif
}

PacketQueue_t *protocolGetEventQueue(int protoNdx)
{
    ProtocolVars_t *pv = _protoGetVars(LOGSRC,protoNdx);
    return _protocolGetEventQueue(pv);
}

/* return the highest priority event in the queue (evGetHighestPriority) */
static PacketPriority_t _protocolGetHighestPriority(ProtocolVars_t *pv)
{
    PacketQueue_t *evQue = _protocolGetEventQueue(pv);
    if (evQue) {
        return pqueGetHighestPriority(evQue);
    } else {
        return PRIORITY_NONE;
    }
}

/* enable overwriting of the oldest event if the event queue fills up (evEnableOverwrite) */
static void _protocolEnableOverwrite(ProtocolVars_t *pv, utBool overwrite)
{
    PacketQueue_t *evQue = _protocolGetEventQueue(pv);
    if (evQue) {
        pqueEnableOverwrite(evQue, overwrite);
    }
}

/* acknowledge events up to and including the specified sequence (evAcknowledgeToSequence) */
static utBool _protocolAcknowledgeToSequence(ProtocolVars_t *pv, UInt32 sequence)
{
    // 'primary' transport events only
    utBool didAck = utFalse;
    utBool ackAll = (sequence == SEQUENCE_ALL)? utTrue : utFalse;
    PacketQueue_t *eventQueue = _protocolGetEventQueue(pv);
    if (eventQueue && (ackAll || pqueHasSentPacketWithSequence(eventQueue,sequence))) {
        PacketQueueIterator_t evi;
        pqueGetIterator(eventQueue, &evi);
        for (;;) {
            // As we iterate through the event packets, we can assume the following:
            // - If we get to a null packet, we are finished with the list
            // - If we find a packet that hasn't been sent, then all following packets have 
            //   also not been sent.
            // - Once we find the 'first' matching sequence, we delete it then stop. This is
            //   safer that deleting the last matching sequence.  (Note: multiple possible
            //   matching sequence numbers can occur if the byte length of the sequence 
            //   number is 1 (ie. 0 to 255), and more than 255 events are currently in the 
            //   event packet queue.  Granted, an unlikely situation, but it can occur.)
            // - No packet->sequence will ever match SEQUENCE_ALL (see 'ackAll').
            Packet_t *pkt = pqueGetNextPacket((Packet_t*)0, &evi);
            if (!pkt || !pkt->sent) {
                //logWARNING(LOGSRC,"Stop at first non-sent packet");
                break;  // stop at first null or non-sent packet
            }
            pqueDeleteFirstEntry(eventQueue);
            didAck = utTrue;
            if (ackAll) {
                // ackowledge all sent packets
                continue;
            } else
            if (pkt->sequence == SEQUENCE_ALL) {
                // This condition can not (should not) occur.
                // We don't know what the real sequence of the packet is.
                // it's safer to stop here.
                break;
            } else
            if (pkt->sequence != (sequence & SEQUENCE_MASK(pkt->seqLen))) {
                // no match yet
                continue;
            }
            break; // stop when sequence matches
        }
    } else {
        logERROR(LOGSRC,"No packet with sequence: 0x%04lX", (UInt32)sequence);
    }
    return didAck;
}

/* acknowledge first sent event (evAcknowledgeFirst) */
static utBool _protocolAcknowledgeFirst(ProtocolVars_t *pv)
{
    PacketQueue_t *evQue = _protocolGetEventQueue(pv);
    if (evQue) {
        UInt32 seq = pqueGetFirstSentSequence(evQue); // return 'SEQUENCE_ALL' if no first event
        if (seq != SEQUENCE_ALL) {
            return _protocolAcknowledgeToSequence(pv,seq);
        } else {
            return utFalse;
        }
    } else {
        return utFalse;
    }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

/* set current session packet encoding */
static void _protocolSetSessionEncoding(ProtocolVars_t *pv, TransportType_t xportType, PacketEncoding_t enc)
{
    // Since this reference implementation does not support received CSV packet encoding
    // from the server, we must make sure that our first packet to the server is NOT
    // CSV encoded if we are transmitting via TRANSPORT_DUPLEX
    pv->sessionEncoding         = enc;
    pv->sessionEncodingChanged  = utFalse;
    pv->sessionFirstEncoding    = pv->sessionEncoding;
    if (xportType == TRANSPORT_DUPLEX) {
        if (ENCODING_VALUE(pv->sessionEncoding) == ENCODING_CSV) {
            PacketEncoding_t dft = ENCODING_BASE64;
            pv->sessionFirstEncoding = ENCODING_IS_CHECKSUM(enc)? ENCODING_CHECKSUM(dft) : dft;
        }
    }
}

/* return true if specified encoding is supported */
static PacketEncoding_t _protocolGetSupportedEncoding(ProtocolVars_t *pv, PacketEncoding_t dftEncoding)
{
    UInt32 enc = ENCODING_VALUE(dftEncoding); // <-- max 15
    if (enc == ENCODING_BINARY) {
        return (PacketEncoding_t)enc;
    } else {
        UInt32 propEncodings = pv->isPrimary? propGetUInt32(PROP_COMM_ENCODINGS, 0L) : 0L; // protocol dependent
        UInt32 encodingMask = propEncodings | ENCODING_REQUIRED_MASK; // <-- must include binary
        while ((ENCODING_MASK(enc) & encodingMask) == 0L) {
            // ENCODING_CSV     3 
            // ENCODING_HEX     2 
            // ENCODING_BASE64  1 
            // ENCODING_BINARY  0 
            enc--;
        }
        //if (enc != ENCODING_VALUE(dftEncoding)) { logDEBUG(LOGSRC,"Encoding down-graded to %ld\n", enc); }
        return ENCODING_IS_CHECKSUM(dftEncoding)? 
            (PacketEncoding_t)ENCODING_CHECKSUM(enc) : 
            (PacketEncoding_t)enc;
    }
}

// ----------------------------------------------------------------------------
// The following are wrappers to the 'transport.c' module function calls

/* return transport 'open' state */
utBool protocolIsSpeakFreely(int protoNdx)
{
    ProtocolVars_t *pv = _protoGetVars(LOGSRC,protoNdx);
    return pv->xFtns->xportIsOpen()? pv->speakFreely : utFalse;
}

/* return transport 'open' state */
utBool protocolIsOpen(int protoNdx)
{
    ProtocolVars_t *pv = _protoGetVars(LOGSRC,protoNdx);
    return pv->xFtns->xportIsOpen();
}

/* open connection to server */
static utBool _protocolOpen(ProtocolVars_t *pv, TransportType_t type)
{
    utBool didOpen = pv->xFtns->xportOpen(type);
        
    if (didOpen) {
        // opened, reset session

        /* clear volatile queue on initial connection */
        pqueResetQueue(&(pv->volatileQueue));

        _protocolEnableOverwrite(pv, utFalse); // disable overwrites while connected
        if (pv->isPrimary) { // data stats
            // data stats only recorded for 'primary' transport
            pv->totalReadBytes      = propGetUInt32(PROP_COMM_BYTES_READ   , 0L); // primary only
            pv->totalWriteBytes     = propGetUInt32(PROP_COMM_BYTES_WRITTEN, 0L); // primary only
        } else {
            // data stats not recorded for 'secondary' transport
            pv->totalReadBytes      = 0L;
            pv->totalWriteBytes     = 0L;
        }
        pv->sessionReadBytes        = 0L;
        pv->sessionWriteBytes       = 0L;

#if defined(TRANSPORT_MEDIA_SERIAL)
        // send account/device for serial transport
        pv->sendIdentification      = SEND_ID_ACCOUNT;
#else
        // try unique-id first for everything else
        pv->sendIdentification      = SEND_ID_UNIQUE;
#endif

        pv->severeErrorCount        = 0;
        pv->checkSumErrorCount      = 0;
        pv->invalidAcctErrorCount   = 0;

        if (pv->isSerial) {
            motionResetMovingMessageTimer();
            // TODO: generate a connection message?
        }

    }
    return didOpen;
}

/* close connection to server */
static utBool _protocolClose(ProtocolVars_t *pv, TransportType_t xportType, utBool sendUDP)
{

    /* close transport */
    // If the connection is via Simplex, the data will be sent now.
    utBool doSend = (xportType == TRANSPORT_SIMPLEX)? sendUDP : utFalse;
    utBool didClose = pv->xFtns->xportClose(doSend);
        
    if (didClose && pv->isPrimary) { // data stats
        // save read/write byte counts if 'close' was successful (primary only)
        propSetUInt32(PROP_COMM_BYTES_READ   , pv->totalReadBytes );
        propSetUInt32(PROP_COMM_BYTES_WRITTEN, pv->totalWriteBytes);
    }
    
    /* reset volitile queue */
    pqueResetQueue(&(pv->volatileQueue));
    
    /* re-enable event queue overwrites while not connected */
    _protocolEnableOverwrite(pv,EVENT_QUEUE_OVERWRITE); // enabled only while not connected
    
    /* check for severe errors */
    if (xportType == TRANSPORT_DUPLEX) {
        if (pv->severeErrorCount) {
            // this helps prevent runnaway clients from abusing the server
            pv->totalSevereErrorCount += pv->severeErrorCount;
            logWARNING(LOGSRC,"Severe errors encountered --> %d", pv->totalSevereErrorCount);
            if (pv->isSerial) {
                // severe errors are ignored for bluetooth connections
            } else
            if (pv->isPrimary) { // severe errors
                if (pv->totalSevereErrorCount >= MAX_SEVERE_ERRORS) {
                    // Slow down minimum connection interval
                    UInt32 minXmitRate = propGetUInt32(PROP_COMM_MIN_XMIT_RATE, 0L); // primary only
                    if (minXmitRate < HOUR_SECONDS(12)) {
                        //if (minXmitRate < 1) { minXmitRate = 1; }
                        if (minXmitRate < MIN_XMIT_RATE) { minXmitRate = MIN_XMIT_RATE; }
                        propAddUInt32(PROP_COMM_MIN_XMIT_RATE, minXmitRate); // doubles rate (primary only)
                    }
                    UInt32 minXmitDelay = propGetUInt32(PROP_COMM_MIN_XMIT_DELAY, 0L); // primary only
                    if (minXmitDelay < HOUR_SECONDS(12)) {
                        //if (minXmitDelay < 1) { minXmitDelay = 1; }
                        if (minXmitDelay < MIN_XMIT_DELAY) { minXmitDelay = MIN_XMIT_DELAY; }
                        propAddUInt32(PROP_COMM_MIN_XMIT_DELAY, minXmitDelay); // doubles delay (primary only)
                    }
                }
                if (pv->totalSevereErrorCount >= EXCESSIVE_SEVERE_ERRORS) {
                    // Turn off periodic messaging
                    logERROR(LOGSRC,"Excessive severe errors! Disabling periodic events!");
                    propSetUInt32(PROP_MOTION_START, 0L); // primary only
                    propSetUInt32(PROP_MOTION_IN_MOTION, 0L); // primary only
                    propSetUInt32(PROP_MOTION_DORMANT_INTRVL, 0L); // primary only
                }
            } else {
                // severe errors are ignored for secondary transports
            }
        } else
        if (pv->totalSevereErrorCount > 0) {
            // a session without any severe errors will reduce this count
            pv->totalSevereErrorCount--;
        }
    }
    
    return didClose;
}

/* write data to server */
static int _protocolWrite(ProtocolVars_t *pv, const UInt8 *buf, int bufLen, utBool calcChksum)
{
    // guaranteed to contain only a single packet
    
    /* print */
    if (bufLen > 0) {
        if (*buf == PACKET_ASCII_ENCODING_CHAR) {
            logINFO(LOGSRC,"Tx%d]%.*s", pv->protoNdx, bufLen - 1, buf);
        } else {
            UInt8 hex[PACKET_MAX_ENCODED_LENGTH];
            logINFO(LOGSRC,"Tx%d]0x%s", pv->protoNdx, strEncodeHex((char*)hex, sizeof(hex), buf, bufLen)); 
        }
    }
    
    /* write */
    int len = pv->xFtns->xportWritePacket(buf, bufLen);
        
    if (len >= 0) {
        if (calcChksum) { 
            cksumCalcFletcher(buf, bufLen); 
        }
        pv->totalWriteBytes   += len;
        pv->sessionWriteBytes += len;
    } else {
        // error 
    }
    return len;
    
}

/* write packet to server */
static int _protocolWritePacket(ProtocolVars_t *pv, Packet_t *pkt)
{
    UInt8 buf[PACKET_MAX_ENCODED_LENGTH];
    Buffer_t bb, *dest = binBuffer(&bb, buf, sizeof(buf), BUFFER_DESTINATION);
    binResetBuffer(dest);
    pktEncodePacket(dest, pkt, pv->sessionFirstEncoding); // we ignore any internal errors
    int rtnWriteLen = _protocolWrite(pv, BUFFER_PTR(dest), BUFFER_DATA_LENGTH(dest), utTrue);
    pv->sessionFirstEncoding = pv->sessionEncoding;
    return rtnWriteLen;
}

// ----------------------------------------------------------------------------

/* queue specified packet for transmission */
static utBool _protocolQueuePacket(ProtocolVars_t *pv, Packet_t *pkt)
{
    // Notes:
    // - By default packets are created with 'PRIORITY_NORMAL', and will be placed
    // into the volatile queue which is reset at the start of each session.  Thus,
    // the volatile queue should only be used within a server connected session. 
    // - If a packet is important enough to be retained until it is transmitted to
    // the server, then its priority should be set to 'PRIORITY_HIGH'. It will then
    // be added to the pending queue and will be retained until it is successfully 
    // transmitted to the server.  This is also true for important packets that are
    // queued while NOT within a server connected session.
    if (!pkt) {
        return utFalse;
    } else
    if (pkt->priority >= PRIORITY_HIGH) {
        // Place high priority packets in the pending queue
        // This queue persists across sessions
        return pqueAddPacket(&(pv->pendingQueue), pkt);
    } else {
        // place normal/low priority packets in the volatile queue
        // This queue is cleared before/after each session
        return pqueAddPacket(&(pv->volatileQueue), pkt);
    }
}
utBool protocolQueuePacket(int protoNdx, Packet_t *pkt)
{
    ProtocolVars_t *pv = _protoGetVars(LOGSRC,protoNdx);
    return _protocolQueuePacket(pv, pkt);
}

/* queue specified error for transmission */
static utBool _protocolQueueError(ProtocolVars_t *pv, const char *fmt, ...)
{
    Packet_t pkt;
    va_list ap;
    va_start(ap, fmt);
    pktVInit(&pkt, PKT_CLIENT_ERROR, fmt, ap); // default PRIORITY_NORMAL
    va_end(ap);
    return _protocolQueuePacket(pv,&pkt);
}
utBool protocolQueueError(int protoNdx, const char *fmt, ...)
{
    ProtocolVars_t *pv = _protoGetVars(LOGSRC,protoNdx);
    Packet_t pkt;
    va_list ap;
    va_start(ap, fmt);
    pktVInit(&pkt, PKT_CLIENT_ERROR, fmt, ap); // default PRIORITY_NORMAL
    va_end(ap);
    return _protocolQueuePacket(pv,&pkt);
}

/* queue specified diagnostic for transmission */
utBool protocolQueueDiagnostic(int protoNdx, const char *fmt, ...)
{
    ProtocolVars_t *pv = _protoGetVars(LOGSRC,protoNdx);
    Packet_t pkt;
    va_list ap;
    va_start(ap, fmt);
    pktVInit(&pkt, PKT_CLIENT_DIAGNOSTIC, fmt, ap); // default PRIORITY_NORMAL
    va_end(ap);
    return _protocolQueuePacket(pv,&pkt);
}

// ----------------------------------------------------------------------------

/* parse packet received from server */
static Packet_t *_protocolParseServerPacket(ProtocolVars_t *pv, Packet_t *pkt, const UInt8 *pktBuf)
{
    
    /* clear packet */
    memset(pkt, 0, sizeof(Packet_t));
    
    /* parse header/data */
    if (*pktBuf == PACKET_ASCII_ENCODING_CHAR) {
        // The packet is assumed to be null-terminated (ie. '\r' was replaced with '0')
        
        /* print packet */
        logDEBUG(LOGSRC,"Rx%d]%s\n", pv->protoNdx, pktBuf); 
        
        /* parse */
        int pktBufLen;
        if (!cksumIsValidCharXOR((char*)pktBuf, &pktBufLen)) {
            // checksum failed: ERROR_PACKET_CHECKSUM
            _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_CHECKSUM, (UInt32)0);
            return (Packet_t*)0;
        } else
        if (pktBufLen < 5) {
            // invalid length: ERROR_PACKET_LENGTH
            _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_LENGTH, (UInt32)0);
            return (Packet_t*)0;
        } else {
            UInt8 buf[2];
            int hlen = strParseHex((char*)(pktBuf + 1), 4, buf, sizeof(buf));
            if (hlen != 2) {
                // header was not parsable: ERROR_PACKET_HEADER
                _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_HEADER, (UInt32)0);
                return (Packet_t*)0;
            } else {
                pkt->hdrType = CLIENT_HEADER_TYPE(buf[0],buf[1]);
                if (pktBufLen > 6) { // $E0FF:XXXX...
                    // encoded character, plus data
                    if (pktBuf[5] == ENCODING_BASE64_CHAR) {
                        int len = (UInt8)base64Decode((char*)(pktBuf + 6), pktBufLen - 6, pkt->data, sizeof(pkt->data));
                        pkt->dataLen = (len >= 0)? (UInt8)len : 0;
                    } else
                    if (pktBuf[5] == ENCODING_HEX_CHAR) {
                        int len = (UInt8)strParseHex((char*)(pktBuf + 6), pktBufLen - 6, pkt->data, sizeof(pkt->data));
                        pkt->dataLen = (len >= 0)? (UInt8)len : 0;
                    } else
                    if (pktBuf[5] == ENCODING_CSV_CHAR) {
                        // unsupported encoding: ERROR_PACKET_ENCODING
                        // parsing CSV encoded packets is not supported in this reference implementation
                        logWARNING(LOGSRC,"CSV parsing is not supported.\n");
                        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_ENCODING, (UInt32)pkt->hdrType);
                        return (Packet_t*)0;
                    } else {
                        // unrecognized encoding: ERROR_PACKET_ENCODING
                        logWARNING(LOGSRC,"Unrecognized encoding: %d\n", pktBuf[5]);
                        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_ENCODING, (UInt32)pkt->hdrType);
                        return (Packet_t*)0;
                    }
                }
            }
        }
        
    } else
    if (*pktBuf == PACKET_HEADER_BASIC) {
        // The packet header is assumed to be a valid length.
        
        /* print packet */
        UInt8 hex[PACKET_MAX_ENCODED_LENGTH];
        UInt16 len = (UInt16)pktBuf[2] + 3;
        logINFO(LOGSRC,"Rx%d]0x%s\n", pv->protoNdx, strEncodeHex((char*)hex, sizeof(hex), pktBuf, len)); 
        
        /* parse into packet */
        pkt->hdrType = CLIENT_HEADER_TYPE(pktBuf[0],pktBuf[1]);
        pkt->dataLen = (UInt8)pktBuf[2];
        if (pkt->dataLen > 0) {
            memcpy(pkt->data, pktBuf + 3, (int)pkt->dataLen);
        }
        
    } else {
        // invalid header: ERROR_PACKET_HEADER
        ClientPacketType_t hdrType = CLIENT_HEADER_TYPE(pktBuf[0],pktBuf[1]);
        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_HEADER, (UInt32)hdrType);
        return (Packet_t*)0;
    }
    
    /* return packet */
    return pkt;
}

// ----------------------------------------------------------------------------

/* read packet from server */
static int _protocolReadServerPacket(ProtocolVars_t *pv, Packet_t *pkt)
{
    UInt8 buf[PACKET_MAX_ENCODED_LENGTH];
    int bufLen = pv->xFtns->xportReadPacket(buf, sizeof(buf));
    
    /* parse and validate packet */
    if (bufLen < 0) {
        // read error (transport not open?)
        return -1; // Fix: MDF 2006/07/28
    } else
    if (bufLen == 0) {
        // timeoout (or no data)
        return 0;
    } else {
        // this count won't be accurate if an error occurred during a packet read
        pv->totalReadBytes   += bufLen;
        pv->sessionReadBytes += bufLen;
        // parse packet
        _protocolParseServerPacket(pv, pkt, buf); // , bufLen);
        return 1;
    }
    
}

// ----------------------------------------------------------------------------

/* flush input buffer (from server) */
static void _protocolFlushInput(ProtocolVars_t *pv)
{
    // This typically only has meaning in speakFreely mode (eq. serial transport)
    // This prevents reading old/obsolete messages that may have been queue by
    // the server some time ago that we weren't able to pick up quickly enough.
    if (pv->isSerial) {
        pv->xFtns->xportReadFlush();
    }
}

/* return true if we have pending data to send */
static utBool _protocolHasDataToSend(ProtocolVars_t *pv)
{
    if (pv->sendIdentification != SEND_ID_NONE) {
        // identification has been requested
        return utTrue;
    } else
    if (pqueHasPackets(&(pv->pendingQueue))) {
        // has pending important packets
        return utTrue;
    } else
    if (pqueHasPackets(&(pv->volatileQueue))) {
        // has miscellaneous volatile packets
        return utTrue;
    } else
    if (pqueHasPackets(_protocolGetEventQueue(pv))) {
        // has event packets
        return utTrue;
    }
    return utFalse;
}

/* send identification packets */
static utBool _protocolSendIdentification(ProtocolVars_t *pv)
{
    if (pv->sendIdentification != SEND_ID_NONE) {
        Packet_t idPkt;
        
        /* account/device */
        const char *acctId = propGetAccountID(); // propGetString(PROP_STATE_ACCOUNT_ID,"");
        const char *devId  = propGetDeviceID(pv->protoNdx);  // propGetString(PROP_STATE_DEVICE_ID,"");
        /* first try our UniqueID */
        utBool okUniqueId = (pv->sendIdentification == SEND_ID_UNIQUE)? utTrue : utFalse;
        if (okUniqueId) {
            UInt8 id[MAX_ID_SIZE];
            int maxLen = sizeof(id);
            PropertyError_t err = propGetValue(PROP_STATE_UNIQUE_ID, id, maxLen); // ok as-is
            int b, len = PROP_ERROR_OK_LENGTH(err);
            if (len >= MIN_UNIQUE_SIZE) { // length must at least MIN_UNIQUE_SIZE
                for (b = 0; (b < len) && (id[b] == 0); b++);
                if (b < len) { // at least one field must be non-zero
                    pktInit(&idPkt, PKT_CLIENT_UNIQUE_ID, "%*b", len, id);
                    if (_protocolWritePacket(pv,&idPkt) < 0) {
                        return utFalse; // write error
                    }
                    pv->sendIdentification = SEND_ID_NONE;
                    return utTrue; // no write-error
                }
            }
        }
    
        // AccountID
        utBool sentAcct = utFalse;
        if (acctId && *acctId) {
            // Account ID is sent iff it is defined
            pktInit(&idPkt, PKT_CLIENT_ACCOUNT_ID, "%*s", MAX_ID_SIZE, acctId);
            if (_protocolWritePacket(pv,&idPkt) < 0) {
                return utFalse; // write error
            }
            sentAcct = utTrue;
        }

        // DeviceID
        utBool sentDev = utFalse;
        if (devId && *devId) {
            // Device ID is sent iff it is defined
            //logDEBUG(LOGSRC,"Device: %s", devId);
            pktInit(&idPkt, PKT_CLIENT_DEVICE_ID, "%*s", MAX_ID_SIZE, devId);
            if (_protocolWritePacket(pv,&idPkt) < 0) {
                return utFalse; // write error
            }
            sentDev = utTrue;
        }

        /* ID successfully sent */
        pv->sendIdentification = SEND_ID_NONE;
        return utTrue; // no write-error

    }

    return utTrue; // no write-error

}

/* send contents of specified queue */
static utBool _protocolSendQueue(ProtocolVars_t *pv, PacketQueue_t *pq, PacketPriority_t maxPri, int maxEvents, utBool *hasMorePackets)
{
    int rtnWriteLen = 0; // rtnVal

    /* adjust arguments */
    if (maxPri < PRIORITY_LOW) { maxPri = PRIORITY_LOW; } // at least low priority packets
    if (maxEvents == 0) { maxEvents = 1; } // at least 1 packet
    // a 'maxEvent' < 0 means there is no maximum number of events to send

    /* iterate through queue */
    // This loop stops as soon as one of the following has occured:
    //  - We've sent the specified 'maxEvents'.
    //  - All events in the queue have been sent.
    //  - We've run into a packet that exceeds our maximum allowable priority.
    Packet_t *quePkt;
    PacketQueueIterator_t queIter;
    pqueGetIterator(pq, &queIter);
    for (; (maxEvents != 0) && (quePkt=pqueGetNextPacket((Packet_t*)0,&queIter)) && (quePkt->priority <= maxPri) ;) {

        /* write packet */
        rtnWriteLen = _protocolWritePacket(pv,quePkt);
        if (rtnWriteLen < 0) {
            break;
        }
        
        /* mark this packet as sent */
        quePkt->sent = utTrue; // mark it as sent
        
        /* decrement counter */
        if (maxEvents > 0) { maxEvents--; }
        
        /* unknown sequence? */
        if ((quePkt->seqLen > 0) && (quePkt->sequence == SEQUENCE_ALL)) {
            // stop if we find a 'sequence' anomoly
            break;
        }
        
    }
    
    /* still have more packets? */
    if (hasMorePackets) {
        *hasMorePackets = pqueHasNextPacket(&queIter);
    }

    /* check for errors */
    if (rtnWriteLen < 0) {
        return utFalse; // write error: close socket
    } else {
        return utTrue;
    }
    
}

/* end End-Of-Block */
static utBool _protocolSendEOB(ProtocolVars_t *pv, utBool hasMoreEvents)
{
    // Duplex Transport
    if (!pv->speakFreely) {
        UInt8 buf[PACKET_MAX_ENCODED_LENGTH];
        Buffer_t bb, *dest = binBuffer(&bb, buf, sizeof(buf), BUFFER_DESTINATION);
        Packet_t eob;
        ClientPacketType_t eobType = hasMoreEvents? PKT_CLIENT_EOB_MORE : PKT_CLIENT_EOB_DONE;

        /* Add Fletcher checksum if encoding is binary */
        if (ENCODING_VALUE(pv->sessionFirstEncoding) == ENCODING_BINARY) {

            /* encode packet with a placeholder for the checksum */
            // Fixed checksum length check, was "sizeof(ChecksumFletcher_t)"
            pktInit(&eob, eobType, "%*z", FLETCHER_CHECKSUM_LENGTH); // zero-fill 2 bytes
            pktEncodePacket(dest, &eob, ENCODING_BINARY); // we ignore any internal errors
            cksumCalcFletcher(BUFFER_PTR(dest), BUFFER_DATA_LENGTH(dest)); // length should be 5

            /* calculate the checksum and insert it into the packet */
            ChecksumFletcher_t fcs;
            cksumGetFletcherChecksum(&fcs); // encode
            binPrintf(BUFFER_PTR(dest)+3, FLETCHER_CHECKSUM_LENGTH, "%*b", FLETCHER_CHECKSUM_LENGTH, fcs.C);

        } else {

            /* encode packet without checksum */
            pktInit(&eob, eobType, (char*)0);
            pktEncodePacket(dest, &eob, pv->sessionFirstEncoding); // we ignore any internal errors

        }

        /* write EOB packet */
        int rtnWriteLen = _protocolWrite(pv, BUFFER_PTR(dest), BUFFER_DATA_LENGTH(dest), utFalse);
        if (rtnWriteLen < 0) {
            return utFalse; // write error: close socket
        }
        pv->speakFreely = utFalse; // relinquish any granted "speak freely" permission on EOB
        pv->speakFreelyMaxEvents = -1;

        /* next encoding */
        pv->sessionFirstEncoding = pv->sessionEncoding;

    }

    return utTrue;
}

/* send packets to server */
static utBool _protocolSendAllPackets(ProtocolVars_t *pv, TransportType_t xportType, utBool brief, int dftMaxEvents)
{

    /* reset checksum before we start transmitting */
    cksumResetFletcher();

    /* transmit identification packets */
    if (!_protocolSendIdentification(pv)) {
        return utFalse; // write error
    }

    /* 'brief' means send only the identification and EOB packets */
    // If the ID packet aren't sent (don't need to be sent), and 'speakFreekly' is true,
    // then its possible that nothing will be sent.
    utBool hasMoreEvents = utFalse;
    if (brief) {

        hasMoreEvents = _protocolHasDataToSend(pv);

    } else {

        /* transmit pending packets (sent first) */
        if (!_protocolSendQueue(pv, &(pv->pendingQueue), PRIORITY_HIGH, -1, (utBool*)0)) {
            return utFalse; // write error: close socket
        }

        /* transmit volatile packets (sent second) */
        if (!_protocolSendQueue(pv, &(pv->volatileQueue), PRIORITY_HIGH, -1, (utBool*)0)) {
            return utFalse; // write error: close socket
        }

        /* reset queues */
        // wait until all queues have successfully been sent before clearing
        pqueResetQueue(&(pv->volatileQueue));
        pqueResetQueue(&(pv->pendingQueue));

        /* send events flag */
        // default to sending events if the specified default maximum is not explicitly '0'
        PacketQueue_t *eventQueue = _protocolGetEventQueue(pv);
        utBool sendEvents = (eventQueue && (dftMaxEvents != 0))? utTrue : utFalse;
        // other criteria may also set this to false below

        /* determine if we should force-relinquish speak-freely */
        if (pv->speakFreely) {
            
            // Always relinquish speak-freely after sending a block of events
            if (sendEvents && pqueHasPackets(eventQueue)) {
                // If we have any events at all, relinquish speak-freely
                // This will allow the server to acknowledge these events and let the client
                // know that the server is listening.
                pv->speakFreely = utFalse;
                pv->speakFreelyMaxEvents = -1;
            }
        }

        /* send events */
        if (sendEvents) { // && (dftMaxEvents != 0)

            /* max events to send during this block */
            int maxEvents = 8;
            switch ((int)xportType) {
                case TRANSPORT_SIMPLEX:
                    if (pv->isPrimary) { // max simplex events
                        maxEvents = (int)propGetUInt32(PROP_COMM_MAX_SIM_EVENTS, 4L); // primary only
                        if (maxEvents > MAX_SIMPLEX_EVENTS) { maxEvents = MAX_SIMPLEX_EVENTS; }
                    } else
                    if (pv->isSerial) {
                        maxEvents = 1; // will never occur, 'serial' doesn't send events via 'simplex' 
                    } else {
                        maxEvents = MAX_SIMPLEX_EVENTS;
                    }
                    break;
                case TRANSPORT_DUPLEX:
                    if (pv->isPrimary) { // max duplex events
                        maxEvents = (int)propGetUInt32(PROP_COMM_MAX_DUP_EVENTS, 8L); // primary only
                        if (maxEvents > MAX_DUPLEX_EVENTS) { maxEvents = MAX_DUPLEX_EVENTS; }
                    } else
                    if (pv->isSerial) {
                        maxEvents = 1;
                    } else {
                        maxEvents = MAX_DUPLEX_EVENTS;
                    }
                    break;
                default:
                    logCRITICAL(LOGSRC,"Invalid Transport-Type: %d", (int)xportType);
                    break;
            }
            if ((dftMaxEvents > 0) && (maxEvents > dftMaxEvents)) {
                // limit number of event to the specified default maximum
                // (the value of 'dftMaxEvent' is disregarded if less than '0')
                maxEvents = dftMaxEvents;
            }

            /* max priority events to send */
            // - This function is getting called because "_getTransportType()" returned a transport
            // type based on what it found in the event queue.  If it chose a Simplex connection
            // based on 'Low' priority events found in the queue, then we should make sure that
            // only low priority events will get sent via Simplex.  This prevents Normal and High
            // priority events from getting sent that may have entered the queue while we are
            // still trying to set up the connection (which could take a while).
            // - If it is desirable to go ahead and send all (including High priority) events found
            // in the queue, then this restriction should be relaxed, however it would then be 
            // necessary to insure that these event don't get purged from the queue until they were 
            // successfully later sent via Duplex.
            PacketPriority_t maxPri;
            maxPri = (pv->isSerial || (xportType == TRANSPORT_DUPLEX) || !acctSupportsDuplex())? 
                PRIORITY_HIGH :     // all priority events will be sent
                PRIORITY_LOW;       // only low priority events will be sent
    
            /* transmit unacknowledged event packets */
            if (eventQueue && !_protocolSendQueue(pv, eventQueue, maxPri, maxEvents, &hasMoreEvents)) {
                return utFalse; // write error: close socket
            }

        } else {

            /* We didn't send events yet.  Check to see if we have any to send */
            hasMoreEvents = _protocolHasDataToSend(pv);

        }

    }

    /* send end-of-block packet */
    if ((xportType == TRANSPORT_DUPLEX) && !pv->speakFreely) {
        if (!_protocolSendEOB(pv,hasMoreEvents)) {
            return utFalse;
        }
    }

    return utTrue;
}

// ----------------------------------------------------------------------------

/* handle server-originated error packet */
static utBool _protocolHandleErrorCode(ProtocolVars_t *pv, UInt16 errCode, ClientPacketType_t pktHdrType, UInt8 *valData, int valDataLen)
{
    //Buffer_t argBuf, *argSrc = binBuffer(&argBuf, valData, valDataLen, BUFFER_SOURCE);
    switch ((ServerError_t)errCode) {
        
        case NAK_OK                     : { // Everything ok (should never occur here
            return utTrue;
        }

        case NAK_ID_INVALID             : { // Invalid unique id
            // The DMT server doesn't recognize our unique-id. 
            // We should try to send our account and device id.
            pv->sendIdentification = SEND_ID_ACCOUNT;
            return utTrue;
        }
            
        case NAK_ACCOUNT_ERROR          : // Internal server error
        case NAK_DEVICE_ERROR           : { // Internal server error
            // The DMT server encountered an error retrieve our ID
            pv->severeErrorCount++;
            if (++(pv->invalidAcctErrorCount) >= 2) { // fail on 2nd error
                return utFalse;
            } else {
                // try one more time (if the server will let us)
                return utTrue;
            }
        }

        case NAK_ACCOUNT_INVALID        : // Invalid/missing account id
        case NAK_DEVICE_INVALID         : { // Invalid/missing device id
            // The DMT server doesn't know who we are
            pv->severeErrorCount++;
            if (++(pv->invalidAcctErrorCount) >= 2) { // fail on 2nd error
                return utFalse;
            } else {
                // try one more time (if the server will let us)
                return utTrue;
            }
        }
            
        case NAK_ACCOUNT_INACTIVE       : // Account has expired, or has become inactive
        case NAK_DEVICE_INACTIVE        : { // Device has expired, or has become inactive
            pv->severeErrorCount++;
            return utFalse;
        }
            
        case NAK_EXCESSIVE_CONNECTIONS  : { // Excessive connections
            if (pv->isPrimary) { // excessive connections
                // The DMT server may mark (or has marked) us as an abuser.
                // No alternative, but to quit
                // Slow down minimum connection interval
                propAddUInt32(PROP_COMM_MIN_XMIT_RATE , 300L);
                propAddUInt32(PROP_COMM_MIN_XMIT_DELAY, 300L);
            } else {
                // should not occur for secondary protocol
            }
            return utFalse;
        }
            
        case NAK_PACKET_HEADER          :   // Invalid/Unsupported packet header
        case NAK_PACKET_TYPE            : { // Invalid/Unsupported packet type
            // The DMT server does not support our custom extensions
            // Ignore the error and continue.
            return utTrue;
        }
        
        case NAK_PACKET_LENGTH          :   // Invalid packet length
        case NAK_PACKET_PAYLOAD         : { // Invalid packet payload
            // This indicates a protocol compliance issue in the client
            pv->severeErrorCount++;
            return utFalse;
        }
        
        case NAK_PACKET_ENCODING        : { // Encoding not supported
            // DMT servers are required to support Binary, ASCII-Base64, and ASCII-Hex encoding.
            // This should only occur if we are encoding packets in ASCII CSV format and the
            // server doesn't support this encoding.  We shoud try again with HEX or Base64
            // encoding for the remainder of the session.
            // We need to handle getting several of these errors during a transmission block.
            if (!pv->sessionEncodingChanged) {
                pv->sessionEncodingChanged = utTrue; // mark changed
                UInt32 encMask = ENCODING_MASK(pv->sessionEncoding);
                if (encMask & ENCODING_REQUIRED_MASK)  {
                    // We're already encoding in a server supported encoding
                    // This is likely some protocol compliance issue with the client
                    // (of course it can't be the server! :-)
                    return utFalse;
                }
                if (pv->isPrimary) { // session encodings
                    UInt32 propEncodings = propGetUInt32(PROP_COMM_ENCODINGS, 0L); // primary only
                    UInt32 encodingMask = propEncodings & ~encMask;
                    propSetUInt32(PROP_COMM_ENCODINGS, encodingMask | ENCODING_REQUIRED_MASK); // primary only
                    pv->sessionEncoding = _protocolGetSupportedEncoding(pv, pv->sessionEncoding);
                } else {
                    pv->sessionEncoding = _protocolGetSupportedEncoding(pv, ENCODING_HEX);
                }
                if ((pktHdrType == PKT_CLIENT_UNIQUE_ID)  ||
                    (pktHdrType == PKT_CLIENT_ACCOUNT_ID) ||
                    (pktHdrType == PKT_CLIENT_DEVICE_ID)    ) {
                    // error occured on identification packets, resend id
#if defined(TRANSPORT_MEDIA_SERIAL)
                    // send account/device for serial transport
                    pv->sendIdentification = SEND_ID_ACCOUNT;
#else
                    // try unique-id first for everything else
                    pv->sendIdentification = SEND_ID_UNIQUE;
#endif
                }
            }
            return utTrue;
        }
        
        case NAK_PACKET_CHECKSUM        : // Invalid packet checksum (ASCII encoding only)
        case NAK_BLOCK_CHECKSUM         : { // Invalid block checksum
            // increment checksum failure indicator, if it gets too large, quit
            if (++(pv->checkSumErrorCount) >= 3) { // fail on 3rd error (per session)
                pv->severeErrorCount++;
                return utFalse;
            } else {
                return utTrue;
            }
        }
        
        case NAK_PROTOCOL_ERROR         : { // Protocol error
            // This indicates a protocol compliance issue in the client
            pv->severeErrorCount++;
            return utFalse;
        }
            
        case NAK_FORMAT_DEFINITION_INVALID: { // Custom format type is invalid
            if (pv->isPrimary) { // invalid format definition
                // The custom type we've specified isn't within the supported custom format packet types
                // This indicates a protocol compliance issue in the client
                propSetBoolean(PROP_COMM_CUSTOM_FORMATS, utFalse);
                pv->severeErrorCount++;
            } else {
                // should not occur for secondary protocol
            }
            return utFalse;
        }

        case NAK_FORMAT_NOT_SUPPORTED   : { // Custom formats not supported
            // The DMT server does not support custom formats (at least not for our current level of service)
            _protocolAcknowledgeToSequence(pv,SEQUENCE_ALL);
            if (pv->isPrimary) { // custom formats not supported
                // We should acknowledge all sent events, and set a flag indicating that
                // we should not send custom formats to this server in the future.
                propSetBoolean(PROP_COMM_CUSTOM_FORMATS, utFalse);
            } else {
                // should not occur for secondary protocol
            }
            return utTrue;
        }
            
        case NAK_FORMAT_NOT_RECOGNIZED  : { // Custom format not recognized
            // The DMT does support custom formats, but it doesn't recognize the 
            // format we've used in an event packet.  We should send the custom
            // format template(s), then resend the events.
            Packet_t custPkt;
            int rtn = evGetCustomFormatPacket(&custPkt, pktHdrType);
            if (rtn >= 0) {
                custPkt.priority = PRIORITY_HIGH;
                return _protocolQueuePacket(pv,&custPkt);
            } else {
                // One of the following has occured:
                // - The server just told us it doesn't support a custom format that we didn't 
                //   send it (unlikely).
                // - An internal buffer overflow has ocurred.
                // - We were unable to add the packet to the queue
                pv->severeErrorCount++;
                return utFalse;
            }
        }

        case NAK_EXCESSIVE_EVENTS       : { // Excessive events
            // If present, the next (first) event will never be accepted, purge it from the queue.
            _protocolAcknowledgeFirst(pv); // first "sent" event
            if (pv->isPrimary) { // excessive events
                // The DMT server may mark (or has marked) us as an abuser.
                // Slow down periodic messages to prevent this from occurring in the future
                UInt32 inMotionInterval = propGetUInt32(PROP_MOTION_IN_MOTION, 0L); // primary only
                if (inMotionInterval > 0L) {
                    propSetUInt32(PROP_MOTION_IN_MOTION, (inMotionInterval + MINUTE_SECONDS(2))); // primary only
                }
                UInt32 dormantInterval = propGetUInt32(PROP_MOTION_DORMANT_INTRVL, 0L); // primary only
                if (dormantInterval > 0L) {
                    propSetUInt32(PROP_MOTION_DORMANT_INTRVL, (dormantInterval + MINUTE_SECONDS(10))); // primary only
                }
            } else {
                // should not occur for secondary protocol
            }
            // continue with session
            return utTrue;
        }

        case NAK_DUPLICATE_EVENT        : { // Duplicate event found
            // ignore error
            return utTrue;
        }

        case NAK_EVENT_ERROR            : { // Server had an error when processing this event
            // ignore error
            return utTrue;
        }

    }
    
    /* unhandled error - ignore */
    return utTrue;

}

// ----------------------------------------------------------------------------

/* handle server-originated packet */
static utBool _protocolHandleServerPacket(ProtocolVars_t *pv, Packet_t *srvPkt)
{
    // client should return 'utFalse' if an error has occured and communication should not continue
    
    /* check header */
    if (CLIENT_PACKET_HEADER(srvPkt->hdrType) != PACKET_HEADER_BASIC) {
        // unsupported header
        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_HEADER, (UInt32)srvPkt->hdrType);
        // returned errors are ignored (possible internal buffer overflow)
        return utTrue; // continue communication
    }
    
    /* handle packet */
    switch ((ServerPacketType_t)srvPkt->hdrType) {
        
        // End of transmission, query response
        // Payload: maxEvents[optional], serverKey[optional]
        case PKT_SERVER_EOB_DONE     : {
            // flush server input buffer (useful for 'serial' transport only)
            _protocolFlushInput(pv); 
            // The server is expecting the client to respond as soon as possible
            pv->speakFreely = utFalse; // relinquish speak-freely permission
            pv->speakFreelyMaxEvents = -1;
            // Get max number of events to send (if specified)
            Int32 eobMaxEvents = -1L;
            if (srvPkt->dataLen > 0) {
                int fldCnt = binScanf(srvPkt->data, (int)srvPkt->dataLen, "%1i", &eobMaxEvents);
                if (fldCnt < 1) { eobMaxEvents = -1L; }
            }
            // Send our packets
            if (!_protocolSendAllPackets(pv, TRANSPORT_DUPLEX, pv->speakBrief, (int)eobMaxEvents)) {
                return utFalse; // write error
            }
            // Clear 'speakBrief' (we've already had our opportunity to speak).
            pv->speakBrief = utFalse;
            return utTrue;
        }
        
        // End of transmission, speak freely
        // Payload: maxEvents[optional], serverKey[optional]
        case PKT_SERVER_EOB_SPEAK_FREELY: {
            // The server is NOT expecting the client to respond anytime soon
            pv->speakFreely = utTrue; // 'speak-freely' permission granted
            pv->speakFreelyMaxEvents = -1;
            // Get max number of events to send (if specified)
            Int32 eobMaxEvents = -1L;
            if (srvPkt->dataLen > 0) {
                int fldCnt = binScanf(srvPkt->data, (int)srvPkt->dataLen, "%1i", &eobMaxEvents);
                if (fldCnt < 1) { eobMaxEvents = -1L; }
                // SpeakFreely 'eobMaxEvents' is interpreted as follows:
                //  0 - speak-freely, but don't send data from the 'event' queue
                //  # - normal speak-freely, but send at most '#' events per ack block.
                pv->speakFreelyMaxEvents = (int)eobMaxEvents;
            }
            // We will be sending data shortly (in the outer loop)
            return utTrue;
        }

        // Acknowledge [optional sequence]
        // Payload: sequence[optional]
        case PKT_SERVER_ACK          : {
            UInt32 sequence = SEQUENCE_ALL;
            int fldCnt = binScanf(srvPkt->data, (int)srvPkt->dataLen, "%4x", &sequence);
            if (fldCnt > 0) {
                // remove sent/acknowledged events from queue up to specified sequence #
                if (!_protocolAcknowledgeToSequence(pv,sequence)) {
                    // ACK error
                    int seqLen = (srvPkt->dataLen <= 4)? srvPkt->dataLen : 4;
                    _protocolQueueError(pv,"%2x%2x%*x", (UInt32)ERROR_PACKET_ACK, (UInt32)srvPkt->hdrType, seqLen, sequence);
                }
            } else {
                // remove all sent events from queue
                _protocolAcknowledgeToSequence(pv,SEQUENCE_ALL);
            }
            return utTrue;
        }
        
        // Get property
        // Payload: propertyKey(s)
        case PKT_SERVER_GET_PROPERTY : {
            Buffer_t srcBuf, *src = binBuffer(&srcBuf, srvPkt->data, srvPkt->dataLen, BUFFER_SOURCE);
            if (BUFFER_DATA_LENGTH(src) >= 2) { // while?
                UInt32 propKey = 0L;
                int fldCnt = binScanf(BUFFER_DATA(src), (int)BUFFER_DATA_LENGTH(src), "%2u", &propKey);
                binAdvanceBuffer(src, 2);
                if (fldCnt > 0) {
                    UInt8 *args   = BUFFER_DATA(src);
                    UInt16 argLen = BUFFER_DATA_LENGTH(src);
                    // queue property value packet
                    Packet_t propPkt;
                    PropertyError_t propErr = propGetPropertyPacket(pv->protoNdx, &propPkt, (Key_t)propKey, args, argLen);
                    if (PROP_ERROR_CODE(propErr) == PROP_ERROR_OK) {
                        // property packet initialized successfully
                        _protocolQueuePacket(pv,&propPkt);
                        // packet queueing errors are ignored (possible internal buffer overflow)
                    } else
                    if (PROP_ERROR_CODE(propErr) == PROP_ERROR_INVALID_KEY) {
                        // unsupported property
                        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_INVALID_ID, (UInt32)propKey);
                        // packet queueing errors are ignored (possible internal buffer overflow)
                    } else
                    if (PROP_ERROR_CODE(propErr) == PROP_ERROR_WRITE_ONLY) {
                        // write-only property
                        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_WRITE_ONLY, (UInt32)propKey);
                        // packet queueing errors are ignored (possible internal buffer overflow)
                    } else
                    if (PROP_ERROR_CODE(propErr) == PROP_ERROR_INVALID_LENGTH) {
                        // invalid payload length
                        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_INVALID_VALUE, (UInt32)propKey);
                        // returned 'queue' errors are ignored (possible internal buffer overflow)
                    } else
                    if (PROP_ERROR_CODE(propErr) == PROP_ERROR_COMMAND_INVALID) {
                        // command not supported/initialized
                        // this will not occur, since commands are write-only
                        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_COMMAND_INVALID, (UInt32)propKey);
                        // packet queueing errors are ignored (possible internal buffer overflow)
                    } else
                    if (PROP_ERROR_CODE(propErr) == PROP_ERROR_COMMAND_ERROR) {
                        // command error
                        // this will not occur, since commands are write-only
                        UInt16 errCode = (UInt16)PROP_ERROR_ARG(propErr);
                        _protocolQueueError(pv,"%2x%2x%2x", (UInt32)ERROR_COMMAND_ERROR, (UInt32)propKey, (UInt32)errCode);
                        // packet queueing errors are ignored (possible internal buffer overflow)
                    } else {
                        // internal error
                        // send error back to server: ERROR_PROPERTY_UNKNOWN_ERROR
                        _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_UNKNOWN_ERROR, (UInt32)propKey);
                        // packet queueing errors are ignored (possible internal buffer overflow)
                    }
                } else {
                    // no property specified
                    _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_PAYLOAD, (UInt32)srvPkt->hdrType);
                    // packet queueing errors are ignored (possible internal buffer overflow)
                }
            }
            return utTrue;
        }
        
        // Set property
        // Payload: propertyKey, propertyValue[optional]
        case PKT_SERVER_SET_PROPERTY : { 
            UInt32 propKey = 0L;
            int valDataLen = (srvPkt->dataLen > 2)? (int)(srvPkt->dataLen - 2) : 0;
            UInt8 valData[PACKET_MAX_ENCODED_LENGTH]; 
            int fldCnt = binScanf(srvPkt->data, (int)srvPkt->dataLen, "%2x%*b", &propKey, valDataLen, valData);
            if (fldCnt >= 1) {
                // set property value
                // TODO: If property is a 'Command', it would be very handy to be able to 
                // let the property manager where to queue the returned data (specifically 
                // the primary/secondary protocol)
                PropertyError_t propErr = propSetValueCmd(pv->protoNdx, (Key_t)propKey, valData, valDataLen); // ok as-is
                if (PROP_ERROR_CODE(propErr) == PROP_ERROR_OK) {
                    // property set successfully
                    // no packet response
                } else
                if (PROP_ERROR_CODE(propErr) == PROP_ERROR_INVALID_KEY) {
                    // unsupported property key
                    _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_INVALID_ID, (UInt32)propKey);
                    // returned errors are ignored (possible internal buffer overflow)
                } else
                if (PROP_ERROR_CODE(propErr) == PROP_ERROR_INVALID_TYPE) {
                    // internal error, invalid type
                    _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_UNKNOWN_ERROR, (UInt32)propKey);
                    // returned errors are ignored (possible internal buffer overflow)
                } else
                if (PROP_ERROR_CODE(propErr) == PROP_ERROR_INVALID_LENGTH) {
                    // invalid payload length
                    _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_INVALID_VALUE, (UInt32)propKey);
                    // returned errors are ignored (possible internal buffer overflow)
                } else
                if (PROP_ERROR_CODE(propErr) == PROP_ERROR_READ_ONLY) {
                    // read-only property
                    _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_READ_ONLY, (UInt32)propKey);
                    // returned errors are ignored (possible internal buffer overflow)
                } else
                if (PROP_ERROR_CODE(propErr) == PROP_ERROR_COMMAND_INVALID) {
                    // command not supported/initialized
                    _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_COMMAND_INVALID, (UInt32)propKey);
                    // returned errors are ignored (possible internal buffer overflow)
                } else
                if (PROP_ERROR_CODE(propErr) == PROP_ERROR_COMMAND_ERROR) {
                    // command not supported/initialized, or possibly COMMAND_OK_ACK
                    UInt16 errCode = (UInt16)PROP_ERROR_ARG(propErr);
                    _protocolQueueError(pv,"%2x%2x%2x", (UInt32)ERROR_COMMAND_ERROR, (UInt32)propKey, (UInt32)errCode);
                    // returned errors are ignored (possible internal buffer overflow)
                } else {
                    // likely internal error
                    _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PROPERTY_UNKNOWN_ERROR, (UInt32)propKey);
                    // returned errors are ignored (possible internal buffer overflow)
                }
            } else {
                // no property specified
                _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_PAYLOAD, (UInt32)srvPkt->hdrType);
                // returned errors are ignored (possible internal buffer overflow)
            }
            return utTrue;
        }

        // File upload
        case PKT_SERVER_FILE_UPLOAD  : { 
#if defined(ENABLE_UPLOAD)
            uploadProcessRecord(pv->protoNdx, srvPkt->data, (int)srvPkt->dataLen);
            // this already sends error/ack packets
#else
            _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_TYPE, (UInt32)srvPkt->hdrType);
            // returned errors are ignored (possible internal buffer overflow)
#endif
            return utTrue;
        }
        
        // NAK/Error codes
        // Payload: errorCode, packetHeader, packetType, extraData
        case PKT_SERVER_ERROR        : { 
            UInt32 errCode = 0L, pktHdrType = 0L;
            int valDataLen = (srvPkt->dataLen > 2)? (int)(srvPkt->dataLen - 2) : 0;
            UInt8 valData[PACKET_MAX_ENCODED_LENGTH]; 
            int fldCnt = binScanf(srvPkt->data, (int)srvPkt->dataLen, "%2x%2x%*b", &errCode, &pktHdrType, valDataLen, valData);
            if (fldCnt >= 1) {
                // 'errCode' contains the Error Code
                // 'pktHdr'  contains the error packet header
                // 'pktTyp'  containt the error packet type
                // 'valData' contains any additional data
                utBool ok = _protocolHandleErrorCode(pv, (UInt16)errCode, (ClientPacketType_t)pktHdrType, valData, valDataLen);
                if (!ok) {
                    return utFalse; // critical error determined by error code
                }
            } else {
                // no error specified
                _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_PAYLOAD, (UInt32)srvPkt->hdrType);
                // returned errors are ignored (possible internal buffer overflow)
            }
            return utTrue;
        }
        
        // End transmission (socket will be closed)
        // Payload: none
        case PKT_SERVER_EOT          : { 
            // return false to close communications
            return utFalse; // server is closing the connection
        }
        
        // Invalid packet type
        default: {
            _protocolQueueError(pv,"%2x%2x", (UInt32)ERROR_PACKET_TYPE, (UInt32)srvPkt->hdrType);
            // returned errors are ignored (possible internal buffer overflow)
            return utTrue;
        }
        
    }
    
}

// ----------------------------------------------------------------------------

// This function checks to see if it is time to make a connection to the DMT service
// provider, and what type of connection to make.  The time of the last connection
// and the number of connections made in the last hour are considered.
static TransportType_t _getTransportType(ProtocolVars_t *pv)
{
    
    /* secondary protocol */
    if (!pv->isPrimary) { // duplex transport for non-primary transport
        // Assume the secondary protocol supports duplex
        // - Highest event priority is PRIORITY_NONE (no events are sent to the secondary protocol)
        // - Never over duplex quota
        return TRANSPORT_DUPLEX;
    }

    /* first check absolute minimum delay between connections */
    if (!acctAbsoluteDelayExpired()) {
        // absolute minimum delay interval has not expired
        //logINFO(LOGSRC,"Absolute minimum delay interval has not expired");
        return TRANSPORT_NONE;
    }
    
    /* check specific event priority */
    TransportType_t xportType = TRANSPORT_NONE;
    PacketPriority_t evPri = _protocolGetHighestPriority(pv);
    switch (evPri) {

        // no events, time for 'checkup'?
        case PRIORITY_NONE: 
            if (!acctUnderTotalQuota()) {
                // over Total quota
                xportType = TRANSPORT_NONE;
            } else
            if (!acctMaxIntervalExpired()) {
                // MAX interval has not expired
                xportType = TRANSPORT_NONE;
            } else
            if (acctUnderDuplexQuota()) {
                // under Total/Duplex quota and MAX interval expired, time for Duplex checkup
                //logDEBUG(LOGSRC,"_getTransportType: NONE/DUPLEX ...");
                xportType = TRANSPORT_DUPLEX;
            } else {
                // over Duplex quota
                xportType = TRANSPORT_NONE;
            }
            break;

        // low priority events
        case PRIORITY_LOW: 
            if (!acctUnderTotalQuota()) {
                // over Total quota, no sending
                xportType = TRANSPORT_NONE;
            } else
            if (!acctMinIntervalExpired()) {
                // min interval has not expired, no sending
                xportType = TRANSPORT_NONE;
            } else
            if (acctSupportsSimplex()) {
                // under Total quota, min interval expired, send Simplex
                //logDEBUG(LOGSRC,"_getTransportType: 1 LOW/SIMPLEX ...");
                xportType = TRANSPORT_SIMPLEX;
            } else
            if (acctUnderDuplexQuota()) {
                // under Total/Duplex quota and min interval expired, Simplex not supported, send Duplex
                //logDEBUG(LOGSRC,"_getTransportType: 2 LOW/DUPLEX ...");
                xportType = TRANSPORT_DUPLEX;
            } else {
                if (!acctSupportsDuplex()) {
                    logCRITICAL(LOGSRC,"Transport does not support Simplex or Duplex!!!");
                }
                // over Duplex quota (or Duplex not supported), no sending
                xportType = TRANSPORT_NONE;
            }
            break;

        // normal priority events
        case PRIORITY_NORMAL:
            if (!acctUnderTotalQuota()) {
                // over Total quota, no sending
                xportType = TRANSPORT_NONE;
            } else
            if (!acctMinIntervalExpired()) {
                // min interval has not expired, no sending
                xportType = TRANSPORT_NONE;
            } else
            if (acctUnderDuplexQuota()) {
                // under Total/Duplex quota and min interval expired, send Duplex
                //logDEBUG(LOGSRC,"_getTransportType: 1 NORMAL/DUPLEX ...");
                xportType = TRANSPORT_DUPLEX;
            } else
            if (!acctSupportsDuplex()) {
                // under Total quota, but the client doesn't support Duplex connections, send Simplex
                //logDEBUG(LOGSRC,"_getTransportType: 2 NORMAL/SIMPLEX ...");
                xportType = TRANSPORT_SIMPLEX;
            } else {
                // over Duplex quota, no sending
                xportType = TRANSPORT_NONE;
            }
            break;

        // high priority events
        case PRIORITY_HIGH:
        default: // catch-all
            if (acctUnderDuplexQuota()) { // (disregard timer interval and total quota)
                // under Duplex quota and critical event, send Duplex
                //logDEBUG(LOGSRC,"_getTransportType: 1 HIGH/DUPLEX ...");
                xportType = TRANSPORT_DUPLEX;
            } else
            if (!acctSupportsDuplex()) {
                // critical event, but the client does not support duplex connections, send Simplex
                //logDEBUG(LOGSRC,"_getTransportType: 2 HIGH/SIMPLEX ...");
                xportType = TRANSPORT_SIMPLEX;
            } else {
                // over Duplex quota, no sending
                xportType = TRANSPORT_NONE;
            }
            break;

    }
    
    //logINFO(LOGSRC,"_getTransportType: %d/%d", evPri, xportType);
    return xportType;
    
}

// ----------------------------------------------------------------------------

/* open duplex session */
static utBool _protocolDuplexTransport(ProtocolVars_t *pv)
{
    Packet_t pkt;
    
    /* open transport */
    if (!_protocolOpen(pv, TRANSPORT_DUPLEX)) {
        if (utcIsTimerExpired(pv->lastDuplexErrorTimer,300L)) {
            pv->lastDuplexErrorTimer = utcGetTimer();
            logINFO(LOGSRC,"Unable to open Duplex transport [%d]", pv->protoNdx);
        }
        return utFalse;
    }
    logINFO(LOGSRC,"Duplex start [%d] ...", pv->protoNdx);

    /* check for GPS Fix expiration ("stale") */
    if (gpsIsFixStale()) {
        // queue GPS error message
        GPSDiagnostics_t gpsDiag;
        gpsGetDiagnostics(&gpsDiag);
        if (utcGetTimeSec() > (gpsDiag.lastSampleTime + GPS_EVENT_INTERVAL)) {
            // Likely serious GPS problem.
            // We haven't received ANYTHING from the GPS reciver in the last GPS_EVENT_INTERVAL seconds
            // The GPS receiver is no longer working!
            _protocolQueueError(pv,"%2x%4u", (UInt32)ERROR_GPS_FAILURE, (UInt32)gpsDiag.lastSampleTime);
        } else {
            // The GPS receiver still appears to be working, the fix is just expired
            _protocolQueueError(pv,"%2x%4u", (UInt32)ERROR_GPS_EXPIRED, (UInt32)gpsDiag.lastValidTime);
        }
    }

    /* default speak freely permission on new connections */
    pv->speakFreely = utFalse;
    pv->speakFreelyMaxEvents = -1;
    pv->relinquishSpeakFreely = utFalse;

    /* default speak-brief on new connection */
    utBool speakFirst = utTrue;
    if (pv->isPrimary) { // first/brief primary transport
        speakFirst     = propGetBoolean(PROP_COMM_SPEAK_FIRST, utTrue); // protocol dependent
        pv->speakBrief = propGetBoolean(PROP_COMM_FIRST_BRIEF, utFalse); // protocol dependent
    } else
    if (pv->isSerial) {
        speakFirst     = utFalse;
        pv->speakBrief = utTrue;
    } else {
        speakFirst     = utTrue;
        pv->speakBrief = utFalse;
    }

    /* packet handling loop */
    utBool rtnOK       = utTrue;
    utBool keepLooping = utTrue;
    utBool firstPass   = utTrue;
    for (;keepLooping;) {
        
        /* send queued packets */
        if (firstPass) {
            firstPass = utFalse;
            if (speakFirst) {
                // client initiates conversation
                // send identification and first block of events
                // 'pv->speakFreely' is always false here
                int firstMaxEvents = -1; // <-- if 'speakBrief' is true, no events will be sent
                if (!_protocolSendAllPackets(pv, TRANSPORT_DUPLEX, pv->speakBrief, firstMaxEvents)) {
                    rtnOK = utFalse; // write error
                    break;
                }
                pv->speakBrief = utFalse;
            }
        } else
        if (pv->speakFreely) {
            // send any pending packets
            // During 'speak-freely' wait until we have something to send.
            if (_protocolHasDataToSend(pv)) {
                int dupMaxEvents = pv->speakFreelyMaxEvents;
#if defined(PROTOCOL_THREAD)
                // The thread may decide whether, or not, to relinquish 'speakFreely' permission
                if (pv->relinquishSpeakFreely) {
                    pv->speakFreely = utFalse; // relinquish speak-freely permission
                    pv->speakFreelyMaxEvents = -1;
                }
#else
                // ALWAYS relinquish 'speakFreely' permission when NOT running in a separate thread
                // otherwise the outer main loop will remain blocked.
                pv->speakFreely = utFalse; // relinquish speak-freely permission
                pv->speakFreelyMaxEvents = -1;
#endif
                if (!_protocolSendAllPackets(pv, TRANSPORT_DUPLEX, utFalse, dupMaxEvents)) {
                    rtnOK = utFalse; // write error
                    break;
                }
            }
        }
        
        /* read packet */
        int err = _protocolReadServerPacket(pv, &pkt); // <-- timeout is specified by transport
        if (err < 0) {
            // read/parse error (transport no longer open?)
            rtnOK = utFalse;
            break;
        } else 
        if (err == 0) {
            // read timeout
#if defined(PROTOCOL_THREAD)
            if (pv->speakFreely) {
                // read timeouts are allowed in 'speak-freely' mode
                continue;
            }
#endif
            if (pv->isSerial) {
                // timeouts are ignored with serial transport (only exit on transmit errors)
                continue;
            } else {
                logINFO(LOGSRC,"Duplex server read timeout [%d]", pv->protoNdx);
                // this is an error when not in 'speak-freely' mode, or not in a thread
                // otherwise we'll be blocking the mainloop for too long.
                rtnOK = utFalse;
                break;
            }
        }
        
        /* handle received packet */
        keepLooping = _protocolHandleServerPacket(pv, &pkt);

    }
    
    /* close transport */
    _protocolClose(pv, TRANSPORT_DUPLEX, utFalse);
    if (pv->isPrimary) { // record duplex connection
        // 'primary' only
        acctSetDuplexConnection();
    }
    logINFO(LOGSRC,"Duplex end [%d] ...", pv->protoNdx);
    return rtnOK;

}

/* open simplex session */
static utBool _protocolSimplexTransport(ProtocolVars_t *pv)
{
    //logINFO(LOGSRC,"Simplex ...");
    
    /* must be the primary protocol */
    if (!pv->isPrimary) { // no simplex for non-primary transport
        // Simplex only allowed for 'primary'
        return utFalse;
    }

    /* open transport */
    if (!_protocolOpen(pv, TRANSPORT_SIMPLEX)) {
        return utFalse;
    }
    
    /* check for GPS Fix expiration ("stale") */
    if (gpsIsFixStale()) {
        // queue GPS error message
        GPSDiagnostics_t gpsDiag;
        gpsGetDiagnostics(&gpsDiag);
        if (utcGetTimeSec() > (gpsDiag.lastSampleTime + GPS_EVENT_INTERVAL)) {
            // Likely serious GPS problem.
            // We haven't received ANYTHING from the GPS reciver in the last GPS_EVENT_INTERVAL seconds
            // The GPS receiver is no longer working!
            _protocolQueueError(pv,"%2x%4u", (UInt32)ERROR_GPS_FAILURE, (UInt32)gpsDiag.lastSampleTime);
        } else {
            // The GPS receiver still appears to be working, the fix is just expired
            _protocolQueueError(pv,"%2x%4u", (UInt32)ERROR_GPS_EXPIRED, (UInt32)gpsDiag.lastValidTime);
        }
    }

    /* send queued packets/events */
    int simMaxEvents = -1;
    if (!_protocolSendAllPackets(pv, TRANSPORT_SIMPLEX, utFalse, simMaxEvents)) {
        _protocolClose(pv, TRANSPORT_SIMPLEX, utFalse); // <-- will only occur if we have a buffer overflow
        return utFalse;
    }
    
    /* acknowledge sent events */
    if (_protocolClose(pv, TRANSPORT_SIMPLEX, utTrue)) {
        // - Data doesn't get transmitted until the close for Simplex connections.
        // So events should not be auto-acknowledged until the close has occured
        // and didn't get any errors. (This still doesn't guarantee that the server
        // received the data).
        // - Since most wireless data services will be placing the device behind a
        // NAT'ed router, there is no way for the server to send back a UDP
        // acknowledgement to the device.  As such, no attempt is made to read an
        // acknowledgement from the server.
        pqueResetQueue(&(pv->pendingQueue)); // remove all pending messages
        _protocolAcknowledgeToSequence(pv,SEQUENCE_ALL);
        acctSetSimplexConnection();
        return utTrue;
    } else {
        return utFalse;
    }

}

// ----------------------------------------------------------------------------

/* start transport session */
static utBool _protocolRunSession(ProtocolVars_t *pv)
{
    // Warning: If running in a multi-threaded environment make sure this function, 
    // and all functions it calls, are thread-safe!
    //logINFO(LOGSRC,"Transport session: %s", pv->xFtns->name);
    
    /* check for transport ready */
    TransportType_t xportType = TRANSPORT_NONE;
    PROTOCOL_LOCK(pv) {
        if (pv->currentTransportType != TRANSPORT_NONE) {
            xportType = pv->currentTransportType;
            PacketEncoding_t encoding = _protocolGetSupportedEncoding(pv, pv->currentEncoding);
            _protocolSetSessionEncoding(pv, xportType, encoding);
        }
    } PROTOCOL_UNLOCK(pv)
    if (xportType == TRANSPORT_NONE) {
        // not ready for connection yet
        return utFalse;
    }
    
    /* reset speak freely */
    pv->speakFreely = utFalse;
    pv->speakFreelyMaxEvents = -1;

    /* establish connections */
    if (xportType == TRANSPORT_SIMPLEX) {
        // establish Simplex communication here
        _protocolSimplexTransport(pv);
    } else 
    if (xportType == TRANSPORT_DUPLEX) {
        // establish Duplex communication here
        _protocolDuplexTransport(pv);
    }
    // If the above connections fail, the outer protocol loop will be calling this function again
    
    /* reset for next connection */
    PROTOCOL_LOCK(pv) {
        // even though the above may have failed, reset the transport type anyway.
        // it will be set again by the main thread.
        pv->currentTransportType = TRANSPORT_NONE;
    } PROTOCOL_UNLOCK(pv)
    return utTrue;

}

// ----------------------------------------------------------------------------

#ifdef PROTOCOL_THREAD

/* where the thread spends its time */
static void _protocolThreadRunnable(void *arg)
{
    ProtocolVars_t *pv = (ProtocolVars_t*)arg;

    /* protocol handler loop */
    while (pv->protoRunThread) {
        
        /* wait for transport notification */
        PROTOCOL_LOCK(pv) {
            if (pv->isSerial) {
                // Serial transports (typically Bluetooth) will continually attempt to 
                // establish a connection with the server.  The transport itself will
                // determine if Bluetooth is available and if a communication session
                // is possible.
                pv->currentEncoding      = pv->sessionEncoding;
                pv->currentTransportType = TRANSPORT_DUPLEX;
            } else {
                // wait until we have a transport request
                while (pv->currentTransportType == TRANSPORT_NONE) {
                    PROTOCOL_WAIT(pv)
                }
            }
        } PROTOCOL_UNLOCK(pv)
        
        /* exit now if this thread needs to stop */
        if (!pv->protoRunThread) {
            break;
        }
        
        /* establish connection */
        // For transports other than serial (Bluetooth), this initiates a 
        // communication session with the server and whether succeeding or failing, 
        // should not last for more than a few seconds.
        // For serial transport (Bluetooth), if a communnication session is possible
        // (ie. Bluetooth is in range), then this function should not reutrn until
        // the serial communication is no longer possible (ie. Bluetooth error).
        _protocolRunSession(pv); // Note: 'pv->protoRunThread' is not checked inside this function.
        // end of session

        /* transport was closed */
        // Wait before retrying (necessary for TRANSPORT_MEDIA_SERIAL!)
        threadSleepMS(2000L);

    } // while (pv->protoRunThread)
    
    /* only reaches here when thread terminates */
    // The following resources need to be released:
    //  - volatileQueue
    //  - pendingQueue
    //  - protocolMutex
    //  - protocolCond
    //  - ???
    logERROR(LOGSRC,"Protocol thread is terminating ...");
    pv->protoRunThread = utFalse;
    threadExit();
    
}

/* indicate thread should stop */
static void _protocolStopThread(void *arg)
{
    ProtocolVars_t *pv = (ProtocolVars_t*)arg;
    pv->protoRunThread = utFalse;
    PROTOCOL_LOCK(pv) {
        // nudge thread
        PROTOCOL_NOTIFY(pv)
    } PROTOCOL_UNLOCK(pv)
}

/* start thread */
static utBool _protocolStartThread(ProtocolVars_t *pv)
{
    /* create thread */
    pv->protoRunThread = utTrue;
    if (threadCreate(&(pv->protocolThread),&_protocolThreadRunnable,(void*)pv,pv->xFtns->name) == 0) {
        // thread started successfully
        threadAddThreadStopFtn(&_protocolStopThread,pv);
    } else {
        logCRITICAL(LOGSRC,"Unable to create protocol thread!!");
        pv->protoRunThread = utFalse;
    }
    return pv->protoRunThread;
}

#endif

// ----------------------------------------------------------------------------

static ProtocolVars_t * _protocolInitVars(int protoNdx, const TransportFtns_t *xport)
{
    
    /* constrain index and get protocol vars */
    if (protoNdx < 0) { 
        protoNdx = 0; 
    } else
    if (protoNdx >= MAX_SIMULTANEOUS_PROTOCOLS) {
        protoNdx = MAX_SIMULTANEOUS_PROTOCOLS - 1;
    }
    ProtocolVars_t *pv = _protoGetVars(LOGSRC,protoNdx);

    /* clear structure */
    memset(pv, 0, sizeof(ProtocolVars_t));

    // transport
    pv->xFtns = xport;

    // primary protocol?
    pv->protoNdx  = protoNdx;
    pv->isPrimary = (protoNdx == 0)? utTrue : utFalse;
    
    // serial transport?
    // 'Serial' transport has some special handling.
    pv->isSerial = (pv->xFtns->media == MEDIA_SERIAL)? utTrue : utFalse;

    // thread support
#ifdef PROTOCOL_THREAD
    pv->protoRunThread = utFalse; // see 'pv->protocolThread'
    threadMutexInit(&(pv->protocolMutex));
    threadConditionInit(&(pv->protocolCond));
#endif

    // pending/volatile queues
    if (pv->isPrimary) {  // queue init
        // primary
        pqueInitQueue(&(pv->volatileQueue), PRIMARY_VOLATILE_QUEUE_SIZE);
        pqueInitQueue(&(pv->pendingQueue) , PRIMARY_PENDING_QUEUE_SIZE);
        // uses standard event queue
    } else {
        // secondary
        pqueInitQueue(&(pv->volatileQueue), SECONDARY_VOLATILE_QUEUE_SIZE);
        pqueInitQueue(&(pv->pendingQueue) , SECONDARY_PENDING_QUEUE_SIZE);
#if defined(SECONDARY_SERIAL_TRANSPORT)
        pqueInitQueue(&secondaryQueue     , SECONDARY_EVENT_QUEUE_SIZE);
#endif
    }

    // identification
    pv->sendIdentification = SEND_ID_NONE;
    
    // severe error counts 
    pv->totalSevereErrorCount = 0;
    pv->severeErrorCount = 0;
    
    // session error counters 
    pv->checkSumErrorCount = 0;
    pv->invalidAcctErrorCount = 0;
    
    // speak freely permission
    // "Speak freely" is generally only used by dedicated/constant client/server connections.
    pv->speakFreely = utFalse;
    pv->speakFreelyMaxEvents = -1;
    pv->relinquishSpeakFreely = utFalse;
    // speak brief on first block
    // 'speakBrief' allows the server to set parameters before any events are transmitted
    // This means that the client will only identify itself on the first packet block.  It
    // will not attempt to send any volatile, pending, or event packets.
    pv->speakBrief = utFalse;
    
    // encoding
    if (pv->isPrimary) { // default encodings
        // primary
        pv->sessionFirstEncoding = DEFAULT_ENCODING;
        pv->sessionEncoding = DEFAULT_ENCODING;
    } else
    if (pv->xFtns->media == MEDIA_FILE) {
        // secondary: file
        pv->sessionFirstEncoding = DEFAULT_FILE_ENCODING;
        pv->sessionEncoding = DEFAULT_FILE_ENCODING;
    } else
    if (pv->xFtns->media == MEDIA_SOCKET) {
        // secondary: socket
        pv->sessionFirstEncoding = DEFAULT_SOCKET_ENCODING;
        pv->sessionEncoding = DEFAULT_SOCKET_ENCODING;
    } else
    if (pv->xFtns->media == MEDIA_SERIAL) {
        // secondary: serial (currently the only secondary used)
        pv->sessionFirstEncoding = DEFAULT_SERIAL_ENCODING;
        pv->sessionEncoding = DEFAULT_SERIAL_ENCODING;
    } else
    if (pv->xFtns->media == MEDIA_GPRS) {
        // secondary: GPRS
        pv->sessionFirstEncoding = DEFAULT_GPRS_ENCODING;
        pv->sessionEncoding = DEFAULT_GPRS_ENCODING;
    } else 
    {
        // secondary: default
        pv->sessionFirstEncoding = ENCODING_BASE64;
        pv->sessionEncoding = ENCODING_BASE64;
    }
    pv->sessionEncodingChanged = utFalse;

    // pending transport
    // Warning: When running in a multi-threaded environment, make sure that the
    // following vars are never accessed outside of a PROTOCOL_LOCK..UNLOCK block.
    pv->currentTransportType = TRANSPORT_NONE;
    pv->currentEncoding = pv->sessionFirstEncoding;

    // duplex connect error timer
    pv->lastDuplexErrorTimer = 0L;
    
    // Accumulation of read/write byte counts.
    // Notes:
    // - 'transport.c' would be a better place to put this responsibility, but it's been
    // moved here to reduce the burden on the customization of the 'transport.c' module.
    // - This module currently only counts the data bytes actually read from and written
    // to the server.  It does not take into account any TCP/UDP packet overhead.  
    // To make the actual byte counts more accurate, the transport medium service provider 
    // (ie. the wireless data plan) must be consulted to determine how they charge for
    // data transfer.
    pv->totalReadBytes = 0L;
    pv->totalWriteBytes = 0L;
    pv->sessionReadBytes = 0L;
    pv->sessionWriteBytes = 0L;
    
    return pv;
    
}

/* protocol module initialization */
// most be called once, and only once per protocol
void protocolInitialize(int protoNdx, const TransportFtns_t *xport)
{
    ProtocolVars_t *pv = _protocolInitVars(protoNdx, xport);
    
    /* init vars */
    if (pv->isPrimary) { // data stats
        // data stats recorded for 'primary' only
        pv->totalReadBytes  = propGetUInt32(PROP_COMM_BYTES_READ   , 0L); // primary only
        pv->totalWriteBytes = propGetUInt32(PROP_COMM_BYTES_WRITTEN, 0L); // primary only
    }
    
    /* start thread */
#ifdef PROTOCOL_THREAD
    _protocolStartThread(pv);
#endif

}

/* indicate transport request */
void protocolTransport(int protoNdx, PacketEncoding_t encoding)
{
    ProtocolVars_t *pv = _protoGetVars(LOGSRC,protoNdx);
    TransportType_t xportType = TRANSPORT_NONE;
    PROTOCOL_LOCK(pv) {
        if (pv->currentTransportType == TRANSPORT_NONE) {
            xportType = _getTransportType(pv);
            if (xportType != TRANSPORT_NONE) {
                pv->currentEncoding      = encoding;
                pv->currentTransportType = xportType;
                PROTOCOL_NOTIFY(pv)
            }
        }
    } PROTOCOL_UNLOCK(pv)
#if !defined(PROTOCOL_THREAD)
    if (xportType != TRANSPORT_NONE) {
        // run protocol now, if not in a separate thread
        _protocolRunSession(pv);
    }
#endif
}

// ----------------------------------------------------------------------------

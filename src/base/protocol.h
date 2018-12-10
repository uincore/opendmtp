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

#ifndef _PROTOCOL_H
#define _PROTOCOL_H
#ifdef __cplusplus
extern "C" {
#endif

#include "tools/stdtypes.h"
#include "tools/threads.h"
#include "base/pqueue.h"
#include "base/packet.h"
#include "custom/transport.h"

// ----------------------------------------------------------------------------

#define MAX_DUPLEX_EVENTS       64
#define MAX_SIMPLEX_EVENTS      8

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// number of allocated protocol instances

#if !defined(MAX_SIMULTANEOUS_PROTOCOLS)
#  define MAX_SIMULTANEOUS_PROTOCOLS    1
#endif

// ----------------------------------------------------------------------------

/* flags to indicate when/which identification packets are to be sent */
enum SendIdent_enum { 
    SEND_ID_NONE    = 0, // identification not necessary, or already sent
    SEND_ID_UNIQUE,      // sending Unique ID (if available)
    SEND_ID_ACCOUNT      // send Account/Device ID
};
typedef enum SendIdent_enum SendIdent_t;

// ----------------------------------------------------------------------------

// These are the variables used by an instance of the protocol
typedef struct {

    // transport
    const TransportFtns_t   *xFtns;

    // main transport?
    int                     protoNdx;                   // protocol index [0|1]
    utBool                  isPrimary;                  // primary protocol?
    utBool                  isSerial;                   // typically BlueTooth
    
    // thread support
#if defined(PROTOCOL_THREAD)
    utBool                  protoRunThread;
    threadThread_t          protocolThread;
    threadMutex_t           protocolMutex;
    threadCond_t            protocolCond;
#endif

    // queues
    // Define queue for holding session dependent packets such as errors, etc
    // This queue is reset before each session
    PacketQueue_t           volatileQueue;
    // Define queue for holding messages which will persist between sessions
    // This queue is reset only after messages have been successfully sent to the server
    PacketQueue_t           pendingQueue;

    // identification
    SendIdent_t             sendIdentification;         // SEND_ID_NONE

    // Warning: When running in a multi-threaded environment, make sure that the
    // following vars are never accessed outside of a PROTOCOL_LOCK..UNLOCK block.
    TransportType_t         currentTransportType;       // TRANSPORT_NONE
    PacketEncoding_t        currentEncoding;            // DEFAULT_ENCODING

    // session error counters 
    UInt16                  checkSumErrorCount;
    UInt16                  invalidAcctErrorCount;
    UInt16                  totalSevereErrorCount;
    UInt16                  severeErrorCount;

    // speak freely permission
    // "Speak freely" is generally only used by dedicated/constant client/server connections.
    utBool                  speakFreely;
    int                     speakFreelyMaxEvents;
    utBool                  relinquishSpeakFreely;
    // speak brief on first block
    // 'speakBrief' allows the server to set parameters before any events are transmitted
    // This means that the client will only identify itself on the first packet block.  It
    // will not attempt to send any volatile, pending, or event packets.
    utBool                  speakBrief;
    
    // encoding
    PacketEncoding_t        sessionFirstEncoding;       // DEFAULT_ENCODING
    PacketEncoding_t        sessionEncoding;            // DEFAULT_ENCODING
    utBool                  sessionEncodingChanged;
    
    // duplex connect error timer
    TimerSec_t              lastDuplexErrorTimer;
    
    // Accumulation of read/write byte counts.
    // Notes:
    // - 'transport.c' would be a better place to put this responsibility, but it's been
    // moved here to reduce the burden on the customization of the 'transport.c' module.
    // - This module currently only counts the data bytes actually read from and written
    // to the server.  It does not take into account any TCP/UDP packet overhead.  
    // To make the actual byte counts more accurate, the transport medium service provider 
    // (ie. the wireless data plan) must be consulted to determine how they charge for
    // data transfer.
    UInt32                  totalReadBytes;
    UInt32                  totalWriteBytes;
    UInt32                  sessionReadBytes;
    UInt32                  sessionWriteBytes;
    
} ProtocolVars_t;

// ----------------------------------------------------------------------------

/* initialize protocol */
void protocolInitialize(int protoNdx, const TransportFtns_t *xftns);

/* open transport and send packets */
void protocolTransport(int protoNdx, PacketEncoding_t encoding);

// ----------------------------------------------------------------------------

/* return true if transport is currently open (typically used for BlueTooth connections) */
utBool protocolIsOpen(int protoNdx);
utBool protocolIsSpeakFreely(int protoNdx);

// ----------------------------------------------------------------------------

/* return the event queue for the specified protocol transport */
PacketQueue_t *protocolGetEventQueue(int protoNdx);

/* queue protocol packets */
utBool protocolQueuePacket(int protoNdx, Packet_t *pkt);
utBool protocolQueueError(int protoNdx, const char *fmt, ...);
utBool protocolQueueDiagnostic(int protoNdx, const char *fmt, ...);

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif

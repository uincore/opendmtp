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

#ifndef _PQUEUE_H
#define _PQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif

#include "custom/defaults.h"

#include "tools/stdtypes.h"
#include "tools/bintools.h"
#include "tools/threads.h"

#include "base/packet.h"

// ----------------------------------------------------------------------------

typedef struct {
    utBool              queOverwrite;
    Int32               queSize;        // total item count
    Int32               queFirst;       // first index (first valid packet if != queLast)
    Int32               queLast;        // last index (always points to invalid/unallocated packet)
    Packet_t            **queue;        // malloc'ed entries (queSize + 1)
    //Packet_t          *queue;       <-- (non-malloc'ed) pointer to pre-allocated array
#ifdef PQUEUE_THREAD_LOCK
    threadMutex_t       queMutex;
#endif
} PacketQueue_t;

typedef struct {
    PacketQueue_t       *pque;
    Int32               index; // must be signed (-1 means 'no index')
} PacketQueueIterator_t;

// 'malloc' is used to maintain entries in the queue
#define PacketQueue_DEFINE(N,S)     static PacketQueue_t N;
#define PacketQueue_INIT(N,S)       pqueInitQueue(&(N),(S))
// [obsolete] (the following assumes pre-allocated entry slots: 
//#define PacketQueue_STRUCT(P,S)     { utTrue, (S), 0, 0, (P) }
//#define PacketQueue_DEFINE(N,S)     static Packet_t _##N[(S)+1]; static PacketQueue_t N=PacketQueue_STRUCT(_##N,(S)+1);
//#define PacketQueue_INIT(N,S)       pqueInitQueue(&(N),(N).queue,(N).queSize)

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void pqueInitQueue(PacketQueue_t *pq, int queSize);

void pqueLoadQueue(PacketQueue_t *pq, const char *loadFile);
void pqueResetQueue(PacketQueue_t *pq);
void pqueEnableOverwrite(PacketQueue_t *pq, utBool overwrite);

Int32 pqueGetPacketCount(PacketQueue_t *pq);
utBool pqueHasPackets(PacketQueue_t *pq);

utBool pqueAddPacket(PacketQueue_t *pq, Packet_t *pkt);
utBool pqueDeleteFirstEntry(PacketQueue_t *pq);

utBool pqueCopyQueue(PacketQueue_t *pqDest, PacketQueue_t *pqSrc, PacketPriority_t priority);

utBool pqueHasUnsentPacket(PacketQueue_t *pq);

UInt32 pqueGetFirstSentSequence(PacketQueue_t *pq);
UInt32 pqueGetLastSequence(PacketQueue_t *pq);
utBool pqueHasSentPacketWithSequence(PacketQueue_t *pq, UInt32 sequence);
PacketPriority_t pqueGetHighestPriority(PacketQueue_t *pq);

void pquePrintQueue(PacketQueue_t *pq);

// ----------------------------------------------------------------------------

PacketQueueIterator_t *pqueGetIterator(PacketQueue_t *pq, PacketQueueIterator_t *i);
utBool pqueHasNextPacket(PacketQueueIterator_t *i);
Packet_t *pqueGetNextPacket(Packet_t *pktCopy, PacketQueueIterator_t *i);

// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif

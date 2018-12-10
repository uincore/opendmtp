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
//  Packet queue manager.
//  Circular buffer for managing queued packets.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/08/01  Martin D. Flynn
//     -Added "pqueHasUnsentPacket" function
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
//     -Dropped support for non-malloc'ed event queues (all current reference
//      implementation platforms support 'malloc')
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "custom/log.h"

#include "base/packet.h"
#include "base/pqueue.h"

#include "tools/io.h"
#include "tools/strtools.h"

// ----------------------------------------------------------------------------

#ifdef PQUEUE_THREAD_LOCK
#include "tools/threads.h"
#define QUEUE_LOCK_INIT(Q)  threadMutexInit(&((Q)->queMutex));
#define QUEUE_LOCK(Q)       MUTEX_LOCK(&((Q)->queMutex));
#define QUEUE_UNLOCK(Q)     MUTEX_UNLOCK(&((Q)->queMutex));
#else
#define QUEUE_LOCK_INIT(Q)  // implement queue lock init here
#define QUEUE_LOCK(Q)       // implement queue lock here
#define QUEUE_UNLOCK(Q)     // implement queue unlock here
#endif

// ----------------------------------------------------------------------------

/* initialize packet queue */
void pqueInitQueue(PacketQueue_t *pq, int queSize)
{
    // This implementation utilizes an array of pointers to Packet_t structures.
    // Each structure is a malloc'ed entry and consumes only as much memory as is
    // required to contain the useful parts of the data payload.  Typically, this
    // should be able to hold up to 3-4 times as many packets in the queue compared
    // to the non-malloc'ed version below.
    // TotalConsumeQueuedMemory = 
    //      sizeof(PacketQueue_t) + 
    //      ((queSize + 1) * sizeof(Packet_t*)) +   // pointer to Packet_t
    //      (MemoryRequiredToHoldPacketsCurrentlyInQueue);
    if (pq) {
        memset(pq, 0, sizeof(PacketQueue_t));
        QUEUE_LOCK_INIT(pq)
        pq->queSize  = queSize + 1; // one element is used to separate the first entry from the last
        pq->queue    = (Packet_t**)calloc(pq->queSize, sizeof(pq->queue));
        pq->queFirst = 0L;
        pq->queLast  = 0L;
        pq->queOverwrite = utTrue; // overwrites allowed by default
    }
}

/* reset queue to 'empty' state */
void pqueResetQueue(PacketQueue_t *pq)
{
    if (pq) {
        QUEUE_LOCK(pq) {
            pq->queFirst = 0L;
            pq->queLast  = 0L;
            pq->queOverwrite = utTrue; // overwrites allowed by default
        } QUEUE_UNLOCK(pq)
    }
}

/* enable/disable overwrite of oldest entry */
void pqueEnableOverwrite(PacketQueue_t *pq, utBool overwrite)
{
    if (pq) {
        QUEUE_LOCK(pq) {
            pq->queOverwrite = overwrite;
        } QUEUE_UNLOCK(pq)
    }
}

// ----------------------------------------------------------------------------

/* return the queue index of the next item in the list following the specified index */
static Int32 _pqueNextIndex(PacketQueue_t *pq, Int32 ndx)
{
    return ((ndx + 1L) < pq->queSize)? (ndx + 1L) : 0L;
}

/* return the queue index of the prior item in the list preceeding the specified index */
static Int32 _pquePriorIndex(PacketQueue_t *pq, Int32 ndx)
{
    return ((ndx - 1L) < 0L)? (pq->queSize - 1L) : (ndx - 1L);
}

/* return the number of current entries in the queue */
Int32 pqueGetPacketCount(PacketQueue_t *pq)
{
    Int32 len = 0L;
    if (pq) {
        QUEUE_LOCK(pq) {
            if (pq->queLast >= pq->queFirst) {
                len = pq->queLast - pq->queFirst;
            } else {
                len = pq->queSize - (pq->queFirst - pq->queLast);
            }
        } QUEUE_UNLOCK(pq)
    } 
    return len;
}

/* return true if the queue is non-empty */
utBool pqueHasPackets(PacketQueue_t *pq)
{
    return (pq && (pqueGetPacketCount(pq) > 0L))? utTrue : utFalse;
}

// ----------------------------------------------------------------------------

static Packet_t *_pqueGetPacketAt(PacketQueue_t *pq, Int32 entry)
{
    return pq->queue[entry];
    //return &(pq->queue[entry]); <-- non-malloc'ed
}

static void _pqueFreePacketAt(PacketQueue_t *pq, Int32 entry)
{
    if (pq->queue[entry]) {
        //pktPrintPacket(pq->queue[entry], "Free Packet", ENCODING_CSV);
        free(pq->queue[entry]);
        pq->queue[entry] = (Packet_t*)0;
    }
    //memset(&(pq->queue[entry]), 0, sizeof(Packet_t)); <-- non-malloc'ed
}

static void _pqueSetPacketAt(PacketQueue_t *pq, Int32 entry, Packet_t *pkt, utBool saveToFile)
{
    _pqueFreePacketAt(pq, entry);
    int pktLen = ((UInt8*)pkt->data - (UInt8*)pkt) + pkt->dataLen;
    pq->queue[entry] = (Packet_t*)malloc(pktLen); // [MALLOC]
    if (pq->queue[entry]) {
        memcpy(pq->queue[entry], pkt, pktLen);
        //memcpy(&(pq->queue[entry]), pkt, sizeof(Packet_t)); <-- non-malloc'ed
    } else {
        logCRITICAL(LOGSRC,"OUT OF MEMORY!!");
    }
}

// ----------------------------------------------------------------------------

/* allocate an entry from the queue */
static Int32 _pqueAllocateNextEntry(PacketQueue_t *pq)
{
    
    /* save entry */
    // last does not currently point to a valid entry
    Int32 newEntry = pq->queLast;
    Int32 newLast  = _pqueNextIndex(pq, newEntry);
    
    /* check for overflow */
    if (newLast == pq->queFirst) {
        // We've run out of space in the queue
        if (pq->queOverwrite) {
            // make room for the newest entry by deleting the oldest entry
            logWARNING(LOGSRC,"Packet queue overflow - overwriting oldest");
            _pqueFreePacketAt(pq, pq->queFirst);
            pq->queFirst = _pqueNextIndex(pq, pq->queFirst);
        } else {
            // overwrites not allowed, the newest entry is ignored
            logWARNING(LOGSRC,"Packet queue overflow - discarding latest");
            return -1L;
        }
    }
    pq->queLast = newLast;
    
    /* clear/return the entry */
    return newEntry;
    
}

/* add (copy) the specified packet to the queue */
utBool pqueAddPacket(PacketQueue_t *pq, Packet_t *pkt)
{
    utBool didAdd = utFalse;
    if (pq && pkt) {
        QUEUE_LOCK(pq) {
            Int32 entry = _pqueAllocateNextEntry(pq);
            if (entry >= 0L) {
                _pqueSetPacketAt(pq, entry, pkt, utTrue);
                didAdd = utTrue;
            } else {
                // buffer overflow
            }
        } QUEUE_UNLOCK(pq)
    }
    //Int32 pqlen = pqueGetPacketCount(pq);
    //if (pqlen == pq->queSize - 1) { pquePrintQueue(pq); }
    return didAdd;
}

// ----------------------------------------------------------------------------

/* copy contents of one queue to another (all packets >= specified priority) */
utBool pqueCopyQueue(PacketQueue_t *pqDest, PacketQueue_t *pqSrc, PacketPriority_t priority)
{
    if (pqDest && pqSrc) {
        Packet_t *quePkt;
        PacketQueueIterator_t srcIter;
        pqueGetIterator(pqSrc, &srcIter);
        utBool rtn = utTrue;
        for (; (quePkt = pqueGetNextPacket((Packet_t*)0, &srcIter)) ;) {
            if (quePkt->priority >= priority) {
                if (!pqueAddPacket(pqDest, quePkt)) {
                    // destination queue overflow
                    rtn = utFalse;
                    break;
                }
            }
        }
        return rtn;
    } 
    return utFalse;
}

// ----------------------------------------------------------------------------

/* delete the first (oldest) entry in the queue */
utBool pqueDeleteFirstEntry(PacketQueue_t *pq)
{
    utBool rtn = utFalse;
    if (pq) {
        QUEUE_LOCK(pq) {
            if (pq->queLast != pq->queFirst) {
                _pqueFreePacketAt(pq, pq->queFirst);
                pq->queFirst = _pqueNextIndex(pq, pq->queFirst);
                rtn = utTrue;
            } else {
                rtn = utFalse;
            }
        } QUEUE_UNLOCK(pq)
    }
    return rtn;
}

// ----------------------------------------------------------------------------

/* return true if the queue contains any unsent Packet entries */
// need to optimize this by just checking the last packet
utBool pqueHasUnsentPacket(PacketQueue_t *pq)
{
    utBool found = utFalse;
    if (pq) {
        QUEUE_LOCK(pq) {
            Int32 m = pq->queFirst;
            while (m != pq->queLast) {
                Packet_t *pkt = _pqueGetPacketAt(pq,m);
                if (!pkt->sent) {
                    found = utTrue;
                    break;
                }
                m = _pqueNextIndex(pq, m);
            }
        } QUEUE_UNLOCK(pq)
    }
    return found;
}

// ----------------------------------------------------------------------------

/* return the first sent sequence in the queue */ 
UInt32 pqueGetFirstSentSequence(PacketQueue_t *pq)
{
    UInt32 seq = SEQUENCE_ALL;
    if (pq) {
        QUEUE_LOCK(pq) {
            Int32 m = pq->queFirst;
            if (m != pq->queLast) {
                Packet_t *pkt = _pqueGetPacketAt(pq,m);
                if (pkt->sent) {
                    seq = pkt->sequence;
                }
            }
        } QUEUE_UNLOCK(pq)
    }
    return seq;
}

/* return the last sequence in the queue */ 
UInt32 pqueGetLastSequence(PacketQueue_t *pq)
{
    UInt32 seq = SEQUENCE_ALL;
    if (pq) {
        QUEUE_LOCK(pq) {
            if (pq->queFirst != pq->queLast) { // not empty
                Packet_t *pkt = _pqueGetPacketAt(pq, _pquePriorIndex(pq, pq->queLast));
                seq = pkt->sequence;
            }
        } QUEUE_UNLOCK(pq)
    }
    return seq;
}

/* return true if the queue contains a Packet entry that has been sent and 
** has the specified sequence number */
utBool pqueHasSentPacketWithSequence(PacketQueue_t *pq, UInt32 sequence)
{
    utBool found = utFalse;
    if (pq) {
        QUEUE_LOCK(pq) {
            Int32 m = pq->queFirst;
            while (m != pq->queLast) {
                Packet_t *pkt = _pqueGetPacketAt(pq,m);
                if (!pkt->sent) {
                    // If we run into an unsent packet, then all following packets have also not been sent.
                    break;
                } else
                if (sequence == SEQUENCE_ALL) {
                    // If sequence is SEQUENCE_ALL, then return true if we have ANY sent packets
                    found = utTrue;
                    break;
                } else
                if (pkt->sequence == SEQUENCE_ALL) {
                    // we don't know what the sequence of this packet is, assume it's a match
                    found = utTrue;
                    break;
                } else
                if (pkt->sequence == sequence) {
                    // we've found a matching packet
                    found = utTrue;
                    break;
                }
                m = _pqueNextIndex(pq, m);
            }
        } QUEUE_UNLOCK(pq)
    }
    return found;
}

// ----------------------------------------------------------------------------

/* return the highest priority found in the queue */
PacketPriority_t pqueGetHighestPriority(PacketQueue_t *pq)
{
    PacketPriority_t maxPri = PRIORITY_NONE;
    if (pq) {
        QUEUE_LOCK(pq) {
            Int32 m = pq->queFirst;
            while (m != pq->queLast) {
                Packet_t *pkt = _pqueGetPacketAt(pq,m);
                if (pkt->priority > maxPri) {
                    maxPri = pkt->priority;
                }
                m = _pqueNextIndex(pq, m);
            }
        } QUEUE_UNLOCK(pq)
    }
    return maxPri;
}

// ----------------------------------------------------------------------------

void pquePrintQueue(PacketQueue_t *pq)
{
    if (pq) {
        int i;
        char m[10];
        PacketQueueIterator_t iter;
        pqueGetIterator(pq, &iter);
        for (i = 0;;i++) {
            Packet_t *pkt = pqueGetNextPacket((Packet_t*)0, &iter);
            if (!pkt) { break; }
            sprintf(m, "%d", i);
            pktPrintPacket(pkt, m, ENCODING_CSV);
        }
    }
}

// ----------------------------------------------------------------------------

/* initialize and return an interator for the queue */
PacketQueueIterator_t *pqueGetIterator(PacketQueue_t *pq, PacketQueueIterator_t *i)
{
    memset(i, 0, sizeof(PacketQueueIterator_t));
    i->pque  = pq;
    i->index = -1L;
    return i;
}

/* return true if the iterator has at least one more entry available */
utBool pqueHasNextPacket(PacketQueueIterator_t *i)
{
    utBool rtn = utFalse;
    if (i) {
        PacketQueue_t *pq = i->pque;
        QUEUE_LOCK(pq) {
            Int32 n = i->index;
            if (n < 0L) {
                n = pq->queFirst;
            } else
            if (n != pq->queLast) {
                n = _pqueNextIndex(pq, n);
            }
            rtn = (n != pq->queLast)? utTrue : utFalse;
        } QUEUE_UNLOCK(pq)
    }
    return rtn;
}

/* return the next Packet in the queue as specified by the iterator, or return
** null if there are no more entries in the queue */
Packet_t *pqueGetNextPacket(Packet_t *pktCopy, PacketQueueIterator_t *i)
{
    Packet_t *pkt = (Packet_t*)0;
    if (i) {
        PacketQueue_t *pq = i->pque;
        QUEUE_LOCK(pq) {
            if (i->index < 0L) {
                i->index = pq->queFirst;
            } else
            if (i->index != pq->queLast) {
                i->index = _pqueNextIndex(pq, i->index);
            }
            pkt = (i->index != pq->queLast)? _pqueGetPacketAt(pq,i->index) : (Packet_t*)0;
            if (pkt && pktCopy) {
                memset(pktCopy, 0, sizeof(Packet_t));
                int pktLen = ((UInt8*)pkt->data - (UInt8*)pkt) + pkt->dataLen;
                memcpy(pktCopy, pkt, pktLen);
                pkt = pktCopy;
            }
        } QUEUE_UNLOCK(pq)
    }
    return pkt;
}

// ----------------------------------------------------------------------------

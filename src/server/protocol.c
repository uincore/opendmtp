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
//  Server DMTP communication protocol handler
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/29  Martin D. Flynn
//     -Added header file "io.h"
//  2006/02/12  Martin D. Flynn
//     -Added header "cerrors.h"
//     -Request GPS diagnostic info whem GPS error/failure received.
//  2007/01/28  Martin D. Flynn
//     -Reset 'revoke speak-freely' timer each time we hear from the client.
//     -Maintain 'lastEventTimer' to acknowledge events in speak-freely mode
//      if we don't get an EOB from the client.
//     -Added support for a single queued 'pending' packet for transmission to
//      the client.
//     -Moved the handling of events, diagnostic packets, etc, to external
//      callback functions (thus making this module much more customizable for 
//      specific applications).
// ----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/gpstools.h"
#include "tools/io.h"
#include "tools/threads.h"

#include "base/props.h"
#include "base/statcode.h"

#include "server/server.h"
#include "server/serrors.h"
#include "server/cerrors.h"
#include "server/packet.h"
#include "server/events.h"
#include "server/log.h"
#include "server/protocol.h"
#include "server/upload.h"

// ----------------------------------------------------------------------------

#define REVOKE_SPEAK_FREELY_INTERVAL   HOUR_SECONDS(24) // 60L

// ----------------------------------------------------------------------------

static UInt32 lastEventSequence     = 0L;
static int lastEventSeqLen          = 0;
static TimerSec_t lastEventTimer    = 0L;

static utBool clientKeepAlive       = utTrue;   // utFalse
static utBool clientSpeaksFirst     = utFalse;  // utTrue

// ----------------------------------------------------------------------------

static utBool serverNeedsMoreInfo   = utFalse;

/* indicate to server that we are expecting more information from the client */
// When called, an end-of-block will be sent, rather than an end-of-transmission
void protSetNeedsMoreInfo()
{
    serverNeedsMoreInfo = utTrue;
}

// ----------------------------------------------------------------------------

static utBool stopSpeakFreely       = utFalse;
static utBool startSpeakFreely      = utFalse;
static utBool isSpeakFreelyMode     = utFalse;
static int    speakFreelyMaxEvents  = -1;

/* set client speakFreely mode */
void protSetSpeakFreelyMode(utBool mode, int maxEvents)
{
    if (mode) {
        // start speakFreely
        if (!isSpeakFreelyMode) {
            startSpeakFreely = utTrue;
        }
        speakFreelyMaxEvents = maxEvents;
    } else {
        // stop speakFreely
        if (isSpeakFreelyMode) {
            stopSpeakFreely = utTrue;
        }
    }
}

// ----------------------------------------------------------------------------

#define PENDING_QUE_SIZE    30 // <-- will hold a maximum of (SIZE - 1) packets
static int                  pendingQueFirst = 0L;
static int                  pendingQueLast  = 0L;
static Packet_t             pendingQue[PENDING_QUE_SIZE];
static threadMutex_t        pendingMutex;
#define PENDING_LOCK        MUTEX_LOCK(&pendingMutex);
#define PENDING_UNLOCK      MUTEX_UNLOCK(&pendingMutex);

utBool protAddPendingPacket(Packet_t *pkt)
{
    if (pkt) {
        utBool rtn = utFalse;
        PENDING_LOCK {
            int newLast = ((pendingQueLast + 1L) < PENDING_QUE_SIZE)? (pendingQueLast + 1L) : 0L;
            if (newLast != pendingQueFirst) {
                memcpy(&pendingQue[pendingQueLast], pkt, sizeof(Packet_t));
                pendingQueLast = newLast;
                logINFO(LOGSRC,"Pending packet set ...");
                rtn = utTrue;
            } else {
                logWARNING(LOGSRC,"Pending packet queue is full ...");
                rtn = utFalse;
            }
        } PENDING_UNLOCK
        return rtn;
    } else {
        logWARNING(LOGSRC,"Null packet specified ...");
        return utFalse;
    }
}

static Packet_t *protGetPendingPacket(Packet_t *pkt)
{
    if (pkt) {
        Packet_t *rtn = (Packet_t*)0;
        PENDING_LOCK {
            if (pendingQueFirst != pendingQueLast) {
                int newFirst = ((pendingQueFirst + 1L) < PENDING_QUE_SIZE)? (pendingQueFirst + 1L) : 0L;
                memcpy(pkt, &pendingQue[pendingQueFirst], sizeof(Packet_t));
                pendingQueFirst = newFirst;
                rtn = pkt;
            } else {
                rtn = (Packet_t*)0;
            }
        } PENDING_UNLOCK
        return rtn;
    } else {
        return (Packet_t*)0;
    }
}

static utBool protHasPendingPackets()
{
    utBool rtn = utFalse;
    PENDING_LOCK {
        rtn = (pendingQueFirst != pendingQueLast)? utTrue : utFalse;
    } PENDING_UNLOCK
    return rtn;
}

// ----------------------------------------------------------------------------

#if defined(INCLUDE_UPLOAD)
static const char *pendingFileUpload = (char*)0;
static const char *pendingFileClient = (char*)0;
utBool protSetPendingFileUpload(const char *file, const char *cliFile)
{
    logINFO(LOGSRC,"Pending 'File' Upload set ...");
    pendingFileUpload = file;
    pendingFileClient = cliFile;
    return utTrue;
}
#endif

// ----------------------------------------------------------------------------

static char clientAccountID[MAX_ID_SIZE + 1];
static char clientDeviceID[MAX_ID_SIZE + 1];

const char *protGetAccountID()
{
    return clientAccountID;
}

const char *protGetDeviceID()
{
    return clientDeviceID;
}

// ----------------------------------------------------------------------------

utBool protUploadPacket(const UInt8 *val, UInt8 valLen)
{
    if (serverIsOpen()) {
        return serverWritePacketFmt(PKT_SERVER_FILE_UPLOAD, "%*b", (int)valLen, val);
    } else {
        return utFalse;
    }
}

// ----------------------------------------------------------------------------

utBool protGetPropValue(UInt16 propKey)
{
    if (serverIsOpen()) {
        return serverWritePacketFmt(PKT_SERVER_GET_PROPERTY, "%2x", (UInt32)propKey);
    } else {
        return utFalse;
    }
}

// ----------------------------------------------------------------------------

utBool protSetPropBinary(UInt16 propKey, const UInt8 *val, UInt8 valLen)
{
    if (serverIsOpen()) {
        //logINFO(LOGSRC,"Setting binary property value: [%d]", valLen);
        return serverWritePacketFmt(PKT_SERVER_SET_PROPERTY, "%2x%*b", (UInt32)propKey, (int)valLen, val);
    } else {
        return utFalse;
    }
}

utBool protSetPropString(UInt16 propKey, const UInt8 *val)
{
    int valLen = strlen(val);
    return protSetPropBinary(propKey, val, valLen);
}

// ----------------------------------------------------------------------------

static utBool _protSetPropUInt32(UInt16 propKey, UInt32 *val, int count, int blen)
{
    if (serverIsOpen() && (count > 0) && (blen >= 1) && (blen <= 4)) {
        UInt32 pk = (UInt32)propKey;
        if (count == 1) {
            return serverWritePacketFmt(PKT_SERVER_SET_PROPERTY, "%2x%*x", pk, blen, (UInt32)val[0]);
        } else {
            UInt8 buf[PACKET_MAX_PAYLOAD_LENGTH];
            Buffer_t bb, *dst = binBuffer(&bb, buf, sizeof(buf), BUFFER_DESTINATION);
            int i;
            for (i = 0; i < count; i++) {
                binBufPrintf(dst, "%*x", blen, (UInt32)val[i]);
            }
            int valLen = BUFFER_DATA_LENGTH(dst);
            return serverWritePacketFmt(PKT_SERVER_SET_PROPERTY, "%2x%*b", pk, valLen, buf);
        }
    } else {
        return utFalse;
    }
}

utBool protSetPropUInt32Arr(UInt16 propKey, UInt32 *val32, int count)
{
    return _protSetPropUInt32(propKey, val32, count, 4);
}
utBool protSetPropUInt32(UInt16 propKey, UInt32 val32)
{
    return _protSetPropUInt32(propKey, &val32, 1, 4);
}

utBool protSetPropUInt16Arr(UInt16 propKey, UInt16 *val, int count)
{
    int i, m = 8;
    UInt32 val32[8];
    for (i = 0; (i < count) && (i < m); i++) { val32[i] = (UInt32)val[i]; }
    return _protSetPropUInt32(propKey, val32, ((count <= m)? count : m), 2);
}
utBool protSetPropUInt16(UInt16 propKey, UInt16 val)
{
    UInt32 val32 = (UInt32)val;
    return _protSetPropUInt32(propKey, &val32, 1, 2);
}

utBool protSetPropUInt8Arr(UInt16 propKey, UInt8 *val, int count)
{
    int i, m = 8;
    UInt32 val32[8];
    for (i = 0; (i < count) && (i < m); i++) { val32[i] = (UInt32)val[i]; }
    return _protSetPropUInt32(propKey, val32, ((count <= m)? count : m), 1);
}
utBool protSetPropUInt8(UInt16 propKey, UInt8 val)
{
    UInt32 val32 = (UInt32)val;
    return _protSetPropUInt32(propKey, &val32, 1, 1);
}

// ----------------------------------------------------------------------------

static protEventCallbackFtn_t  ftnEventHandler = 0;

void protocolSetEventHandler(protEventCallbackFtn_t ftn)
{
    ftnEventHandler = ftn;
}

static void protocolHandleEvent(Packet_t *pkt, Event_t *ev)
{
    if (ftnEventHandler) {
        (*ftnEventHandler)(pkt, ev);
    }
}

// ----------------------------------------------------------------------------

static protDataCallbackFtn_t  ftnPropertyHandler = 0;

void protocolSetPropertyHandler(protDataCallbackFtn_t ftn)
{
    ftnPropertyHandler = ftn;
}

static void protocolHandleProperty(UInt16 propKey, const UInt8 *propData, UInt16 propDataLen)
{
    if (ftnPropertyHandler) {
        (*ftnPropertyHandler)(propKey, propData, propDataLen);
    }
}

// ----------------------------------------------------------------------------

static protDataCallbackFtn_t  ftnDiagHandler = 0;

void protocolSetDiagHandler(protDataCallbackFtn_t ftn)
{
    ftnDiagHandler = ftn;
}

static void protocolHandleDiag(UInt16 diagKey, const UInt8 *diagData, UInt16 diagDataLen)
{
    if (ftnDiagHandler) {
        (*ftnDiagHandler)(diagKey, diagData, diagDataLen);
    }
}

// ----------------------------------------------------------------------------

static protDataCallbackFtn_t  ftnErrorHandler = 0;

void protocolSetErrorHandler(protDataCallbackFtn_t ftn)
{
    ftnErrorHandler = ftn;
}

static void protocolHandleError(UInt16 errKey, const UInt8 *errData, UInt16 errDataLen)
{
    if (ftnErrorHandler) {
        (*ftnErrorHandler)(errKey, errData, errDataLen);
    }
}

// ----------------------------------------------------------------------------

static protClientInitCallbackFtn_t  ftnClientInitHandler = 0;

void protocolSetClientInitHandler(protClientInitCallbackFtn_t ftn)
{
    ftnClientInitHandler = ftn;
}

static void protocolHandleClientInit()
{
    
    /* external client init */
    if (ftnClientInitHandler) {
        (*ftnClientInitHandler)();
    }
    
    /* send pending packets */
    if (protHasPendingPackets()) {
        logINFO(LOGSRC,"Sending pending packets during client initialization ...");
        Packet_t pkt;
        while (protGetPendingPacket(&pkt)) {
            serverWritePacket(&pkt);
        }
    }
    
}

// ----------------------------------------------------------------------------

/* protocol loop entry point */
void protocolLoop(
    const char *portName, utBool portLog,
    utBool cliKeepAlive, 
    utBool cliSpeaksFirst)
{
    TimerSec_t revokeSpeakFreelyTimer = 0L;
    UInt32 haveEvents = 0L;
    Packet_t packet, *pkt = &packet;
    utBool clientNeedsInit = utFalse;
    
    /* global states */
    clientKeepAlive = cliKeepAlive; // utFalse
    clientSpeaksFirst = cliSpeaksFirst; // utTrue;
    
    /* pending packet mutex init */
    threadMutexInit(&pendingMutex);
        
    /* init */
    serverInitialize();
    lastEventSequence = 0L;
    lastEventSeqLen   = 0;
    lastEventTimer    = 0L;

    /* loop forever */
    for (;;) {
        
        /* open port */
        if (!serverIsOpen()) {
            logINFO(LOGSRC,"Openning server port: %s", portName);
            if (!serverOpen(portName, portLog)) {
                // Unable to open/accept client (message already displayed)
                /* sleep and try again */
                threadSleepMS(3000L);
                continue;
            }
            logINFO(LOGSRC,"Server port opened: %s", portName);
            *clientAccountID = 0;
            *clientDeviceID = 0;
            revokeSpeakFreelyTimer = utcGetTimer();
            if (!clientSpeaksFirst) {
                // If the client does not speak first, then we must nudge the client
                // to let him know to speak now.
                serverWritePacketFmt(PKT_SERVER_EOB_DONE,"%1u",(UInt32)0L);
                isSpeakFreelyMode = utFalse;
            }
            // Send any client initialization at the start of each new connection
            clientNeedsInit = utTrue; // clientSpeaksFirst;
            serverNeedsMoreInfo = utFalse;
        }
        
        /* send pending packets, etc. */
        if (cliKeepAlive) {
#if defined(INCLUDE_UPLOAD)
            if (pendingFileUpload) {
                utBool rtn = utFalse;
                if (pendingFileClient && *pendingFileClient) {
                    logINFO(LOGSRC,"Uploading file: %s", pendingFileUpload);
                    rtn = uploadSendFile(pendingFileUpload, pendingFileClient);
                } else {
                    logINFO(LOGSRC,"Uploading encoded file: %s", pendingFileUpload);
                    rtn = uploadSendEncodedFile(pendingFileUpload);
                }
                if (!rtn) {
                    logERROR(LOGSRC,"Upload failed!");
                }
                pendingFileUpload = (char*)0;
                pendingFileClient = (char*)0;
            } else
#endif
            if (protHasPendingPackets()) {
                logINFO(LOGSRC,"Sending pending packet during client keep-alive ...");
                Packet_t pkt;
                while (protGetPendingPacket(&pkt)) {
                    serverWritePacket(&pkt);
                }
            } else 
            if (stopSpeakFreely) {
                if (isSpeakFreelyMode) {
                    serverWritePacketFmt(PKT_SERVER_EOB_DONE,"%1u",(UInt32)0L);
                    isSpeakFreelyMode = utFalse;
                }
                stopSpeakFreely = utFalse;
            } else
            if (startSpeakFreely) {
                if (!isSpeakFreelyMode) {
                    if (speakFreelyMaxEvents >= 0) {
                        serverWritePacketFmt(PKT_SERVER_EOB_SPEAK_FREELY,"%1u",(UInt32)speakFreelyMaxEvents);
                    } else {
                        serverWritePacketFmt(PKT_SERVER_EOB_SPEAK_FREELY,"");
                    }
                    isSpeakFreelyMode = utTrue;
                }
                startSpeakFreely = utFalse;
            }
        }

        /* read client packet */
        int err = serverReadPacket(pkt);
        //logINFO(LOGSRC,"Client Packet HEADER=%04X", pkt->hdrType);
        if (err == SRVERR_TRANSPORT_ERROR) {
            logERROR(LOGSRC,"Read error (EOF?)");
            serverClose();
            continue;
        } else
        if (err == SRVERR_TIMEOUT) {
            // read timeout
            if (!clientKeepAlive) {
                serverWritePacketFmt(PKT_SERVER_EOT,"");
                logINFO(LOGSRC,"End-Of-Transmission\n\n");
                serverClose();
            } else
            if (clientNeedsInit) {
                // we haven't heard from the client, thus we haven't initialized it either
                // nudge the client again to get him to speak
                logINFO(LOGSRC,"Init Client");
                serverWritePacketFmt(PKT_SERVER_EOB_DONE,"%1u",(UInt32)0L);
                isSpeakFreelyMode = utFalse;
            } else
            if ((lastEventTimer != 0L) && utcIsTimerExpired(lastEventTimer,3L)) {
                // Acknowledge the events that we've received
                // [Note: Only needed if client never releases 'speakFreely' rights]
                logWARNING(LOGSRC,"Read timeout, ackowledging received events");
                Packet_t pkt;
                if (lastEventSeqLen > 0) {
                    pktInit(&pkt, PKT_SERVER_ACK, "%*x", lastEventSeqLen, lastEventSequence);
                } else {
                    pktInit(&pkt, PKT_SERVER_ACK, "");
                }
                serverWritePacket(&pkt);
                haveEvents = 0;
                lastEventTimer = 0L;
            } else
            if (utcIsTimerExpired(revokeSpeakFreelyTimer,REVOKE_SPEAK_FREELY_INTERVAL)) {
                // read timeout, revoke speak-freely (ping client)
                //logDEBUG(LOGSRC,"Client Timeout");
                serverWritePacketFmt(PKT_SERVER_EOB_DONE,"%1u",(UInt32)0L);
                isSpeakFreelyMode = utFalse;
                revokeSpeakFreelyTimer = utcGetTimer();
            }
            continue;
        } else
        if (err != SRVERR_OK) {
            // SRVERR_CHECKSUM_FAILED
            // SRVERR_PARSE_ERROR
            // SRVERR_PACKET_LENGTH
            logERROR(LOGSRC,"Checksum error");
            // The remainder of the session is suspect, flush the rest
            serverWritePacketFmt(PKT_SERVER_EOT,"");
            logINFO(LOGSRC,"End-Of-Transmission\n\n");
            serverClose();
            continue;
        }
        
        /* reset "revoke speak-freely" timer */
        // reset timer when we hear from the client
        revokeSpeakFreelyTimer = utcGetTimer();

        /* print received packet */
        pktPrintPacket(pkt, "[RX]", ENCODING_CSV);
        
        /* handle event packet */
        ClientPacketType_t pht = pkt->hdrType;
        if (((pht >= PKT_CLIENT_FIXED_FMT_STD  ) && (pht <= PKT_CLIENT_FIXED_FORMAT_F )) ||
            ((pht >= PKT_CLIENT_DMTSP_FORMAT_0 ) && (pht <= PKT_CLIENT_DMTSP_FORMAT_F )) ||
            ((pht >= PKT_CLIENT_CUSTOM_FORMAT_0) && (pht <= PKT_CLIENT_CUSTOM_FORMAT_F))   ) {
            Event_t ev;
            if (evParseEventPacket(pkt, &ev)) {
                if ((lastEventSequence > 0L) && ((lastEventSequence + 1L) != ev.sequence)) {
                    logERROR(LOGSRC,"********************************************************");
                    logERROR(LOGSRC,"Possible Event Data Loss");
                    logERROR(LOGSRC,"Expected sequence: 0x%04X", (lastEventSequence + 1L));
                    logERROR(LOGSRC,"Found sequence...: 0x%04X", ev.sequence);
                    logERROR(LOGSRC,"********************************************************");
                }
                lastEventSequence = ev.sequence;
                lastEventSeqLen   = ev.seqLen;
                protocolHandleEvent(pkt, &ev);
            }
            haveEvents++;
            lastEventTimer = utcGetTimer();
            continue;
        } 
        /* handle other */
        switch ((UInt16)pkt->hdrType) {
            
            case PKT_CLIENT_UNIQUE_ID:
                // ignore
                break;

            case PKT_CLIENT_ACCOUNT_ID:
                memset(clientAccountID, 0, sizeof(clientAccountID));
                binScanf(pkt->data, pkt->dataLen, "%*s", MAX_ID_SIZE, clientAccountID);
                strTrimTrailing(clientAccountID);
                // protocolHandleAccountID(clientAccountID);
                break;

            case PKT_CLIENT_DEVICE_ID:
                memset(clientDeviceID, 0, sizeof(clientDeviceID));
                binScanf(pkt->data, pkt->dataLen, "%*s", MAX_ID_SIZE, clientDeviceID);
                strTrimTrailing(clientDeviceID);
                logINFO(LOGSRC,"Client Account/Device: %s/%s\n", clientAccountID, clientDeviceID);
                // protocolHandleDeviceID(clientDeviceID);
                break;
                
            case PKT_CLIENT_PROPERTY_VALUE:
                if (pkt->dataLen >= 2) {
                    UInt32 propKey = 0L;
                    binScanf(pkt->data, pkt->dataLen, "%2x", &propKey);
                    protocolHandleProperty((UInt16)propKey, pkt->data + 2, (UInt16)(pkt->dataLen - 2));
                } else {
                    // invalid property packet (just ignore)
                }
                break;
                
            case PKT_CLIENT_DIAGNOSTIC:
                if (pkt->dataLen >= 2) {
                    UInt32 diagKey = 0L;
                    binScanf(pkt->data, pkt->dataLen, "%2x", &diagKey);
                    protocolHandleDiag((UInt16)diagKey, pkt->data + 2, (UInt16)(pkt->dataLen - 2));
                } else {
                    // invalid diagnostic packet (just ignore)
                }
                break;
                
            case PKT_CLIENT_ERROR:
                if (pkt->dataLen >= 2) {
                    UInt32 errKey = 0L;
                    binScanf(pkt->data, pkt->dataLen, "%2x", &errKey);
                    protocolHandleError((UInt16)errKey, pkt->data + 2, (UInt16)(pkt->dataLen - 2));
                } else {
                    // invalid error packet (just ignore)
                }
                break;
                
            case PKT_CLIENT_EOB_DONE:
            case PKT_CLIENT_EOB_MORE:
                // NOTE: any client supplied Fletcher checksum is ignored here.
                serverReadFlush(); // flush any remaining byte in the client input queue
                if (haveEvents > 0) {
                    // Acknowledge the events that we've received
                    Packet_t pkt;
                    if (lastEventSeqLen > 0) {
                        pktInit(&pkt, PKT_SERVER_ACK, "%*x", lastEventSeqLen, lastEventSequence);
                    } else {
                        pktInit(&pkt, PKT_SERVER_ACK, "");
                    }
                    serverWritePacket(&pkt);
                    haveEvents = 0;
                    lastEventTimer = 0L;
                }
                if (clientNeedsInit) {
                    // send any desired client initialization
                    clientNeedsInit = utFalse;
                    protocolHandleClientInit();
                }
                if (clientKeepAlive) {
                    if (serverNeedsMoreInfo) {
                        serverWritePacketFmt(PKT_SERVER_EOB_DONE,"%1x",(UInt32)0L);
                        isSpeakFreelyMode = utFalse;
                        serverNeedsMoreInfo = utFalse;
                    } else
                    if (isSpeakFreelyMode) {
                        // tell client to speak when he wishes
                        if (speakFreelyMaxEvents >= 0) {
                            serverWritePacketFmt(PKT_SERVER_EOB_SPEAK_FREELY,"%1u",(UInt32)speakFreelyMaxEvents);
                        } else {
                            serverWritePacketFmt(PKT_SERVER_EOB_SPEAK_FREELY,"");
                        }
                        isSpeakFreelyMode = utTrue;
                    }
                } else
                if (serverNeedsMoreInfo) {
                    // we need more info from the client
                    serverWritePacketFmt(PKT_SERVER_EOB_DONE,"%1x",(UInt32)0L);
                    isSpeakFreelyMode = utFalse;
                    serverNeedsMoreInfo = utFalse;
                } else
                if ((UInt16)pkt->hdrType == PKT_CLIENT_EOB_DONE) {
                    // client is finished, close socket
                    serverWritePacketFmt(PKT_SERVER_EOT,"");
                    logINFO(LOGSRC,"End-Of-Transmission\n\n");
                    serverClose();
                } else {
                    // client isn't done yet, tell client to continue
                    serverWritePacketFmt(PKT_SERVER_EOB_DONE,"");
                    isSpeakFreelyMode = utFalse;
                }
                break;
                
            default:
                serverWriteError("%2x%2x", (UInt32)NAK_PACKET_TYPE, (UInt32)pkt->hdrType);
                break;
                
        }
        
    } // for (;;) 
    
    /* we only get here if there is an error */
    
}

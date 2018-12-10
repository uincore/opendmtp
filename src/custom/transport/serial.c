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
//  Serial RS232 transport support.
// Notes:
//  - This implementation requires pthread support.
//  - Serial port connections are typically always duplex, and always remain 
//    'connected' until the media is physically disconnected.  A client may not 
//    be aware that a physical disconnection/reconnection has occured, thus it 
//    is the servers responibility to initiate all conversations when 
//    connecting/reconnecting with a client.
// ---
// Change History:
//  2006/02/19  Martin D. Flynn
//     -Initial release
//  2007/01/28  Martin D. Flynn
//     -Initial support for Window CE (may not be fully complete)
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "custom/log.h"
#include "custom/transport/serial.h"

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/bintools.h"
#include "tools/comport.h"
#include "tools/buffer.h"
#include "tools/threads.h"

#include "base/props.h"
#include "base/propman.h"
#include "base/packet.h"

// ----------------------------------------------------------------------------

/* transport structure */
typedef struct {
    TransportType_t     type;
} SerialTransport_t;

static SerialTransport_t        serialXport;

// ----------------------------------------------------------------------------

// This should be set to the appropriate configured serial port speed
#define SERIAL_SPEED_BPS        BPS_57600

// size of packet buffer
#define MAX_PACKET_BUFFER       (30000L)

// ----------------------------------------------------------------------------

#if defined(TARGET_WINCE)
#  include <winsock2.h>
#endif

// ----------------------------------------------------------------------------

static TransportFtns_t          serXportFtns = { MEDIA_SERIAL, "XportSerial" };

static ComPort_t                serComPort;

static UInt32                   serOpenTime  = 0L;
static UInt32                   serCloseTime = 0L;

static CircleBuffer_t           *serBuffer;

static utBool                   serRunThread = utFalse;
static threadThread_t           serThread;
static threadMutex_t            serMutex;
static threadCond_t             serCond;
static struct timespec          serCondWaitTime;

#define BUFFER_LOCK             MUTEX_LOCK(&serMutex);
#define BUFFER_UNLOCK           MUTEX_UNLOCK(&serMutex);
#define BUFFER_WAIT(T)          CONDITION_TIMED_WAIT(&serCond,&serMutex,utcGetAbsoluteTimespec(&serCondWaitTime,(T)));
#define BUFFER_NOTIFY           CONDITION_NOTIFY(&serCond);

#define OPEN_LOCK               BUFFER_LOCK
#define OPEN_UNLOCK             BUFFER_UNLOCK

// ----------------------------------------------------------------------------

/* test for active Serial/BT connection */
static utBool btIsConnected(ComPort_t *com)
{
    utBool ready = utTrue;
#if defined(COMPORT_SUPPORT_BLUETOOTH)
    ready = comPortIsBluetoothReady(com);
#else
    // ready = comPortGetCTS(com);
#endif
    //logDEBUG(LOGSRC,"Bluetoth ready = %d", (int)ready);
    return ready;
}

// ----------------------------------------------------------------------------

#define OPEN_FAILED_INVERVAL_SEC   MINUTE_SECONDS(60)
static long serLastOpenFailedMessageTime = 0L;

/* open serial port (and check for Serial connection) */
static utBool _serdevOpenCom(ComPort_t *com, utBool log)
{

    /* port config */
    const char *portName = propGetString(PROP_CFG_XPORT_PORT, "");
    if (!portName || !*portName) {
        logERROR(LOGSRC,"Serial transport port name not specified");
        threadSleepMS(MINUTE_SECONDS(3) * 1000L); // long sleep
        return utFalse;
    }
    UInt32 portBPS = propGetUInt32(PROP_CFG_XPORT_BPS, 0L);
    if (portBPS == 0L) { portBPS = SERIAL_SPEED_BPS; }

    /* open port */
    //logDEBUG(LOGSRC,"Openning Serial port '%s' ...", portName);
    //comPortInitStruct(com); <-- port may already be open, don't initialize
    ComPort_t *openCom = (ComPort_t*)0;
    utBool portFound = utFalse;
    OPEN_LOCK {
        serOpenTime = 0L;
        if (comPortIsOpen(com)) {
            // ComPort already openned
            openCom = com;
        } else {
            // Open ComPort
            openCom = comPortOpen(com, portName, portBPS, "8N1", utFalse);
        }
        if (openCom) {
            // Serial port open
            portFound = utTrue;
            if (!btIsConnected(com)) {
                // No Serial connection
                openCom = (ComPort_t*)0;
            } else {
                // We have an active Serial connection
                serOpenTime  = utcGetTimeSec();
                serCloseTime = 0L;
            }
        } else {
            // Still closed
            // leave 'serCloseTime' as-is
        }
    } OPEN_UNLOCK
    
    /* open failed */
    if (!openCom) {
        // The outer loop will retry the open later
        // We expect this to occur when the Serial port device is no longer available
        // (ie. when the Serial device has disconnected)
        if (utcIsTimerExpired(serLastOpenFailedMessageTime,OPEN_FAILED_INVERVAL_SEC)) {
            serLastOpenFailedMessageTime = utcGetTimer();
            if (portFound) {
                logINFO(LOGSRC,"Waiting for Serial transport connection: %s", portName);
            } else {
                logERROR(LOGSRC,"Serial transport open failed: %s", portName);
            }
        }
        return utFalse;
    }

    /* additional port attributes */
    //comPortAddOptions(com, COMOPT_NOESCAPE);
    //if (PORT_IsCONSOLE(com->port)) {
    //    logINFO(LOGSRC,"Console port, backspace/echo enabled ...");
    //    comPortAddOptions(com, COMOPT_BACKSPACE);
    //    comPortAddOptions(com, COMOPT_ECHO);
    //}

    /* return success */
    logINFO(LOGSRC,"Serial port '%s' openned", portName);
    return utTrue;
    
}

/* close serial port */
static void _serdevCloseCom(utBool closePort)
{
    ComPort_t *com = &serComPort;
    OPEN_LOCK {
        comPortClose(com);
        serOpenTime  = 0L;
        serCloseTime = utcGetTimeSec();
    } OPEN_UNLOCK
}

/* return the time the connection was opened, or 0L if the connection is closed */
static UInt32 serdevGetOpenTime()
{
    UInt32 openTime = 0L;
    OPEN_LOCK {
        openTime = serOpenTime;
    } OPEN_UNLOCK
    return openTime;
}

// ----------------------------------------------------------------------------

/* initialize serial communications */
static void serdevInitialize()
{
        
    /* init comport */
    comPortInitStruct(&serComPort);
    
    /* create mutex */
    threadMutexInit(&serMutex);
    threadConditionInit(&serCond);

    /* create buffer */
    serBuffer = bufferCreate(MAX_PACKET_BUFFER);

}

// ----------------------------------------------------------------------------

//static double maxBufferLoad = 0.0;

/* continue reading until comport error, end of thread, or Serial disconnects */
static int _serdevReadData()
{
    ComPort_t *com = &serComPort;

    /* flush everything currently in the comport buffers */
    comPortFlush(com, 0L);

    /* read Serial device packet */
    // continues until port read error, or Serial disconnects
    char buf[PACKET_MAX_ENCODED_LENGTH];
    while (serRunThread) {
        
        /* test serial connection */
        if (!btIsConnected(com)) {
            // no active connection
            return 0;
        }

        /* read data */
        int cmdLen = comPortReadLine(com, buf, sizeof(buf), -1L);
        if (cmdLen < 0) {
            // com port closed?
            return -1;
        } else
        if ((cmdLen == 0) && COMERR_IsTimeout(com)) {
            // timeout, try again
            //logDEBUG(LOGSRC,"Serial device timeout");
            continue;
        }

        /* check for EOL */
        if (buf[cmdLen - 1] == 13) {
            logERROR(LOGSRC,"***** EOL wasn't stripped");
        }

        /* place char in buffer */
        BUFFER_LOCK {
            if (!bufferPutString(serBuffer, buf)) {
                // This means that we will be losing packets
                // The buffer is large enough that we should never get backed up.
                // This would only occur if the main thread somehow stopped pulling
                // data off this queue.
                logCRITICAL(LOGSRC,"Command buffer overflow!");
            }
            //double load = (double)bufferGetLength(serBuffer) * 100.0 / (double)bufferGetSize(serBuffer);
            //if (load > maxBufferLoad) { maxBufferLoad = load; }
            //logINFO(LOGSRC,"Buffer load: %.1f [max %.1f]", load, maxBufferLoad);
            BUFFER_NOTIFY;
        } BUFFER_UNLOCK
                
    }
    
    return -1; // end of thread 
    
}

/* thread main */
static void _serdevThreadRunnable(void *arg)
{
    ComPort_t *com = &serComPort;
    serCloseTime = 0L;
    while (serRunThread) {
        
        /* open port to Serial device */
        if (!_serdevOpenCom(com, utFalse)) {
            threadSleepMS(3000L); // short sleep
            continue;
        }
        comPortSetDTR(com, utTrue);
        comPortSetRTS(com, utTrue);

        /* read data loop */
        int err = _serdevReadData();
        // Return codes:
        //   0 : Serial disconnect
        //  -1 : a comport error occurred, or end-of-thread
        
        /* ComPort failed, or Serial disconnect */
        logINFO(LOGSRC,"Closing Serial comport\n");
        _serdevCloseCom(err? utTrue : utFalse);
        
    }
    // once this thread stops, it isn't starting again
    logERROR(LOGSRC,"Stopping thread");
    threadExit();
}

/* call-back to stop thread */
static void _serdevStopThread(void *arg)
{
    serRunThread = utFalse;
    _serdevCloseCom(utTrue);
}

/* start serial communication thread */
static utBool serdevStartThread()
{
        
    /* init */
    comPortInitStruct(&serComPort);

    /* create thread */
    serRunThread = utTrue;
    if (threadCreate(&serThread,&_serdevThreadRunnable,0,"Serial") == 0) {
        // thread started successfully
        threadAddThreadStopFtn(&_serdevStopThread,0);
        return utTrue;
    } else {
        serRunThread = utFalse;
        return utFalse;
    }
    
}

// ----------------------------------------------------------------------------

/* read a line of data fom the serial port */
static int serdevReadLine(char *data, int dataLen, UInt32 timeoutMS)
{
    int len;
    UInt32 nowTimeSec = utcGetTimeSec();
    UInt32 timeoutSec = nowTimeSec + ((timeoutMS + 500L) / 1000L);
    for (;;) {

        /* get string */
        BUFFER_LOCK {
            len = bufferGetString(serBuffer, data, dataLen);
        } BUFFER_UNLOCK

        /* error? (unlikely) */
        if (len < 0) {
            return len;
        }

        /* no data? (very likely) */
        if (len == 0) {
            nowTimeSec = utcGetTimeSec();
            if (nowTimeSec < timeoutSec) {
                // wait for data
                //threadSleepMS(100L);
                BUFFER_LOCK {
                    BUFFER_WAIT(1000L);
                } BUFFER_UNLOCK
                continue;
            } else {
                // timeout - exit now
                return 0;
            }
        }

        /* append EOL and return */
        //if ((len < dataLen) && (data[len - 1] != PACKET_ASCII_ENCODING_EOL)) {
        //    data[len++] = PACKET_ASCII_ENCODING_EOL;
        //}
        return len;
        
    }
    
    /* timeout */
    return 0;
     
}

// ----------------------------------------------------------------------------

/* write data to the serial port */
static int serdevWrite(const UInt8 *data, int dataLen)
{
    ComPort_t *com = &serComPort;
    
    /* last openned? */
    UInt32 lastOpenned = serdevGetOpenTime();
    if (lastOpenned <= 0L) {
        // outbound port is not open
        logINFO(LOGSRC,"Serial port not open");
        return -1; 
    }
    
    /* write to comport */
    return comPortWrite(com, data, dataLen);

}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

/* flush input buffer */
static void serial_transportReadFlush()
{
    BUFFER_LOCK {
        bufferClear(serBuffer);
    } BUFFER_UNLOCK
}

/* return true if transport is open */
static utBool serial_transportIsOpen()
{
    // this may be called by threads other than the protocol thread.
    UInt32 openTime = serdevGetOpenTime();
    return (openTime > 0L)? utTrue : utFalse;
}

/* cloe transport */
static utBool serial_transportClose(utBool sendUDP)
{
    // This transport is never explicitly closed, but is automatically
    // closed when the Serial port device 'disappears' (ie Serial disconnects) 
    return utTrue; // however, always return successful
}

/* open transport */
static utBool serial_transportOpen(TransportType_t type)
{
    if (type == TRANSPORT_DUPLEX) {
        serialXport.type = TRANSPORT_DUPLEX;
        // the client is just now starting a communication session
        // clear all old server data from the buffer
        serial_transportReadFlush();
        return serial_transportIsOpen();
    } else {
        logERROR(LOGSRC,"Transport type not supported: %d", type);
        return utFalse;
    }
}

/* read packet from transport */
static int serial_transportReadPacket(UInt8 *buf, int bufLen)
{
    //logINFO(LOGSRC,"transport read packet ...");
    
    /* transport not open? */
    if (!serial_transportIsOpen()) {
        logERROR(LOGSRC,"Transport is not open");
        return -1;
    }
    
    /* no data was requested */
    if (!buf || (bufLen == 0)) {
        return 0;
    }
    *buf = 0;
    
    /* read line (packet) */
    int readLen = serdevReadLine((char*)buf, bufLen, 2000L);
    //logINFO(LOGSRC,"ReadLine: [%d] %s", readLen, buf);
    
    /* return length */
    return readLen;
    
}

// ----------------------------------------------------------------------------

/* write packet to transport */
static int serial_transportWritePacket(const UInt8 *buf, int bufLen)
{

    /* transport not open? */
    if (!serial_transportIsOpen()) {
        logERROR(LOGSRC,"Transport is not open");
        return -1;
    }

    /* nothing specified to write */
    if (bufLen == 0) {
        return 0;
    }

    /* write data per transport type */
    int len = serdevWrite(buf, bufLen);
    return len;

}

// ----------------------------------------------------------------------------

/* initialize transport */
static utBool _transportDidInit = utFalse;
const TransportFtns_t *serial_transportInitialize()
{

    /* already initialized? */
    if (_transportDidInit) {
        return &serXportFtns;
    }
    _transportDidInit = utTrue;

    /* init transport structure */
    memset(&serialXport, 0, sizeof(SerialTransport_t));
    serialXport.type = TRANSPORT_DUPLEX;
    
    /* init Serial communication */
    serdevInitialize();
    serdevStartThread();

    /* init function structure */
    serXportFtns.xportIsOpen        = &serial_transportIsOpen;
    serXportFtns.xportOpen          = &serial_transportOpen;
    serXportFtns.xportClose         = &serial_transportClose;
    serXportFtns.xportReadFlush     = &serial_transportReadFlush;
    serXportFtns.xportReadPacket    = &serial_transportReadPacket;
    serXportFtns.xportWritePacket   = &serial_transportWritePacket;
    return &serXportFtns;

}

// ----------------------------------------------------------------------------

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
//  WindowsCE specific OS tools
// Notes:
//  - This section is for the WindowsCE/Mobile implementation.
// ---
// Change History:
//  2007/01/28  Martin D. Flynn
//      Initial release
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"
#if defined(TARGET_WINCE)

// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <pm.h>
#include <Winreg.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/io.h"

#include "base/props.h"

#include "custom/log.h"
#include "custom/startup.h"

#include "custom/wince/wceos.h"

// ----------------------------------------------------------------------------

/* WSStartup init */
void osInitWSAStartup()
{
    /* init WSA windows extensions */
    WSADATA wsaData;
    WORD wsaVersion = MAKEWORD(2,2);
    int wsaErr = WSAStartup(wsaVersion, &wsaData);
    if (wsaErr != 0) {
        logERROR(LOGSRC,"WSAStartup failed: %d", wsaErr);
    }
    logDEBUG(LOGSRC,"WSAStartup info: %u.%u", LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
}

// ----------------------------------------------------------------------------

/* reboot device */
utBool osReboot()
{
#if defined(ENABLE_REBOOT)
    // implement reboot here
#endif

    logWARNING(LOGSRC,"'osReboot' is not supported on this platform");
    return utFalse;

}

// ----------------------------------------------------------------------------

#define DEFAULT_SERIAL_NUMBER       "X000000000000" // length = 13

/* unique device serial number */
static char wceosSerialID[MAX_ID_SIZE + 1] = { 0 };
static UInt8 wceosSerialType = 'X';
static UInt8 wceosSerial[8];
static int wceosSerialLen = 0;

/* get device unique id */
const char *osGetSerialNumberID()
{
    
    /* init serial # */
    if (!*wceosSerialID) {
        UInt8 serial[8];
        int serialLen = osGetSerialNumber(serial, sizeof(serial));
        if (serialLen > 0) {
            // use the last 6 bytes of the serial #
            wceosSerialID[0] = (char)wceosSerialType;
            int sLen = (serialLen < 6)? serialLen : 6;
            UInt8 *s = serial + serialLen - sLen;
            strEncodeHex(&wceosSerialID[1], sizeof(wceosSerialID)-1, s, sLen); 
            // ie "X9000CE437C47"
        }
    }

    /* default serial number */
    if (!*wceosSerialID) {
        strcpy(wceosSerialID, DEFAULT_SERIAL_NUMBER);
    }
    
    return wceosSerialID;
}

/* get device unique id */
const int osGetSerialNumber(UInt8 *serial, int maxLen)
{
    
    /* no place to put serial#? */
    if (!serial || (maxLen <= 0)) {
        return 0;
    }
    
    // implement unique serial number retrieval here
    
    /* copy serial number */
    int len = (wceosSerialLen < maxLen)? wceosSerialLen : maxLen;
    if (len > 0) {
        memcpy(serial, wceosSerial, len);
        return len;
    } else {
        return 0;
    }
    
}

// ----------------------------------------------------------------------------

/* return current host/device name */
const char *osGetHostname(char *host, int hostLen)
{
    if (host && (hostLen > 0)) {
        char devName[128];
        int err = gethostname(devName, sizeof(devName));
        if (err) {
            int wsaErr = WSAGetLastError();
            // Possible errors:
            //   WSAEFAULT          - Invalid buffer, buffer too small
            //   WSANOTINITIALISED  - 'WSAStartup' not yet called
            //   WSAENETDOWN        - Network system failure
            //   WSAEINPROGRESS     - Winsock is busy
            logERROR(LOGSRC,"Unable to get hostname [err=%d]", wsaErr);
            *host = 0;
            return (char*)0;
        }
        strCopy(host, hostLen, devName, -1);
        return host;
    }
    return (char*)0;
}

/* set current host name */
utBool osSetHostname(const char *host)
{
    
    /* get new device name */
    char devName[128];
    if (!host || !*host || !isalnum(*host)) {
        strCopy(devName, sizeof(devName), osGetSerialNumberID(), -1);
    } else {
        strCopy(devName, sizeof(devName), host, -1);
    }
    
    /* filter hostname */
    // TODO: Filter proper hostname

    /* get current hostname */
    char currentHostName[64] = { 0 };
    osGetHostname(currentHostName, sizeof(currentHostName));

    /* set device name */
    if (!strEquals(devName,currentHostName)) {
        logDEBUG(LOGSRC,"Setting hostname: %s", devName);
        int err = sethostname(devName, strlen(devName) + 1); // Windows requires '+1'
        if (err) {
            // Possible errors:
            //   WSAEINVAL[10022] - Invalid arguments (host len must include terminating null)
            //   
            int wsaErr = WSAGetLastError();
            logERROR(LOGSRC,"Unable to set hostname: %s [err=%d]", host, wsaErr);
            return utFalse;
        }
    }
    
    /* set published Bluetooth name */
    // TODO: set published bluetooth name
    
    /* success */
    return utTrue;
}

// ----------------------------------------------------------------------------
#endif

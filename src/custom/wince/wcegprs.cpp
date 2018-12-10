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
//  WindowsCE specific controls for GPRS connectivity
// Notes:
//  - This section is for the WindowsCE/Mobile implementation.
//  - I'm still experimenting with the method used to invoke the GPRS connection
//  facility of the WindowsCE platform.  If you see something that should be
//  changed here, please let me know.
//  - Note: even though the following appears to be gprs-centric, there is 
//  nothing here that mandates that the connection necessarily be GPRS.  [It
//  could be any other 'connection' name defined by PROP_COMM_CONNECTION].
//  - Modem may need initialization string:
//    IE. AT+CGDCONT=1,"IP","isp.cingular"
//        AT&W
// ---
// Change History:
//  2007/01/28  Martin D. Flynn
//     -Initial release
//  2007/03/11  Martin D. Flynn
//     -Increased MODEM_SHORT_RESET_TIMEOUT to 6 hours 
//     -Increased MODEM_LONG_RESET_TIMEOUT to 36 hours 
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"
#if defined(TARGET_WINCE)

// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <ras.h>
#include <raserror.h>
#include <pm.h>

#include "custom/log.h"
#include "custom/startup.h"

#include "custom/wince/wceos.h"
#include "custom/wince/wcegprs.h"

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/bintools.h"
#include "tools/utctools.h"
#include "tools/threads.h"  // for 'threadSleepMS'
#include "tools/comport.h"
#include "tools/sockets.h"

#include "base/props.h"
#include "base/propman.h"
#include "base/packet.h"

// ----------------------------------------------------------------------------

#if !defined(MODEM_SHORT_RESET_TIMEOUT)
#  define MODEM_SHORT_RESET_TIMEOUT     HOUR_SECONDS(6)     // ERROR_PORT_NOT_AVAILABLE errors
#endif
#if !defined(MODEM_LONG_RESET_TIMEOUT)
#  define MODEM_LONG_RESET_TIMEOUT      HOUR_SECONDS(36)    // any non-connectivity reason
#endif
#if !defined(MODEM_CONTINUE_TIMEOUT)
#  define MODEM_CONTINUE_TIMEOUT        MINUTE_SECONDS(15)
#endif

#define DEFAULT_ENTRY_NAME              "GPRS"

// ----------------------------------------------------------------------------

static TimerSec_t           _wceResetTimerLong      = 0L;
static TimerSec_t           _wceResetTimerShort     = 0L;

// ----------------------------------------------------------------------------

/* WinCE initialization */
static utBool _didInit = utFalse;
void wceGprsInitialize()
{
    
    /* already initialized */
    if (_didInit) {
        return;
    }
    _didInit = utTrue;

    /* clear errors */
    _wceResetTimerLong  = 0L;
    _wceResetTimerShort = 0L;

}

// ----------------------------------------------------------------------------

/* hard reset the modem */
utBool wceGprsResetModem()
{
    logWARNING(LOGSRC,"TODO: Reset GPRS modem ...");
    // TODO: Change this to only reset the modem/phone
    return utFalse; // false, until this is properly implemented
}

// ----------------------------------------------------------------------------

/* get connection matching PROP_COMM_CONNECTION */
HRASCONN wceGprsGetConnection(const char *entryName)
{
    
    /* default entry name */
    if (!entryName || !*entryName) {
        entryName = propGetString(PROP_COMM_CONNECTION, DEFAULT_ENTRY_NAME);
    }
    
    /* wide-char entryName */
    TCHAR wEntryName[RAS_MaxEntryName + 1];
    strWideCopy(wEntryName, RAS_MaxEntryName, entryName, -1);
    
    /* enumerate connections */
    RASCONN rasConn[10]; // should be more than enough
    memset(rasConn, 0, sizeof(rasConn));
    rasConn[0].dwSize = sizeof(rasConn[0]);
    DWORD rasSize = sizeof(rasConn), rasCnt = 0L;
    DWORD err = RasEnumConnections(rasConn, &rasSize, &rasCnt);
    if (!err) {
        DWORD i;
        for (i = 0L; i < rasCnt; i++) {
            if (_wcsicmp(wEntryName, rasConn[i].szEntryName) == 0) {
                // Just because we've found a connection, doesn't means we're actually still
                // connected.  The phone can be 'off' (on PDA phones where the 'phone' can be
                // powered separately) and we can still find an existing connection.
                return rasConn[i].hrasconn;
            }
        }
    }
    
    /* not found */
    //logINFO(LOGSRC,"No existing connection: %ls", wEntryName);
    return NULL;
    
}

/* list existing entry names */
static void wceGprsListEntryNames()
{
    RASENTRYNAME rasEntryName[10]; // <-- should be more than enough
    memset(&rasEntryName, 0, sizeof(rasEntryName));
    rasEntryName[0].dwSize = sizeof(rasEntryName[0]);
    DWORD rasEntryNameSize = sizeof(rasEntryName);
    DWORD rasEntryCount = 0;
    WSASetLastError(0);
    DWORD entErr = RasEnumEntries(NULL, NULL, rasEntryName, &rasEntryNameSize, &rasEntryCount);
    if (entErr == ERROR_SUCCESS) {
        int i;
        logINFO(LOGSRC,"Entry names:");
        for (i = 0; i < (int)rasEntryCount; i++) {
            logINFO(LOGSRC,"  %d) %ls", (i+1), rasEntryName[i].szEntryName);
        }
    } else {
        logINFO(LOGSRC,"Unable to enumerate entry names [%d]", WSAGetLastError());
    }
}

// ----------------------------------------------------------------------------

/* check to see if we have a connection that matches PROP_COMM_CONNECTION */
utBool wceGprsIsConnected(HRASCONN rasConn)
{

    /* check for open connection */
    if (rasConn != NULL) {
        RASCONNSTATUS rasConnStat;
        memset(&rasConnStat, 0, sizeof(rasConnStat));
        rasConnStat.dwSize = sizeof(rasConnStat);
        DWORD err = RasGetConnectStatus(rasConn, &rasConnStat);
        if (err) {
            // Errors encountered (see 'raserror.h'/'winerror.h' for a complete list)
            //   6  ERROR_INVALID_HANDLE
            if (err != ERROR_INVALID_HANDLE) {
                logWARNING(LOGSRC,"Error retrieving connect status [%ld]", (Int32)err);
            }
            return utFalse;
        } else
        if (rasConnStat.rasconnstate == RASCS_Connected) {
            return utTrue;
        } else {
            return utFalse;
        }
    }
    
    /* no existing connection */
    return utFalse;

}

// ----------------------------------------------------------------------------

/* disconnect the connection that we created */
static int _wceIsDisconnecting = 0;
utBool wceGprsDisconnect(HRASCONN rasConn)
{
    utBool rtn = utTrue;
    if (rasConn != NULL) {
        if (_wceIsDisconnecting > 0) {
            // already disconnecting?
            logWARNING(LOGSRC,"******* Disconnect already in-process !!!");
            rtn = utFalse;
        } else {
            // We've seen this section lock up!
            _wceIsDisconnecting++;
            DWORD err = RasHangUp(rasConn);
            threadSleepMS(4000L); // make sure that the port has time to close
            if (err != SUCCESS) {
                logWARNING(LOGSRC,"******* Disconnect Failed !!!");
                rtn = utFalse;
            } else {
                // wait here until we get disconnect confirmation, or timeout
                RASCONNSTATUS rsc;
                memset(&rsc, 0, sizeof(rsc));
                rsc.dwSize = sizeof(rsc);
                long timeout  = 6000L;  // maximum wait time
                long interval = 2000L;  // sleep interval
                for (;timeout > 0L; timeout -= interval) {
                    if (RasGetConnectStatus(rasConn,&rsc) == ERROR_INVALID_HANDLE) {
                        logINFO(LOGSRC,"Disconnected");
                        break;
                    }
                    threadSleepMS(interval); 
                }
                if (timeout <= 0L) {
                    logWARNING(LOGSRC,"******* Disconnect Timeout !!!");
                    rtn = utFalse;
                } else {
                    rtn = utTrue;
                }
            }
            _wceIsDisconnecting--;
        }
    }
    return rtn;
}

// ----------------------------------------------------------------------------

/* make sure a connection has been established */
static utBool _wceGprsConnect(HRASCONN *pRasConn)
{
    const char *entryName = propGetString(PROP_COMM_CONNECTION, DEFAULT_ENTRY_NAME);
    if (pRasConn) { *pRasConn = NULL; }

    /* already connected? */
    HRASCONN hExistingRasConn = wceGprsGetConnection(entryName);
    if (wceGprsIsConnected(hExistingRasConn)) {
        logDEBUG(LOGSRC,"GPRS already connected: %s", entryName);
        _wceResetTimerLong  = 0L;
        _wceResetTimerShort = 0L;
        if (pRasConn) { *pRasConn = hExistingRasConn; }
        return utTrue;
    }
    //logDEBUG(LOGSRC,"GPRS attempt connection: %s", entryName);

    /* signal strength */
    // check signal strength here
    /* RASDIALPARAMS structure */
    RASDIALPARAMS rasDialParms;
    memset(&rasDialParms, 0, sizeof(rasDialParms));
    rasDialParms.dwSize = sizeof(rasDialParms);
    strWideCopy(rasDialParms.szEntryName, RAS_MaxEntryName, entryName, -1);

    /* init RAS configuration from system parameters */
    BOOL hasRasParams = FALSE;
    BOOL inclPass = FALSE;
	DWORD rasEntryErr = RasGetEntryDialParams(NULL, &rasDialParms, &inclPass);
    if (rasEntryErr) {
        // Possible errors:
        //  1168  ERROR_NOT_FOUND   Element not found
        logERROR(LOGSRC,"No dial params [%ld]: %ls", rasEntryErr, rasDialParms.szEntryName);
        //return utFalse;
    } else {
        hasRasParams = TRUE;
    }
    //logINFO(LOGSRC,"S) inclPass : %d\n" , inclPass);

    /* override parameters */
    // szPhoneNumber
    const char *phoneNum = propGetString(PROP_COMM_APN_PHONE, "");
    if (phoneNum && *phoneNum) {
        strWideCopy(rasDialParms.szPhoneNumber, RAS_MaxPhoneNumber, phoneNum, -1);
        hasRasParams = TRUE;
    }
    // szCallbackNumber
    const char *callback = "";
    if (callback && *callback) {
        strWideCopy(rasDialParms.szCallbackNumber, RAS_MaxCallbackNumber, callback, -1);
        hasRasParams = TRUE;
    }
    // szUserName
    const char *apnUser = propGetString(PROP_COMM_APN_USER,"");
    if (apnUser && *apnUser) {
        strWideCopy(rasDialParms.szUserName, UNLEN, apnUser, -1);
        hasRasParams = TRUE;
    }
    // szPassword
    const char *apnPass = propGetString(PROP_COMM_APN_PASSWORD,"");
    if (apnPass && *apnPass) {
        strWideCopy(rasDialParms.szPassword, PWLEN, apnPass, -1);
        hasRasParams = TRUE;
    }
    // szDomain
    const char *apnDomain = propGetString(PROP_COMM_APN_SERVER,"");
    if (apnDomain && *apnDomain) {
        strWideCopy(rasDialParms.szDomain, DNLEN, apnDomain, -1);
        hasRasParams = TRUE;
    }
    
    /* do we have any dialing parameters? */
    if (!hasRasParams) {
        logERROR(LOGSRC,"No RAS dialing parameters!");
        return utFalse;
    }

    /* Dial/Connect */
#if defined(WCEGPRS_CONNECT_VIA_RASDIAL_EXE)
    // External RASDIAL connection here (if implemented)
#else

    // "RasDial(...)" appears to have its share of problems:
    // - On several occasions, 'RasDial' has caused the program to terminate.
    // - On at least one occasion, a call to 'RasDial' never returned.  I suspect
    //   that it had terminated the current thread.
    // - 'RasDial' may get in an endless 'ERROR_PORT_DISCONNECTED' error loop even
    //   though a valid SIM card is in place and GPRS coverage is fine.  (I've been
    //   able to recover in this situation by hard-resetting the phone).
    /* call 'RasDial(...)' */
    // In marginal GPRS coverage, this call can hang for more than 60 seconds 
    // before returning a failed connection attempt.
    UInt32 rasDialStartTime = utcGetTimeSec();
    logDEBUG(LOGSRC,"[%lu] 'RasDial' start ...", rasDialStartTime);
    HRASCONN hRasConn = NULL;
    DWORD rasDialErr = RasDial(NULL,NULL,&rasDialParms,NULL,NULL,&hRasConn);
    UInt32 rasDialEndTime = utcGetTimeSec();
    logDEBUG(LOGSRC,"[%lu] 'RasDial' end ... [%ld sec]", rasDialEndTime, (rasDialEndTime - rasDialStartTime));
    if (pRasConn) { *pRasConn = hRasConn; }
    
    /* check returned errors */
	if (rasDialErr != SUCCESS) {
        // Errors encountered (see 'raserror.h' for a complete list)
        // 604  ERROR_WRONG_INFO_SPECIFIED  [may be sent when GPRS is unavailable]
        // 608  ERROR_DEVICE_DOES_NOT_EXIST [likely an invalid entry name]
        // 619  ERROR_PORT_DISCONNECTED     [may already be connected]
        // 631  ERROR_USER_DISCONNECTION
        // 633  ERROR_PORT_NOT_AVAILABLE
        // 660  ERROR_NO_RESPONSES
        // 679  ERROR_NO_CARRIER
        // ---------------------------------
        
        /* hang-up */
        if (hRasConn) {
            wceGprsDisconnect(hRasConn);
        } else {
            // wait here even if 'RasDial' failed and 'hRasConn' is null
            threadSleepMS(3500L);
        }
        
        /* check specific error */
        if (rasDialErr == ERROR_WRONG_INFO_SPECIFIED) {
            // - I've seen this error occur when either GPRS is unavailable, or the
            // phone is turned off.  For now, just continue with the retry ...
        } else
        if (rasDialErr == ERROR_PORT_DISCONNECTED) {
            // - This most likely indicates that GPRS coverage is not available.
            // - When this error occurs multiple times and GPRS *IS* available, I've been 
            //   able to recover by manually turning off the phone connection, then turning 
            //   it back on (while testing on an HP hw6945).  In an attempt to perform this 
            //   reset automatically, we'll try to detect this state and attempt to hang-up 
            //   the connection, hopefully clearing this error state on the next iteration.
            //   This will not be effective if GPRS coverage is not available.
            // - This error may also occur if the SIM card is not installed, in which
            //   case this error will continue forever.
            logERROR(LOGSRC,"RasDial ERROR_PORT_DISCONNECTED [%ld]", (Int32)rasDialErr);
        } else
        if (rasDialErr == ERROR_PORT_NOT_AVAILABLE) {
            // - This likely indicates that the phone/modem is locked up and unusable.
            // - Or we may actually be connected, but under a different entry-name.
            // - This error can also occur if the SIM card is not installed, in which
            //   case this error will continue forever.
            logERROR(LOGSRC,"RasDial ERROR_PORT_NOT_AVAILABLE [%ld]", (Int32)rasDialErr);
        } else
        if (rasDialErr == ERROR_DEVICE_DOES_NOT_EXIST) {
            // - This likely indicates an invalid entry name
            logERROR(LOGSRC,"RasDial ERROR_DEVICE_DOES_NOT_EXIST [%ld] - %s", (Int32)rasDialErr, entryName);
            wceGprsListEntryNames(); // displa entry names 
        } else {
            logINFO(LOGSRC,"RasDial connection failed [raserr=%ld]", (Int32)rasDialErr);
        }
        
        /* short timeout */
        // Short timeouts specifically checks for ERROR_PORT_NOT_AVAILABLE errors.
        // 'ERROR_PORT_NOT_AVAILABLE' errors may not be recoverable.
        if (rasDialErr == ERROR_PORT_NOT_AVAILABLE) {
            // we may not be able to recover without a rebbot, set a short timer
            if (_wceResetTimerShort == 0L) {
                // default reset timer, short delay
                _wceResetTimerShort = utcGetTimer();
            }
        } else {
            // we may be able to recover from non-port-not-available errors
            _wceResetTimerShort = 0L;
        }

        /* long timeout */
        // Long timeouts check for no connectivity, for any reason, for a long period of time.
        if (_wceResetTimerLong == 0L) {
            // default reset timer, long delay
            _wceResetTimerLong = utcGetTimer();
        }

        /* check timeouts */
        if ((MODEM_LONG_RESET_TIMEOUT > 0L) && (_wceResetTimerLong > 0L) && 
            utcIsTimerExpired(_wceResetTimerLong,MODEM_LONG_RESET_TIMEOUT)) {
            // we've been trying unsuccessfully for awhile now, just reset the modem
            _wceResetTimerLong  = utcGetTimer();
            _wceResetTimerShort = 0L;
            wceGprsResetModem();
            // (if we've rebooted, control should not reach here)
        } else
        if ((MODEM_SHORT_RESET_TIMEOUT > 0L) && (_wceResetTimerShort > 0L) && 
            utcIsTimerExpired(_wceResetTimerShort,MODEM_SHORT_RESET_TIMEOUT)) {
            // we've been trying unsuccessfully for awhile now, just reset the modem
            _wceResetTimerLong  = utcGetTimer();
            _wceResetTimerShort = 0L;
            wceGprsResetModem();
            // (if we've rebooted, control should not reach here)
        }

        /* 'continue' attempt */
        // after a few minutes, start indicating that we've made a connection
        // (the protocol transport will fail if it cannot actually transmit)
        // we do this just-in-case this error is incorrect, which may in fact 
        // be the case if we are connected using a different "Connection name".
        if (utcIsTimerExpired(_wceResetTimerLong,MODEM_CONTINUE_TIMEOUT)) {
            logERROR(LOGSRC,"Continuing as if connected ...");
            return utTrue;
        }

        /* connection failed */
        return utFalse;

    }
    
#endif // else defined(WCEGPRS_CONNECT_VIA_RASDIAL_EXE)

    /* connected! */
    logINFO(LOGSRC,"------- GPRS Connection Success ------");
    _wceResetTimerLong  = 0L;
    _wceResetTimerShort = 0L;
    threadSleepMS(2000L);
    return utTrue;

}

/* make sure a connection has been established */
utBool wceGprsConnect(HRASCONN *pRasConn, utBool disconnectFirst)
{
    utBool rtn = utFalse;

    /* force a disconnect first? */
    if (disconnectFirst && pRasConn) {
        // This usually indicates that the caller was unable to establish a socket 
        // connection during the last GPRS connection.  Disconnect the previous 
        // failing connection before establishing a new connection.  In this 
        // situation, it is possible that if enough time has passed since the last
        // socket-connection attempt, the OS may have already closed the old failing
        // GPRS-connection and established a new one.  In which case we may be closing 
        // a good GPRS-connection.  There's no easy way to determine if the OS has
        // in fact already established a connection, so going ahead and closing the
        // current GPRS-connection, and establishing a new connection is the safest
        // thing to do at this point.
        logINFO(LOGSRC,"Forcing a disconnect before establishing a new connection");
        wceGprsDisconnect(*pRasConn);
    }

    /* connect (may use an existing connection, if available) */
    // This can take up to 60 seconds in a marginal GPRS coverage area.
    rtn = _wceGprsConnect(pRasConn);
    
    return rtn;
}

// ----------------------------------------------------------------------------
#endif // defined(TARGET_WINCE)

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
//  Example UDP/TCP socket utilities for data transport use.
// ---
// Change History:
//  2006/01/04  Martin D. Flynn
//     -Initial release
//  2006/01/12  Martin D. Flynn
//     -Fixed read timeout problem on Linux.
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
//     -Enabled non-blocking mode on tcp sockets (TARGET_WINCE only)
//     -Added 'send' select check (see 'socketIsSendReady')
//     -Fixed premature timeout problem in socketReadTCP
//  2007/02/05  Martin D. Flynn
//     -Fixed size of 'heBuf' in call to 'gethostbyname_r'.  Was 256, which was
//      too small for uClibc. (Thanks to Tomasz Rostanski for catching this!).
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#define SKIP_TRANSPORT_MEDIA_CHECK // only if TRANSPORT_MEDIA not used in this file 
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <limits.h>

#if defined(TARGET_WINCE)
#  include <winsock2.h>
typedef int socklen_t;
#else
#  include <unistd.h>
#  include <errno.h>
#  include <termios.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/types.h>
#  include <sys/ioctl.h>
#  include <sys/stat.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#endif

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/sockets.h"

#include "custom/log.h"

// ----------------------------------------------------------------------------

#define ALWAYS_RESOLVE_HOST     utFalse

// ----------------------------------------------------------------------------

// Overhead:
//  TCP => 20(IP) + 20{TCP) = 40
//  UDP => 20(IP) +  8(UDP) = 28
// Minimum IP datagram size = 576 bytes (include the above overhead)
// Maximum IP datagram size = 65516
//
// Protocol overhead: http://sd.wareonearth.com/~phil/net/overhead/

// ----------------------------------------------------------------------------

#if   defined(TARGET_CYGWIN)
// This works on Cygwin
#  define IOCTIL_REQUEST_BYTES_AVAIL    TIOCINQ     // (int*) Cygwin
#  define CLOSE_SOCKET(F)               close(F)
#  define IOCTL_SOCKET(F,R,A)           ioctl(F,R,A)
#  define IOCTL_ARG_TYPE                int
#  define INVALID_SOCKET                (-1)
#  define SOCKET_ERROR                  (-1)

#elif defined(TARGET_WINCE)
// Windows CE
#  define IOCTIL_REQUEST_BYTES_AVAIL    FIONREAD    // (u_long*) Windows CE
#  define IOCTIL_REQUEST_NON_BLOCKING   FIONBIO     // (u_long*) Windows CE
#  define CLOSE_SOCKET(F)               closesocket(F)
#  define IOCTL_SOCKET(F,R,A)           ioctlsocket(F,R,A)  // [A=(ulong*)]
#  define IOCTL_ARG_TYPE                u_long
//#  define INVALID_SOCKET              (-1)        <- already defined
//#  define SOCKET_ERROR                (-1)        <- already defined

#else
// This works fine on Linux and GumStix
#  include <asm/ioctls.h>
#  define IOCTIL_REQUEST_BYTES_AVAIL    FIONREAD    // (int*)
//#  define IOCTIL_REQUEST_NON_BLOCKING FIONBIO     // (int*)
#  define CLOSE_SOCKET(F)               close(F)
#  define IOCTL_SOCKET(F,R,A)           ioctl(F,R,A)
#  define IOCTL_ARG_TYPE                int
#  define INVALID_SOCKET                (-1)
#  define SOCKET_ERROR                  (-1)
#endif

// ----------------------------------------------------------------------------

// Windows CE 'errno' redefinitions
#if defined(TARGET_WINCE)
#  define RESET_ERRNO                   WSASetLastError(0) // docs claim this is required
#  define ERRNO                         WSAGetLastError() // 'errno' undefined on WinCE
#  define ECONNRESET                    WSAECONNRESET
#  define EWOULDBLOCK                   WSAEWOULDBLOCK
#  define EAGAIN                        WSAEWOULDBLOCK
#else
#  define ERRNO                         errno
#  define RESET_ERRNO                   /*NO-OP*/ // errno = 0
#endif

// ----------------------------------------------------------------------------

/* client/server: clear socket structure */
void socketInitStruct(Socket_t *sock, const char *host, int port, int type)
{
    if (sock) {
        memset(sock, 0, sizeof(Socket_t));
#if defined(ENABLE_SERVER_SOCKET)
        sock->serverfd = INVALID_SOCKET;
#endif
        sock->sockfd = INVALID_SOCKET;
        sock->type = type; // SOCK_DGRAM/SOCK_STREAM
        if (host) {
            int len = strLength(host, sizeof(sock->host) - 1);
            strncpy(sock->host, host, len);
            sock->host[len] = 0;
        }
        sock->port = port;
    }
}

// ----------------------------------------------------------------------------

static int socketResolveHost(const char *host, UInt8 *addr, utBool alwaysResolve)
{
    // Note: 'addr' is assumed to be 6 bytes in length

    /* invalid addr */
    if (!addr) {
        return COMERR_SOCKET_HOST;
    }

    /* check for already resolved */
    if (!alwaysResolve) {
        // don't resolve if address has already been resolved
        // [NOTE: may have no effect if the socket structure is initialized to nulls on each open]
        int i;
        for (i = 0; i < 6; i++) {
            if (addr[i]) {
                return COMERR_SUCCESS;
            }
        }
    }

    /* invalid host name */
    if (!host || !*host) {
        return COMERR_SOCKET_HOST;
    }
    
    /* get host entry */
    struct hostent *he = (struct hostent*)0;
#if defined(TARGET_LINUX)
    // thread safe
    struct hostent rhe;
    char heBuf[512]; // was 256 - (too small for uClibc)
    // Exact size would be '460':
    //    sizeof(struct in_addr) +
    //    sizeof(struct in_addr*)*2 + 
    //    sizeof(char*)*(ALIAS_DIM) +  // (ALIAS_DIM is (2 + 5/*MAX_ALIASES*/ + 1))
    //    384/*namebuffer*/ + 
    //    32/*margin*/;
    int heErrno = 0;
    gethostbyname_r(host, &rhe, heBuf, sizeof(heBuf), &he, &heErrno);
#elif defined(TARGET_WINCE)
    // not thread safe
    WSASetLastError(0);
    he = gethostbyname(host); // this may fail if 'host' contains an IP address
    int heErrno = WSAGetLastError(); // WSAHOST_NOT_FOUND [11001]
    if (!he && isdigit(*host)) { // <-- simple test for IP address
        logWARNING(LOGSRC,"Attempting to parse IP address: %s", host);
        u_long ipAddr = inet_addr(host);
        if (ipAddr != INADDR_NONE) {
            he = gethostbyaddr((char*)&ipAddr, 4, AF_INET);
            if (he) {
                logINFO(LOGSRC,"Successful at obtaining hostent from IP address!");
                heErrno = 0;
            } else {
                heErrno = WSAGetLastError();
            }
        } else {
            logWARNING(LOGSRC,"Unable to parse IP address");
        }
    }
#else
    // not thread safe
    he = gethostbyname(host);
    int heErrno = h_errno;
#endif

    /* extract address */
    if (he) {
        int len = (he->h_length < 6)? he->h_length : 6;
        memcpy(addr, he->h_addr_list[0], len);
        return COMERR_SUCCESS;
    } else {
        logWARNING(LOGSRC,"Unable to resolve host [%d]: %s", heErrno, host);
        return COMERR_SOCKET_HOST;
    }
    
}

// ----------------------------------------------------------------------------

/* enable non-blocking mode */
utBool socketEnableNonBlockingClient(Socket_t *sock, utBool enable)
{
#if defined(IOCTIL_REQUEST_NON_BLOCKING)
    if (sock && (sock->sockfd != INVALID_SOCKET)) {
        IOCTL_ARG_TYPE enableNonBlocking = enable? 1 : 0;
        RESET_ERRNO; // WSASetLastError(0);
        int status = IOCTL_SOCKET(sock->sockfd, IOCTIL_REQUEST_NON_BLOCKING, &enableNonBlocking);
        if (status >= 0) {
            sock->nonBlock = enable;
            //logINFO(LOGSRC,"Socket set to non-blocking mode");
            return utTrue;
        } else {
            int err = ERRNO; // WSAGetLastError();
            logERROR(LOGSRC,"Unable to enable non-blocking mode [errno=%d]", err);
            return utFalse;
        }
    }
#endif
    return utFalse;
}

/* return true if non-blocking mode has been set for this client socket */
utBool socketIsNonBlockingClient(Socket_t *sock)
{
    if (sock && (sock->sockfd != INVALID_SOCKET)) {
        return sock->nonBlock;
    } else {
        return utFalse;
    }
}

// ----------------------------------------------------------------------------

/* client: open a UDP socket for writing */
int socketOpenUDPClient(Socket_t *sock, const char *host, int port)
{

    /* create socket */
    socketInitStruct(sock, host, port, SOCK_DGRAM);
    sock->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock->sockfd < 0) {
        // unlikely to occur
        return COMERR_SOCKET_OPEN;
    }

    /* resolve hostname */
    int err = socketResolveHost(host, sock->hostAddr, ALWAYS_RESOLVE_HOST);
    if (err != COMERR_SUCCESS) {
        //fprintf(stderr, "Unable to resolve host: %s\n", host);
        CLOSE_SOCKET(sock->sockfd);
        sock->sockfd = INVALID_SOCKET;
        return COMERR_SOCKET_HOST;
    }

    return COMERR_SUCCESS;

}

/* server: open a UDP socket for reading */
#if defined(ENABLE_SERVER_SOCKET)
int socketOpenUDPServer(Socket_t *sock, int port)
{

    /* create socket */
    socketInitStruct(sock, (char*)0, port, SOCK_DGRAM);
    sock->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock->sockfd < 0) {
        // unlikely to occur
        return COMERR_SOCKET_OPEN;
    }
        
    /* bind to local port (RX only) */
    struct sockaddr_in my_addr;             // connector's address information
    my_addr.sin_family = AF_INET;           // host byte order
    my_addr.sin_port = htons(sock->port);   // short, network byte order
    my_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock->sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1) {
        // Unable to bind server to specified port
        //fprintf(stderr, "Unable to bind to port %d\n", port);
        CLOSE_SOCKET(sock->sockfd);
        sock->sockfd = INVALID_SOCKET;
        return COMERR_SOCKET_BIND;
    }

    return COMERR_SUCCESS;

}
#endif

// ----------------------------------------------------------------------------

/* client: open a TCP client socket */
int socketOpenTCPClient(Socket_t *sock, const char *host, int port)
{

    /* init socket */
    socketInitStruct(sock, host, port, SOCK_STREAM);
    sock->sockfd = socket(AF_INET, SOCK_STREAM, 0); // SOCK_DGRAM
    if (sock->sockfd == INVALID_SOCKET) {
        // unlikely to occur
        return COMERR_SOCKET_OPEN;
    }

    /* resolve hostname */
    int err = socketResolveHost(host, sock->hostAddr, ALWAYS_RESOLVE_HOST);
    if (err != COMERR_SUCCESS) {
        //fprintf(stderr, "Unable to resolve host: %s\n", host);
        CLOSE_SOCKET(sock->sockfd);
        sock->sockfd = INVALID_SOCKET;
        return COMERR_SOCKET_HOST;
    }

    /* connect */
    struct sockaddr_in their_addr; // connector's address information
    their_addr.sin_family = AF_INET;      // host byte order
    their_addr.sin_port   = htons(port);  // short, network byte order
    their_addr.sin_addr   = *((struct in_addr *)sock->hostAddr);
    memset(&(their_addr.sin_zero), 0, sizeof(their_addr.sin_zero));  // (8) zero the rest of the struct
    RESET_ERRNO; // WSASetLastError(0);
    if (connect(sock->sockfd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) < 0) {
        // Unable to connect to specified host:port
        int err = ERRNO;
        logERROR(LOGSRC,"Unable to establish socket connect [errno=%d]", err);
        CLOSE_SOCKET(sock->sockfd);
        sock->sockfd = INVALID_SOCKET;
        return COMERR_SOCKET_CONNECT;
    }
    
    /* non-blocking mode */
#if defined(TARGET_WINCE)
    // set non-blocking AFTER connection
    socketEnableNonBlockingClient(sock, utTrue);
#endif

    /* send timeout (SO_SNDTIMEO) */
    //(This is not supported on all platforms)
    //struct timeval timeo;
    //utcGetTimestampDelta(&timeo, 10000L);
    //if (setsockopt(sock->sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeo, sizeof(timeo)) == -1) {
    //    // Unable to set client socket options
    //    CLOSE_SOCKET(sock->sockfd);
    //    sock->sockfd = INVALID_SOCKET;
    //    return COMERR_SOCKET_OPTION;
    //}

    /* success */
    return COMERR_SUCCESS;

}

/* server: is client socket open */
utBool socketIsOpenClient(Socket_t *sock)
{
    return (sock && (sock->sockfd != INVALID_SOCKET))? utTrue : utFalse;
}

// ----------------------------------------------------------------------------

#if defined(ENABLE_SERVER_SOCKET)

/* server: open a TCP server socket */
int socketOpenTCPServer(Socket_t *sock, int port)
{

    /* init */
    socketInitStruct(sock, 0, port, SOCK_STREAM);

    /* socket */
    sock->serverfd = socket(AF_INET, SOCK_STREAM, 0); // SOCK_DGRAM
    if (sock->serverfd == INVALID_SOCKET) {
        return COMERR_SOCKET_OPEN;
    }
    
    /* reuse address */
    int yes = 1;
    if (setsockopt(sock->serverfd, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(int)) == -1) {
        // Unable to set server socket options
        CLOSE_SOCKET(sock->serverfd);
        sock->serverfd = INVALID_SOCKET;
        return COMERR_SOCKET_OPTION;
    }
    
    /* send timeout? (SO_SNDTIMEO) */

    /* linger on close */
    struct linger so_linger;
    so_linger.l_onoff  = 1; // linger on
    so_linger.l_linger = 2; // 2 seconds
    if (setsockopt(sock->serverfd, SOL_SOCKET, SO_LINGER, (char*)&so_linger, sizeof(struct linger)) == -1) {
        // Unable to set server socket options
        CLOSE_SOCKET(sock->serverfd);
        sock->serverfd = INVALID_SOCKET;
        return COMERR_SOCKET_OPTION;
    }

    /* bind to port */
    struct sockaddr_in my_addr;             // my address information
    my_addr.sin_family = AF_INET;           // host byte order
    my_addr.sin_port = htons(sock->port);   // short, network byte order
    my_addr.sin_addr.s_addr = INADDR_ANY;   // auto-fill with my IP
    memset(&my_addr.sin_zero, 0, 8); // zero the rest of the struct
    if (bind(sock->serverfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
        // Unable to bind server to specified port
        CLOSE_SOCKET(sock->serverfd);
        sock->serverfd = INVALID_SOCKET;
        return COMERR_SOCKET_BIND;
    }
    
    /* set listen */
    if (listen(sock->serverfd, 1) == -1) {
        // Unable to listen on specified port
        CLOSE_SOCKET(sock->serverfd);
        sock->serverfd = INVALID_SOCKET;
        return COMERR_SOCKET_OPEN;
    }
    
    return 0;

}

/* server: is server socket open */
utBool socketIsOpenServer(Socket_t *sock)
{
    return (sock && (sock->serverfd != INVALID_SOCKET))? utTrue : utFalse;
}

/* server: accept an incoming client */
int socketAcceptTCPClient(Socket_t *sock)
{
    if (sock && (sock->serverfd != INVALID_SOCKET)) {
        struct sockaddr_in clientAddr;
        socklen_t alen = sizeof(clientAddr);
        sock->sockfd = accept(sock->serverfd, (struct sockaddr *)&clientAddr, &alen);
        if (sock->sockfd == INVALID_SOCKET) {
            // client accept failed
            return COMERR_SOCKET_ACCEPT;
        }
        return 0;
    } else {
        return COMERR_SOCKET_FILENO;
    }
}

#endif

// ----------------------------------------------------------------------------

/* client: close */
int socketCloseClient(Socket_t *sock)
{
    if (sock && (sock->sockfd != INVALID_SOCKET)) {
        // SO_LINGER?
        CLOSE_SOCKET(sock->sockfd);
        sock->sockfd = INVALID_SOCKET;
    }
    return 0;
}

#if defined(ENABLE_SERVER_SOCKET)
/* server: close */
int socketCloseServer(Socket_t *sock)
{
    socketCloseClient(sock);
    if (sock && (sock->serverfd != INVALID_SOCKET)) {
        CLOSE_SOCKET(sock->serverfd);
        sock->serverfd = INVALID_SOCKET;
    }
    return 0;
}
#endif

// ----------------------------------------------------------------------------

/* return true if any data is available for reading */
static utBool socketIsDataAvailable(Socket_t *sock, long timeoutMS)
{
    if (sock && (sock->sockfd != INVALID_SOCKET)) {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(sock->sockfd, &rfds);
        tv.tv_sec  = timeoutMS / 1000L;
        tv.tv_usec = (timeoutMS % 1000L) * 1000L;
        RESET_ERRNO; // WSASetLastError(0);
        select(sock->sockfd + 1, &rfds, 0, 0, &tv);
        if (FD_ISSET(sock->sockfd, &rfds)) {
            return utTrue;
        }
    }
    return utFalse;
}

/* return number of bytes available for reading */
static int socketGetAvailableCount(Socket_t *sock)
{
    if (sock && (sock->sockfd != INVALID_SOCKET)) {
        IOCTL_ARG_TYPE nBytesAvail = 0;
        int status = IOCTL_SOCKET(sock->sockfd, IOCTIL_REQUEST_BYTES_AVAIL, &nBytesAvail);
        int availBytes = (status >= 0)? (int)nBytesAvail : 0;
        return availBytes;
    } else {
        return -1;
    }
}

/* client/server: read TCP */
int socketReadTCP(Socket_t *sock, UInt8 *buf, int bufSize, long timeoutMS)
{
    if (sock && (sock->sockfd != INVALID_SOCKET)) {
        
        /* nothing to read? */
        if ((bufSize <= 0) || !buf) {
            return 0;
        }
        
        /* current timestamp */
        struct timeval ts;
        utcGetTimestamp(&ts);

        int tot = 0;
        UInt32 zeroLengthRead = 0L;
        while (tot < bufSize) {
            
            /* wait for data */
            //int avail = 0;
            if (timeoutMS > 0L) {
                Int32 deltaMS = utcGetDeltaMillis(&ts, 0);
                if (deltaMS >= timeoutMS) { 
                    // timeout
                    //logWARNING(LOGSRC,"Socket read timeout ... %ld >= %ld", deltaMS, timeoutMS);
                    //return COMERR_SOCKET_TIMEOUT;
                    break;
                }
                // timeoutMS -= deltaMS; <-- removed to fix timeout problem
                if (sock->avail <= 0) {
                    utBool hasData = socketIsDataAvailable(sock, timeoutMS - deltaMS);
                    sock->avail = socketGetAvailableCount(sock);
                    //printf("[socket.c] avail=%d [%d]\n", sock->avail, hasData);
                    if (sock->avail <= 0) {
                        if (hasData) {
                            // This means that getting the available count has failed.
                            // (This seems to always be the case on Cygwin)
                            sock->avail = 1; // at least 1 byte is available for reading
                        } else {
                            // try again on next loop
                            continue;
                        }
                    }
                } else {
                    // data is already available
                    // leave sock->avail as-is
                }
            } else {
                // no timeout is in place, just read everything we need
                sock->avail = bufSize - tot;
            }
            
            /* read data */
            int readLen = bufSize - tot;
            if (readLen > sock->avail) { readLen = sock->avail; }
            RESET_ERRNO; // WSASetLastError(0);
            int cnt = recv(sock->sockfd, (char*)(buf + tot), readLen, 0); // MSG_WAITALL);
            if (cnt < 0) {  // can this be <= 0 to eliminate the check below?
                int err = ERRNO; // WSAGetLastError();
                if (err == EWOULDBLOCK) {
                    logERROR(LOGSRC,"'recv' EWOULDBLOCK [errno=%d]", err);
                } else {
                    logERROR(LOGSRC,"'recv' error [errno=%d]", err);
                }
                sock->avail = 0;
                return COMERR_SOCKET_READ; 
            } else
            if (cnt == 0) {
                zeroLengthRead++;
                if (zeroLengthRead > 10L) {
                    // if the specified 'fd' has been closed, it's possible that
                    // 'cnt' will be zero (forever), rather than '-1'  This section 
                    // is a hack which checks this possibility and exits accordingly.
                    logERROR(LOGSRC,"Excessive zero leangth reads!");
                    sock->avail = 0;
                    return COMERR_SOCKET_READ;
                }
            } else {
                tot += cnt;
                sock->avail -= cnt;
                zeroLengthRead = 0L;
            }
            
        } // while (tot < bufSize)
        //logINFO(LOGSRC,"Socket read bytes: %d", tot);

        /* return bytes read */
        if (tot > 0L) {
            // if (tot < bufSize) then a timeout has occurred
            return tot;
        } else {
            logERROR(LOGSRC,"Socket read timeout");
            return COMERR_SOCKET_TIMEOUT;
        }
        
    } else {
        
        // Invalid 'socketReadTCP' fileno
        logERROR(LOGSRC,"Invalid socket file number");
        return COMERR_SOCKET_FILENO;
        
    }
}

#if defined(ENABLE_SERVER_SOCKET)
/* server: read UDP */
int socketReadUDP(Socket_t *sock, UInt8 *buf, int bufSize, long timeoutMS)
{
    if (sock && (sock->sockfd != INVALID_SOCKET)) {

        /* check for available data? */
        if (timeoutMS >= 0L) {
            utBool hasData = socketIsDataAvailable(sock, timeoutMS);
            if (!hasData) {
                return 0;
            }
        }

        /* read */
        struct sockaddr_in sender_addr;
        socklen_t sender_length = sizeof(sender_addr);
        int len = recvfrom(sock->sockfd, (char*)buf, bufSize, 0, (struct sockaddr*)&sender_addr, &sender_length);
        //printf("Read %d bytes: %s:%d)\n", len, inet_ntoa(sender_addr.sin_addr), sender_addr.sin_port);
        return len;
        
    } else {
        
        // Invalid 'socketReadUDP' fileno
        return COMERR_SOCKET_FILENO;
        
    }
}
#endif

// ----------------------------------------------------------------------------

/* return true if any data is available for reading */
static utBool socketIsSendReady(Socket_t *sock, long timeoutMS)
{
    if (sock && (sock->sockfd != INVALID_SOCKET)) {
        fd_set wfds;
        struct timeval tv;
        FD_ZERO(&wfds);
        FD_SET(sock->sockfd, &wfds);
        tv.tv_sec  = timeoutMS / 1000L;
        tv.tv_usec = (timeoutMS % 1000L) * 1000L;
        RESET_ERRNO; // WSASetLastError(0);
        select(sock->sockfd + 1, 0, &wfds, 0, &tv); // 'tv' may be updated
        if (FD_ISSET(sock->sockfd, &wfds)) {
            return utTrue;
        }
    }
    return utFalse;
}

/* client/server: write TCP */
int socketWriteTCP(Socket_t *sock, const UInt8 *buf, int bufLen)
{
    // On one occasion it appears that 'send(...)' has locked up when GPRS 
    // coverage became unavilable in the middle of the transmission.
    // Since 'SO_SNDTIMEO' is not sidely supported, socket set to non-blocking,
    // and 'select' is used to determine 'send'ability.
    if (sock && (sock->sockfd != INVALID_SOCKET)) {
        
        /* 'select' timeout */
        // Maximum possible time for this function to complete would be 
        // (bufLen * selectTimeoutMS), but would only occur if the transmitter
        // could only transmit 1 byte every 'selectTimeoutMS' seconds, which is
        // extremely unlikely.  If this does become an issue, a 'readTimeoutMS'
        // should be implemented which limits the accumulation of 'select' timeouts.
        long selectTimeoutMS = 10000L;
        
        /* 'send' loop */
        int tot = 0;
        UInt32 zeroLengthSend = 0L;
        while (tot < bufLen) {

            /* check for 'send' ready */
            if (!socketIsSendReady(sock,selectTimeoutMS)) {
                int err = ERRNO;
                logERROR(LOGSRC,"Timeout waiting for 'send' [errno=%d]", err);
                return COMERR_SOCKET_TIMEOUT;
            }

            /* send data */
            int flags = 0;
            RESET_ERRNO; // WSASetLastError(0);
            int cnt = send(sock->sockfd, (char*)(buf + tot), bufLen - tot, flags);
            if (cnt < 0) {
                int err = ERRNO; // WSAGetLastError();
                if (err == ECONNRESET) {
                    logERROR(LOGSRC,"Connection reset by peer [errno=%d]", err);
                } else
                if ((err == EWOULDBLOCK) || (err == EAGAIN)) {
                    logERROR(LOGSRC,"'send' would block [errno=%d]", err);
                } else {
                    logERROR(LOGSRC,"Socket 'send' error [errno=%d]", err);
                }
                return COMERR_SOCKET_WRITE;
            } else 
            if (cnt == 0) {
                // normally should not occur, but check anyway
                zeroLengthSend++;
                if (zeroLengthSend > 20L) {
                    logERROR(LOGSRC,"Too many zero-length 'send's");
                    return COMERR_SOCKET_WRITE;
                }
            } else
            if (cnt < (bufLen - tot)) {
                // normal condition in non-blocking mode
            }
            tot += cnt;
            
        }
        return tot;
        
    } else {
        
        // Invalid 'socketWriteTCP' fileno
        return COMERR_SOCKET_FILENO;
        
    }
}

/* client: write UDP */
int socketWriteUDP(Socket_t *sock, const UInt8 *buf, int bufLen)
{
    if (sock && (sock->sockfd != INVALID_SOCKET)) {

        /* check for 'send' ready */
        long selectTimeoutMS = 10000L;
        if (!socketIsSendReady(sock,selectTimeoutMS)) {
            int err = ERRNO;
            logERROR(LOGSRC,"Timeout waiting for 'sendto' [errno=%d]", err);
            return COMERR_SOCKET_TIMEOUT;
        }

        /* send datagram */
        struct sockaddr_in their_addr;
        their_addr.sin_family = AF_INET;
        their_addr.sin_port = htons(sock->port); // network byte order
        their_addr.sin_addr = *((struct in_addr *)sock->hostAddr);
        int cnt = sendto(sock->sockfd, (char*)buf, bufLen, 0, (struct sockaddr *)&their_addr, sizeof(their_addr));
        if (cnt < 0) { return COMERR_SOCKET_WRITE; }
        return bufLen;
        
    } else {
        
        // Invalid 'socketWriteUDP' fileno
        return COMERR_SOCKET_FILENO;
        
    }
}

// ----------------------------------------------------------------------------

//#define SOCKET_MAIN

#ifdef SOCKET_MAIN
int main(int argc, char *argv[])
{
    Socket_t sock;
    int port = 15152;
    int i, x;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-txu")) {
            printf("UDP TX ...\n");
            char msg[64];
            socketOpenUDP(&sock, "localhost", port);
            for (x = 0; x < 5000; x++) {
                sprintf(msg, "$ %d data\r", x);
                socketWriteUDP(&sock, msg, strlen(msg));
            }
            continue;
        }
        if (!strcmp(argv[i], "-txt")) {
            printf("TCP TX ...\n");
            char msg[64];
            socketOpenTCPClient(&sock, "localhost", port);
            for (x = 0; x < 5000; x++) {
                sprintf(msg, "$ %d data\r", x);
                socketWriteTCP(&sock, msg, strlen(msg));
            }
            continue;
        }
#if defined(ENABLE_SERVER_SOCKET)
        if (!strcmp(argv[i], "-rxu")) {
            printf("UDP RX ...\n");
            char data[600];
            socketOpenUDP(&sock, "localhost", port);
            for (x = 0; x < 5000; x++) {
                int dataLen = socketReadUDP(&sock, data, sizeof(data));
                if (dataLen <= 0) {
                    printf("Read error: %d\n", dataLen);
                } else
                if (*data == '$') {
                    data[dataLen] = 0;
                    char *s = data;
                    for (;*s;s++) {
                        printf("%c", *s);
                        if (*s == '\r') { printf("\n"); }
                    }
                    printf("\n");
                } else {
                    printf("UDP read: %d bytes\n", dataLen);
                }
            }
            continue;
        } 
        if (!strcmp(argv[i], "-rxt")) {
            printf("TCP RX ...\n");
            char data[600];
            socketOpenTCPServer(&sock, port);
            socketAcceptTCPClient(&sock);
            for (x = 0; x < 5000; x++) {
                int dataLen = 0;
                while (1) {
                    int len = socketReadTCP(&sock, &data[dataLen], 1, 3000L);
                    if (len <= 0) {
                        dataLen = len;
                        break;
                    }
                    if ((data[dataLen] == '\n') || (data[dataLen] == '\r')) {
                        data[dataLen] = 0;
                        break;
                    }
                    dataLen += len;
                }
                if (dataLen <= 0) {
                    printf("Read error: %d\n", dataLen);
                    break;
                }
                printf("TCP read[%d]: %s\n", dataLen, data);
            }
            continue;
        } 
#endif
    }
    return 0;
}
#endif

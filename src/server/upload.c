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
//  File upload (server to client) support.
// ---
// Change History:
//  2006/01/15  Martin D. Flynn
//     -Initial release
//  2007/01/28  Martin D. Flynn
//     -Fixed "uploadSendFile" to properly send binary files.
//     -Added encoded upload file validation.
// ----------------------------------------------------------------------------

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/base64.h"
#include "tools/checksum.h"
#include "tools/io.h"

#include "server/server.h"
#include "server/upload.h"
#include "server/packet.h"
#include "server/protocol.h"
#include "server/log.h"

// ----------------------------------------------------------------------------
// these must match the values used by the client

#define UPLOAD_MAX_FILE_SIZE          250000L
#define UPLOAD_MAX_FILENAME_SIZE      64
#define UPLOAD_MAX_ENCODED_FILE_SIZE  (((UPLOAD_MAX_FILE_SIZE + 2L) / 3L) * 4L) // close approximation

#define UPLOAD_TYPE_FILE              0x01
#define UPLOAD_TYPE_DATA              0x02
#define UPLOAD_TYPE_END               0x03

// ----------------------------------------------------------------------------

//static char uploadEncodedFilename[80] = { 0 };
//
//void uploadSetEncodedFile(const char *file)
//{
//    // The reference file must be a Base64 encoded ASCII file.  All '\r' and/or
//    // '\n' characters are considered line terminators.  The first line of the
//    // file must specify the name and path of the file where it is to be
//    // located on the DMTP client.  Subsequent lines specify the Base64 ASCII
//    // encoded file itself.
//    memset(uploadEncodedFilename, 0, sizeof(uploadEncodedFilename));
//    if (file && *file) {
//        logINFO(LOGSRC,"Setting upload file: %s", file);
//        strncpy(uploadEncodedFilename, file, sizeof(uploadEncodedFilename) - 1);
//    }
//}
//
//utBool uploadEncodedFileNow()
//{
//    if (*uploadEncodedFilename) {
//        utBool rtn = uploadSendEncodedFile(uploadEncodedFilename);
//        *uploadEncodedFilename = 0;
//        return rtn;
//    } else {
//        return utFalse;
//    }
//}

// ----------------------------------------------------------------------------

//static char uploadFilename[80] = { 0 };
//static char clientFilename[80] = { 0 };
//
//void uploadSetFile(const char *uplFile, const char *cliFile)
//{
//    memset(uploadFilename, 0, sizeof(uploadFilename));
//    memset(clientFilename, 0, sizeof(clientFilename));
//    if (uplFile && *uplFile) {
//        logINFO(LOGSRC,"Setting upload file: %s", uplFile);
//        strncpy(uploadFilename, uplFile, sizeof(uploadFilename) - 1);
//        if (!cliFile || !*cliFile) { cliFile = uplFile; }
//        logINFO(LOGSRC,"Setting client file: %s", cliFile);
//        strncpy(clientFilename, cliFile, sizeof(clientFilename) - 1);
//    }
//}
//
//utBool uploadFileNow()
//{
//    if (*uploadFilename) {
//        utBool rtn = uploadSendFile(uploadFilename, clientFilename);
//        *uploadFilename = 0;
//        *clientFilename = 0;
//        return rtn;
//    } else {
//        return utFalse;
//    }
//}

// ----------------------------------------------------------------------------

utBool uploadSendData(const char *clientFileName, const UInt8 *data, Int32 dataLen)
{
    UInt32 ofs = 0L;
    UInt8 buf[PACKET_MAX_PAYLOAD_LENGTH];
    int bufLen;
    
    /* null client file name? */
    if (!clientFileName) {
        return utFalse;
    }

    /* file name */
    bufLen = binPrintf(buf, sizeof(buf), "%1x%3x%*s", (UInt32)UPLOAD_TYPE_FILE, (UInt32)dataLen, (int)UPLOAD_MAX_FILENAME_SIZE, clientFileName);
    if (!protUploadPacket(buf, bufLen)) {
        return utFalse;
    }
    // Ideally, we should wait for an acknowledgement from the client that the filename
    // was acceptable.

    /* data */
    while (ofs < dataLen) {
        int len = 245; // leave some room for the overhead
        if (len > (dataLen - ofs)) {
            len = dataLen - ofs;
        }
        bufLen = binPrintf(buf, sizeof(buf), "%1x%3x%*b", (UInt32)UPLOAD_TYPE_DATA, (UInt32)ofs, (int)len, &data[ofs]);
        if (!protUploadPacket(buf, bufLen)) {
            return utFalse;
        }
        ofs += len;
    }

    /* end */
    bufLen = binPrintf(buf, sizeof(buf), "%1x%3x%1x%1x", (UInt32)UPLOAD_TYPE_END, (UInt32)dataLen, (UInt32)0, (UInt32)0);
    if (!protUploadPacket(buf, bufLen)) {
        return utFalse;
    }
    // Ideally, we should wait for an acknowledgement from the client that the upload 
    // was successful.

    /* success */
    return utTrue;
    
}

// ----------------------------------------------------------------------------

utBool uploadSendEncodedFile(const char *localFileName)
{
    // The referenced file must be a Base64 encoded ASCII file.  All '\r' and/or
    // '\n' characters are considered line terminators.  The first line of the
    // file must specify the name and path of the file where it is to be
    // located on the DMTP client.  Subsequent lines specify the Base64 ASCII
    // encoded file itself.
    
    if (!localFileName || !*localFileName) {
        // nothing to upload
        return utFalse;
    }
    logINFO(LOGSRC,"Uploading encoded file: %s", localFileName);

    /* file size */
    struct stat st;
    int rtn = stat(localFileName, &st);
    long encodedFileSize = (rtn == 0)? st.st_size : -1L;
    if (encodedFileSize <= 0L) {
        // file empty, or non existant
        logERROR(LOGSRC,"Encoded upload file not found (or is empty)");
        return utFalse;
    } else
    if (encodedFileSize >= UPLOAD_MAX_ENCODED_FILE_SIZE) {
        // file too large
        logERROR(LOGSRC,"Encoded upload file is too large");
        return utFalse;
    }
    
    /* open file */
    FILE *file = ioOpenStream(localFileName, IO_OPEN_READ);
    if (!file) {
        // open error
        logERROR(LOGSRC,"Unable to open encoded upload file");
        return utFalse;
    }
    
    /* read fileName */
    unsigned char clientFileName[40], *fn = clientFileName;
    utBool commentLine = utFalse; // support leading comment lines
    for (;;) {
        
        /* read single character */
        unsigned char ch[1];
        long len = ioReadStream(file, ch, 1);
        if (len <= 0) {
            // EOF / read error
            logERROR(LOGSRC,"Encoded upload file read error");
            ioCloseStream(file);
            return utFalse;
        }
        
        /* line terminator */
        if ((*ch == '\r') || (*ch == '\n')) {
            if (commentLine) {
                // comment-line terminated (trailing line-terminator may still be remaining)
                commentLine = utFalse;
                continue;
            } else
            if (fn == clientFileName) {
                // filename is empty (found a leading line terminator)
                continue;
            } else {
                // we now have the client filename
                *fn = 0;
                break;
            }
        }
        
        /* inside a comment line? */
        if (commentLine) {
            // ignore all characters (except line terminators) on a comment line
            continue;
        }
        
        /* start of a comment line */
        // (valid only if no filename characters have been parsed)
        if ((fn == clientFileName) && (*ch == '#')) {
            // filename is empty, and we found a leading '#'
            commentLine = utTrue;
            continue;
        }
        
        /* invalid filename character */
        if (!isalnum(*ch) && (*ch != '/') && (*ch != '-') && (*ch != '_')) {
            // invalid filename
            logERROR(LOGSRC,"Invalid characters in encoded upload client filename");
            ioCloseStream(file);
            return utFalse;
        }
        
        /* save character */
        *fn++ = *ch;
        if ((fn - clientFileName) >= (sizeof(clientFileName) - 1)) {
            // filename too large
            logERROR(LOGSRC,"Encoded upload client filename is too large");
            ioCloseStream(file);
            return utFalse;
        }
        
    }
    
    /* read Base64 data */
    UInt8 b64[UPLOAD_MAX_ENCODED_FILE_SIZE], *b = b64;
    int lastEqualChars = 0;
    for (;;) {
        
        /* read a single character */
        long len = ioReadStream(file, b, 1);
        if (len <= 0) {
            if (feof(file)) {
                // normal EOF
                break;
            } else {
                // read error
                logERROR(LOGSRC,"Encoded upload file read error");
                ioCloseStream(file);
                return utFalse;
            }
        }
        
        /* ignore line terminators */
        if ((*b == '\n') || (*b == '\r') || (*b == 0)) {
            // ignore line terminators ('\n','\r')
            // ('0' included because I've seen some editors place nulls at the end of a file)
            continue;
        }
        
        /* check for Base64 terminating character */
        if ((*b == '=') && (lastEqualChars < 2)) {
            // No encoding characters should following the last 2 '='s (if present)
            lastEqualChars++;
            b++;
            continue;
        }
        
        /* check for invalid Base64 characters */
        if (lastEqualChars || (!isalnum(*b) && (*b != '+') && (*b != '/'))) {
            // invalid Base64 character
            logERROR(LOGSRC,"Invalid Base64 characters in encoded upload file");
            ioCloseStream(file);
            return utFalse;
        }
        
        /* save character and advance */
        b++;
        
    }
    ioCloseStream(file);
    
    /* test data length */
    long b64Len = b - b64;
    if (b64Len <= 0L) {
        // read error
        logERROR(LOGSRC,"Encoded upload file read error");
        return utFalse;
    }
    
    /* decode Base64 data in-place */
    UInt8 *data = b64;
    long dataLen = base64Decode(b64, b64Len, data, sizeof(b64));
    if (dataLen <= 0L) {
        // invalid data length
        logERROR(LOGSRC,"Invalid encoded upload file Base64 data length");
        return utFalse;
    }

    /* send to client */
    return uploadSendData(clientFileName, data, dataLen);
    
}
// ----------------------------------------------------------------------------

utBool uploadSendFile(const char *localFileName, const char *clientFileName)
{
    
    /* invalid local file name? */
    if (!localFileName || !*localFileName) {
        // nothing to upload
        return utFalse;
    }
    logINFO(LOGSRC,"Uploading file: %s", localFileName);

    /* file size */
    struct stat st;
    int rtn = stat(localFileName, &st);
    long localFileSize = (rtn == 0)? st.st_size : -1L;
    if (localFileSize <= 0L) {
        // file empty, or non existant
        logERROR(LOGSRC,"Upload file not found (or is empty)");
        return utFalse;
    } else
    if (localFileSize >= UPLOAD_MAX_FILE_SIZE) {
        // file too large
        logERROR(LOGSRC,"Upload file is too large");
        return utFalse;
    }
    
    /* open file */
    FILE *file = ioOpenStream(localFileName, IO_OPEN_READ);
    if (!file) {
        // open error
        logERROR(LOGSRC,"Unable to open upload file");
        return utFalse;
    }
        
    /* read Binary data */
    UInt8 data[UPLOAD_MAX_FILE_SIZE], *b = data;
    for (;;) {
        long len = ioReadStream(file, b, 1);
        if (len <= 0L) {
            if (feof(file)) {
                break; // EOF
            } else {
                logERROR(LOGSRC,"Upload file read error");
                ioCloseStream(file); // read error
                return utFalse;
            }
        }
        b++;
    }
    ioCloseStream(file);
    long dataLen = b - data;
    if (dataLen <= 0L) {
        // read error
        logERROR(LOGSRC,"Upload file read error");
        return utFalse;
    }

    /* send data to client */
    utBool ok = uploadSendData(clientFileName, data, dataLen);
    //logINFO(LOGSRC,"Upload file size = %ld", dataLen);
    return ok;
    
}

// ----------------------------------------------------------------------------

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
//  Encode a file for download to the client
// ---
// Change History:
//  2006/07/13  Martin D. Flynn
//     -Initial release
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#define SKIP_TRANSPORT_MEDIA_CHECK // only if TRANSPORT_MEDIA not used in this file
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/base64.h"
#include "tools/utctools.h"
#include "tools/io.h"

#include "encode.h"

// ----------------------------------------------------------------------------

#define DEFAULT_OUTPUT_WIDTH      100

// ----------------------------------------------------------------------------

utBool encodeDownloadEncode(const char *fromFile, const char *clientFile, int fileWidth)
{
    
    /* read data */
    UInt8 data[200000];
    long dataLen = ioReadFile(fromFile, data, sizeof(data));
    if (dataLen <= 0L) {
        // read error
        fprintf(stderr, "Unable to read file: %s\n", fromFile);
        return utFalse;
    }
    
    /* Base64 encode */
    char b64[300000];
    long b64Len = base64Encode(b64, sizeof(b64), data, dataLen);
    if (b64Len <= 0L) {
        // encode error
        fprintf(stderr, "Unable to encode: %s", fromFile);
        return utFalse;
    }
    
    /* write filename */
    printf("%s\n", clientFile);
    
    /* write data */
    long b = 0L;
    for (;(b64Len - b) > 0L;) {
        long len = (long)fileWidth;
        if (len > (b64Len - b)) { len = b64Len - b; }
        printf("%.*s\n", (int)len, &b64[b]);
        b += len;
    }
    return utTrue;

}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Main entry point

static void _usage(const char *pgm, int exitCode)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "   %s [-w <width>] <fromFile> <clientFile>\n", pgm);
    fprintf(stderr, "\n");
    exit(exitCode);
}

// main entry point
int main(int argc, char *argv[])
{
    int fileWidth = DEFAULT_OUTPUT_WIDTH;
    
    /* arg index */
    int argNdx = 1; // first argument

    /* file width */
    if ((argc > argNdx) && strStartsWith(argv[argNdx], "-w")) {
        argNdx++;
        if (argc > argNdx) {
            int fw = (int)strParseUInt32(argv[argNdx], DEFAULT_OUTPUT_WIDTH);
            if (fw > 0) {
                fileWidth = (fw < 20)? 20 : fw;
            }
            argNdx++;
        }
    }

    /* validate args */
    if ((argc - argNdx) < 2) { // at least 2 arguments remaining?
        _usage(argv[0], 1); 
        // does not return
    }
    
    /* encode */
    char *fromFile   = argv[argNdx++];
    char *clientFile = argv[argNdx++];
    if (!encodeDownloadEncode(fromFile, clientFile, fileWidth)) {
        fprintf(stderr, "Error encoding file ...\n");
        exit(1);
    }
    
    /* success */
    return 0;
    
}

// ----------------------------------------------------------------------------

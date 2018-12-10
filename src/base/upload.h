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

#ifndef _UPLOAD_H
#define _UPLOAD_H
#ifdef __cplusplus
extern "C" {
#endif
#if defined(ENABLE_UPLOAD)

// ----------------------------------------------------------------------------

#define UPLOAD_TIMEOUT_SEC              MINUTE_SECONDS(4)

// ----------------------------------------------------------------------------

#define UPLOAD_MAX_FILE_SIZE            200000L
#define UPLOAD_MAX_FILENAME_SIZE        64
#define UPLOAD_MAX_FILE_BLOCKSIZE       72L

// ----------------------------------------------------------------------------

#define UPLOAD_TYPE_FILE                0x01
#define UPLOAD_TYPE_DATA                0x02
#define UPLOAD_TYPE_END                 0x03

// ----------------------------------------------------------------------------

utBool uploadIsActive();
utBool uploadIsExpired();

void uploadCancel();

utBool uploadProcessRecord(int protoNdx, const UInt8 *rcd, int rcdLen);

// ----------------------------------------------------------------------------

#endif // defined(ENABLE_UPLOAD)
#ifdef __cplusplus
}
#endif
#endif // _UPLOAD_H

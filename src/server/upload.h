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

#include "server/defaults.h"
#include "server/server.h"

// ----------------------------------------------------------------------------

#if defined(INCLUDE_UPLOAD)
//#warning Including upload support

utBool uploadSendEncodedFile(const char *localFileName);

utBool uploadSendFile(const char *localFileName, const char *clientFileName);
utBool uploadSendData(const char *clientFileName, const UInt8 *data, Int32 dataLen);

#endif // INCLUDE_UPLOAD

// ----------------------------------------------------------------------------

#endif

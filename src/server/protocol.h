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

#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include "server/defaults.h"
#include "server/events.h"
#include "server/packet.h"

// ----------------------------------------------------------------------------

typedef void (*protDataCallbackFtn_t)(UInt16 key, const UInt8 *data, UInt16 dataLen);
typedef void (*protClientInitCallbackFtn_t)(void);
typedef void (*protEventCallbackFtn_t)(Packet_t *pkt, Event_t *ev);

// ----------------------------------------------------------------------------

const char *protGetDeviceID();

// ----------------------------------------------------------------------------

utBool protUploadPacket(const UInt8 *val, UInt8 valLen);

utBool protGetPropValue(UInt16 propKey);

utBool protSetPropBinary(UInt16 propKey, const UInt8 *val, UInt8 valLen);
utBool protSetPropString(UInt16 propKey, const UInt8 *val);
utBool protSetPropUInt32Arr(UInt16 propKey, UInt32 *val, int count);
utBool protSetPropUInt32(UInt16 propKey, UInt32 val);
utBool protSetPropUInt16Arr(UInt16 propKey, UInt16 *val, int count);
utBool protSetPropUInt16(UInt16 propKey, UInt16 val);
utBool protSetPropUInt8Arr(UInt16 propKey, UInt8 *val, int count);
utBool protSetPropUInt8(UInt16 propKey, UInt8 val);

// ----------------------------------------------------------------------------

void protSetNeedsMoreInfo();
void protSetSpeakFreelyMode(utBool mode, int maxEvents);

// ----------------------------------------------------------------------------

utBool protAddPendingPacket(Packet_t *pkt);

utBool protSetPendingFileUpload(const char *file, const char *cliFile);
void protocolLoop(
    const char *portName, utBool portLog,
    utBool cliKeepAlive, 
    utBool cliSpeaksFirst);
    
void protocolSetEventHandler(protEventCallbackFtn_t ftn);
void protocolSetClientInitHandler(protClientInitCallbackFtn_t ftn);
void protocolSetPropertyHandler(protDataCallbackFtn_t ftn);
void protocolSetPropertyHandler(protDataCallbackFtn_t ftn);
void protocolSetDiagHandler(protDataCallbackFtn_t ftn);
void protocolSetErrorHandler(protDataCallbackFtn_t ftn);

// ----------------------------------------------------------------------------

#endif

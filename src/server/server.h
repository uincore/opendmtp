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

#ifndef _SERVER_H
#define _SERVER_H

// ----------------------------------------------------------------------------

#include "server/defaults.h"
#include "server/packet.h"

// ----------------------------------------------------------------------------

#define SRVERR_OK                   0
#define SRVERR_TIMEOUT              1
#define SRVERR_TRANSPORT_ERROR      2
#define SRVERR_CHECKSUM_FAILED      3
#define SRVERR_PARSE_ERROR          4
#define SRVERR_PACKET_LENGTH        5

// ----------------------------------------------------------------------------

void serverInitialize();

utBool serverOpen(const char *portName, utBool portLog);
utBool serverIsOpen();
utBool serverClose();

void serverReadFlush();

int serverReadPacket(Packet_t *pkt);
utBool serverWritePacket(Packet_t *pkt);
utBool serverWritePacketFmt(ServerPacketType_t pktType, const char *fmt, ...);

utBool serverWriteError(const char *fmt, ...);

// ----------------------------------------------------------------------------

#endif

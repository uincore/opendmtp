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

#ifndef _PARSFILE_H
#define _PARSFILE_H

#include "tools/stdtypes.h"

#include "base/packet.h"

// ----------------------------------------------------------------------------

#define SRVERR_OK                   0
#define SRVERR_TIMEOUT              1
#define SRVERR_TRANSPORT_ERROR      2
#define SRVERR_CHECKSUM_FAILED      3
#define SRVERR_PARSE_ERROR          4
#define SRVERR_PACKET_LENGTH        5

// ----------------------------------------------------------------------------

utBool parseIsOpen();
utBool parseOpen(const char *fileName);
utBool parseClose();

int parseReadPacket(Packet_t *pkt);

// ----------------------------------------------------------------------------

#endif

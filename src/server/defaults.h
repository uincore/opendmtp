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

#ifndef _DEFAULTS_H
#define _DEFAULTS_H

// ----------------------------------------------------------------------------

// uncomment to include upload support
// (must also be supported on the client to be useful)
#define INCLUDE_UPLOAD

// uncomment to include upload GeoZone support
#define INCLUDE_GEOZONE

// uncomment to include authorization support
#define INCLUDE_AUTH

// uncomment to include misc custom support
// (the support this enables is an 'extension' to the OpenDMTP protocol)
#define INCLUDE_CUSTOM

// ----------------------------------------------------------------------------

// Experimental code
// uncomment to include experimental DMTP support (not yet part of the DMTP protocol)
#define DMTP_EXPERIMENTAL

// ----------------------------------------------------------------------------

#endif

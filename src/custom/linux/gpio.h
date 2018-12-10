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

#ifndef _GPIO_H
#define _GPIO_H
#ifdef __cplusplus
extern "C" {
#endif

#include "tools/stdtypes.h"

// ----------------------------------------------------------------------------

#define GPIO_TYPE_UNKNOWN       -1
#define GPIO_TYPE_INPUT         0
#define GPIO_TYPE_OUTPUT        1

// ----------------------------------------------------------------------------

void gpioInitialize();

utBool gpioGetInput(int dio);
void gpioSetInput(int dio, utBool state);   // debug only

utBool gpioGetOutput(int dio);
void gpioSetOutput(int dio, utBool state);

// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif // _GPIO_H

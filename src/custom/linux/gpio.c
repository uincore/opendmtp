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
//  Platform specific General-Purpose I/O
// Notes:
//  - Most of the utilities in this module were designed for the GumStix.  They
//    may need to be modified to fit other platforms.
// ---
// Change History:
//  2006/05/10  Martin D. Flynn
//     -Initial release
// ----------------------------------------------------------------------------

#include "stdafx.h" // TARGET_WINCE
#include "custom/defaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <time.h>
#include <math.h>

#include "custom/log.h"
#include "custom/linux/gpio.h"

#include "tools/stdtypes.h"
#include "tools/strtools.h"
#include "tools/utctools.h"
#include "tools/io.h"

// ----------------------------------------------------------------------------
// AC'97 GPIO ports seem the most reasonable to use.
// To initialize at boot up:
//   % modprobe proc_gpio 
//   % echo "GPIO in"   > /proc/gpio/GPIO29    # - SDATA_IN0 (pin 43)
//   % echo "GPIO in"   > /proc/gpio/GPIO32    # - SDATA_IN1 (pin 15)
//   % echo "GPIO out"  > /proc/gpio/GPIO30    # - SDATA_OUT (pin  5)
// To se/clear:
//   % echo "out set"   > /proc/gpio/GPIO30
//   % echo "out clear" > /proc/gpio/GPIO30
// To read current state:
//   % echo "in"        > /proc/gpio/GPIO29
//   % cat /proc/gpio/GPIO29
//   # will report either "set", or "clear"

#define GPIO_FILE               "/proc/gpio/GPIO%02d"
#define GPIO_SDATA_IN0          29
#define GPIO_SDATA_IN1          32
#define GPIO_SDATA_OUT          30

#define MAX_GPIO_IN             2
#define MAX_GPIO_OUT            1

#define GPIO_RETURN_DEFAULT         // define until we get hardware that supports GPIO
#define GPIO_DEFAULT_IN0        utTrue
#define GPIO_DEFAULT_IN1        utFalse

static utBool GPIO_STATE_INPUT[]  = { GPIO_DEFAULT_IN0, GPIO_DEFAULT_IN1 };
static utBool GPIO_STATE_OUTPUT[] = { utFalse };

// ----------------------------------------------------------------------------

#if defined(TARGET_GUMSTIX)
static void _gpioInit(int gpio, int gpioType)
{
    // % echo "GPIO in"   > /proc/gpio/GPIO??
    // or
    // % echo "GPIO out"  > /proc/gpio/GPIO??
    char gpioFile[64], *direction = (gpioType == GPIO_TYPE_INPUT)? "GPIO in" : "GPIO out";
    sprintf(gpioFile, GPIO_FILE, gpio);
    ioWriteFile(gpioFile, direction, strlen(direction));
}
#endif

void gpioInitialize()
{
#if defined(TARGET_GUMSTIX)
    _gpioInit(GPIO_SDATA_IN0, GPIO_TYPE_INPUT);
    _gpioInit(GPIO_SDATA_IN1, GPIO_TYPE_INPUT);
    _gpioInit(GPIO_SDATA_OUT, GPIO_TYPE_OUTPUT);
#endif
    int dio;
    for (dio = 0; dio < MAX_GPIO_OUT; dio++) {
        gpioSetOutput(dio, GPIO_STATE_OUTPUT[dio]);
    }
}

// ----------------------------------------------------------------------------

/* get digital input */
utBool gpioGetInput(int dio)
{
    // Getting IO through file access is rather cumbersome, however, this is
    // the only documented way to do this. When I find a better way, it will
    // be implemented.

#if defined(GPIO_RETURN_DEFAULT) || !defined(TARGET_GUMSTIX)

    if ((dio >= 0) && (dio < MAX_GPIO_IN)) {
        return GPIO_STATE_INPUT[dio];
    } else {
        return utFalse;
    }

#else

    /* get GPIO file and init to read input */
    char gpioFile[64] = { 0 };
    switch (dio) {
        case 0:
            // SDATA_IN0
            sprintf(gpioFile, GPIO_FILE, GPIO_SDATA_IN0);
            break;
        case 1:
            // SDATA_IN1
            sprintf(gpioFile, GPIO_FILE, GPIO_SDATA_IN1);
            break;
        default:
            // unsupported GPIO is returned as 'false'
            logERROR(LOGSRC,"Unsupported GPIO: %d", dio);
            return utFalse;
    }

    /* write input initialization to input port */
    ioWriteFile(gpioFile, "in", 2);
    
    /* read state */
    utBool state = utFalse;
    char ioState[32];
    long len = ioReadFile(gpioFile, ioState, sizeof(ioState) - 1);
    if (len > 0L) {
        ioState[len] = 0; // terminate string
        state = strEqualsIgnoreCase(ioState, "set"); // true if 'set', false otherwise
    } else {
        logERROR(LOGSRC,"Unable to read GPIO: %s", gpioFile);
        state = utFalse;
    }
    GPIO_STATE_INPUT[dio] = state;
    return state;
    
#endif
    
}

/* this function is for debug/simulation purposes only */
void gpioSetInput(int dio, utBool state) 
{
#if defined(GPIO_RETURN_DEFAULT) || !defined(TARGET_GUMSTIX)

    /* set debug input state */
    if ((dio >= 0) && (dio < MAX_GPIO_IN)) {
        GPIO_STATE_INPUT[dio] = state;
    }
    
#else

    logERROR(LOGSRC,"Setting GPIO 'input' not supported: %d", dio);
    
#endif
}

// ----------------------------------------------------------------------------

/* this function is typically for debug/simulation purposes only */
utBool gpioGetOutput(int dio) 
{
    if ((dio >= 0) && (dio < MAX_GPIO_OUT)) {
        // always return the last set state
        return GPIO_STATE_OUTPUT[dio];
    } else {
        return utFalse;
    }
}

/* set digital output */
void gpioSetOutput(int dio, utBool state)
{
    // Setting IO through file access is rather cumbersome, however, this is
    // the only documented way to do this. When I find a better way, it will
    // be implemented.

    /* get GPIO file and init to set/clear output */
    char gpioFile[64] = { 0 };
    switch (dio) {
        case 0:
            // SDATA_OUT
            sprintf(gpioFile, GPIO_FILE, GPIO_SDATA_OUT);
            break;
        default:
            // unsupported GPIO is returned as 'false'
            logERROR(LOGSRC,"Invalid GPIO: %d", dio);
            return;
    }
    GPIO_STATE_OUTPUT[dio] = state;

#if defined(TARGET_GUMSTIX)
    
    /* set output state */
    const char *stateStr = state? "out set" : "out clear";
    ioWriteFile(gpioFile, stateStr, strlen(stateStr));

#else

    logWARNING(LOGSRC,"GPIO Output is simulated on this platform: %d", dio);
    
#endif
}

// ----------------------------------------------------------------------------

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
//  OpenDMTP protocol status code constants.
// ----------------------------------------------------------------------------
// Change History:
//  2006/03/31  Martin D. Flynn
//     -Added new status codes:
//      STATUS_INITIALIZED, STATUS_WAYMARK
//  2007/01/28  Martin D. Flynn
//     -WindowsCE port
//     -Added new status codes:
//      STATUS_QUERY, STATUS_LOW_BATTERY, STATUS_OBC_FAULT, STATUS_OBC_RANGE,
//      STATUS_OBC_RPM_RANGE, STATUS_OBC_FUEL_RANGE, STATUS_OBC_OIL_RANGE,
//      STATUS_OBC_TEMP_RANGE, STATUS_MOTION_MOVING
//  2007/??/??  Martin D. Flynn
//     -Added new status code: STATUS_POWER_FAILURE
// ----------------------------------------------------------------------------

#ifndef _STATUS_CODES_H
#define _STATUS_CODES_H
#ifdef __cplusplus
extern "C" {
#endif

#include "tools/stdtypes.h"

// ----------------------------------------------------------------------------
// Reserved status codes: [E0-00 through FF-FF]
// Groups:
//      0xF0..  - Generic
//      0xF1..  - Motion
//      0xF2..  - Geofence
//      0xF4..  - Digital input/output
//      0xF6..  - Sensor input
//      0xF7..  - Temperature input
//      0xF8..  - Miscellaneous
//      0xF9..  - J1708
//      0xFD..  - Device status
// ----------------------------------------------------------------------------

typedef UInt16          StatusCode_t;

// ----------------------------------------------------------------------------
// No status code: 0x0000

#define STATUS_NONE                 0x0000

// ----------------------------------------------------------------------------
// Generic codes: 0xF000 to 0xF0FF

#define STATUS_INITIALIZED          0xF010
    // Description:
    //      General Status/Location information (event generated by some
    //      initialization function performed by the device).
    // Notes:
    //      - This contents of the payload must at least contain the current
    //      timestamp (and latitude and longitude if available).

#define STATUS_LOCATION             0xF020
    // Description:
    //      General Status/Location information
    // Notes:
    //      - This contents of the payload must at least contain the current
    //      timestamp, latitude, and longitude.

#define STATUS_WAYMARK              0xF030
    // Description:
    //      General Status/Location information (event generated by manual user
    //      intervention at the device. ie. pressing a 'Waymark' button).
    // Notes:
    //      - This contents of the payload must at least contain the current
    //      timestamp, latitude, and longitude.

#define STATUS_QUERY                0xF040
    // Description:
    //      General Status/Location information (event generated by a 'query'
    //      request from the server).
    // Notes:
    //      - This contents of the payload must at least contain the current
    //      timestamp, latitude, and longitude.

// ----------------------------------------------------------------------------
// Motion codes: 0xF100 to 0xF1FF

#define STATUS_MOTION_START         0xF111
    // Description:
    //      Device start of motion
    // Notes:
    //      - The definition of motion-start is provided by property PROP_MOTION_START

#define STATUS_MOTION_IN_MOTION     0xF112
    // Description:
    //      Device in-motion interval
    // Notes:
    //      - The in-motion interval is provided by property PROP_MOTION_IN_MOTION.
    //      - This status is typically used for providing in-motion events between
    //      STATUS_MOTION_START and STATUS_MOTION_STOP events.

#define STATUS_MOTION_STOP          0xF113
    // Description:
    //      Device stopped motion
    // Notes:
    //      - The definition of motion-stop is provided by property PROP_MOTION_STOP

#define STATUS_MOTION_DORMANT       0xF114
    // Description:
    //      Device dormant interval (ie. not moving)
    // Notes:
    //      - The dormant interval is provided by property PROP_MOTION_DORMANT

#define STATUS_MOTION_EXCESS_SPEED  0xF11A
    // Description:
    //      Device exceeded preset speed limit
    // Notes:
    //      - The excess-speed threshold is provided by property PROP_MOTION_EXCESS_SPEED
    //      - The 'speed' field should be checked for the actual speed of the vehicle.

#define STATUS_MOTION_MOVING        0xF11C
    // Description:
    //      Device is moving
    // Notes:
    //      - This status code may be used to indicating that the device was moving
    //      at the time the event was generated. It is typically not associated
    //      with the status codes STATUS_MOTION_START, STATUS_MOTION_STOP, and  
    //      STATUS_MOTION_IN_MOTION, and may be used independently of these codes.
    //      - This status code is typically used for devices that need to periodically
    //      report that they are moving, apart from the standard start/stop/in-motion
    //      events.

#define STATUS_ODOM_0               0xF130
#define STATUS_ODOM_1               0xF131
#define STATUS_ODOM_2               0xF132
#define STATUS_ODOM_3               0xF133
#define STATUS_ODOM_4               0xF134
#define STATUS_ODOM_5               0xF135
#define STATUS_ODOM_6               0xF136
#define STATUS_ODOM_7               0xF137
    // Description:
    //      Odometer value
    // Notes:
    //      The odometer limit is provided by property PROP_ODOMETER_#_LIMIT

#define STATUS_ODOM_LIMIT_0         0xF140
#define STATUS_ODOM_LIMIT_1         0xF141
#define STATUS_ODOM_LIMIT_2         0xF142
#define STATUS_ODOM_LIMIT_3         0xF143
#define STATUS_ODOM_LIMIT_4         0xF144
#define STATUS_ODOM_LIMIT_5         0xF145
#define STATUS_ODOM_LIMIT_6         0xF146
#define STATUS_ODOM_LIMIT_7         0xF147
    // Description:
    //      Odometer has exceeded a set limit
    // Notes:
    //      The odometer limit is provided by property PROP_ODOMETER_#_LIMIT

// ----------------------------------------------------------------------------
// Geofence/Geocorridor: 0xF200 to 0xF2FF

#define STATUS_GEOFENCE_ARRIVE      0xF210
    // Description:
    //      Device arrived at geofence
    // Notes:
    //      - Client may wish to include FIELD_GEOFENCE_ID in the event packet.

#define STATUS_GEOFENCE_DEPART      0xF230
    // Description:
    //      Device departed geofence
    // Notes:
    //      - Client may wish to include FIELD_GEOFENCE_ID in the event packet.

#define STATUS_GEOFENCE_VIOLATION   0xF250
    // Description:
    //      Geofence violation
    // Notes:
    //      - Client may wish to include FIELD_GEOFENCE_ID in the event packet.

#define STATUS_GEOFENCE_ACTIVE      0xF270
    // Description:
    //      Geofence now active
    // Notes:
    //      - Client may wish to include FIELD_GEOFENCE_ID in the event packet.

#define STATUS_GEOFENCE_INACTIVE    0xF280
    // Description:
    //      Geofence now inactive
    // Notes:
    //      - Client may wish to include FIELD_GEOFENCE_ID in the event packet.

#define STATUS_STATE_ENTER          0xF2A0
    // Description:
    //      Device has entered a state

#define STATUS_STATE_EXIT           0xF2B0
    // Description:
    //      Device has exited a state

// ----------------------------------------------------------------------------
// Digital input/output (state change): 0xF400 to 0xF4FF

#define STATUS_INPUT_STATE          0xF400
    // Description:
    //      Current input ON state (bitmask)
    // Notes:
    //      - Client should include FIELD_INPUT_STATE in the event packet,
    //      otherwise this status code would have no meaning.

#define STATUS_INPUT_ON             0xF402
    // Description:
    //      Input turned ON
    // Notes:
    //      - Client should include FIELD_INPUT_ID in the event packet,
    //      otherwise this status code would have no meaning.
    //      - This status code may be used to indicate that an arbitrary input
    //      'thing' turned ON, and the 'thing' can be identified by the 'Input ID'.
    //      This 'ID' can also represent the index of a digital input.

#define STATUS_INPUT_OFF            0xF404
    // Description:
    //      Input turned OFF
    // Notes:
    //      - Client should include FIELD_INPUT_ID in the event packet,
    //      otherwise this status code would have no meaning.
    //      - This status code may be used to indicate that an arbitrary input
    //      'thing' turned OFF, and the 'thing' can be identified by the 'Input ID'.
    //      This 'ID' can also represent the index of a digital input.

#define STATUS_OUTPUT_STATE         0xF406
    // Description:
    //      Current output ON state (bitmask)
    // Notes:
    //      - Client should include FIELD_OUTPUT_STATE in the event packet,
    //      otherwise this status code would have no meaning.

#define STATUS_OUTPUT_ON            0xF408
    // Description:
    //      Output turned ON
    // Notes:
    //      - Client should include FIELD_OUTPUT_ID in the event packet,
    //      otherwise this status code would have no meaning.
    //      - This status code may be used to indicate that an arbitrary output
    //      'thing' turned ON, and the 'thing' can be identified by the 'Output ID'.
    //      This 'ID' can also represent the index of a digital output.

#define STATUS_OUTPUT_OFF           0xF40A
    // Description:
    //      Output turned OFF
    // Notes:
    //      - Client should include FIELD_OUTPUT_ID in the event packet,
    //      otherwise this status code would have no meaning.
    //      - This status code may be used to indicate that an arbitrary output
    //      'thing' turned OFF, and the 'thing' can be identified by the 'Output ID'.
    //      This 'ID' can also represent the index of a digital output.

#define STATUS_INPUT_ON_00          0xF420
#define STATUS_INPUT_ON_01          0xF421
#define STATUS_INPUT_ON_02          0xF422
#define STATUS_INPUT_ON_03          0xF423
#define STATUS_INPUT_ON_04          0xF424
#define STATUS_INPUT_ON_05          0xF425
#define STATUS_INPUT_ON_06          0xF426
#define STATUS_INPUT_ON_07          0xF427
#define STATUS_INPUT_ON_08          0xF428
#define STATUS_INPUT_ON_09          0xF429
#define STATUS_INPUT_ON_10          0xF42A
#define STATUS_INPUT_ON_11          0xF42B
#define STATUS_INPUT_ON_12          0xF42C
#define STATUS_INPUT_ON_13          0xF42D
#define STATUS_INPUT_ON_14          0xF42E
#define STATUS_INPUT_ON_15          0xF42F
    // Description:
    //      Digital input state changed to ON

#define STATUS_INPUT_OFF_00         0xF440
#define STATUS_INPUT_OFF_01         0xF441
#define STATUS_INPUT_OFF_02         0xF442
#define STATUS_INPUT_OFF_03         0xF443
#define STATUS_INPUT_OFF_04         0xF444
#define STATUS_INPUT_OFF_05         0xF445
#define STATUS_INPUT_OFF_06         0xF446
#define STATUS_INPUT_OFF_07         0xF447
#define STATUS_INPUT_OFF_08         0xF448
#define STATUS_INPUT_OFF_09         0xF449
#define STATUS_INPUT_OFF_10         0xF44A
#define STATUS_INPUT_OFF_11         0xF44B
#define STATUS_INPUT_OFF_12         0xF44C
#define STATUS_INPUT_OFF_13         0xF44D
#define STATUS_INPUT_OFF_14         0xF44E
#define STATUS_INPUT_OFF_15         0xF44F
    // Description:
    //      Digital input state changed to OFF

#define STATUS_OUTPUT_ON_00         0xF460
#define STATUS_OUTPUT_ON_01         0xF461
#define STATUS_OUTPUT_ON_02         0xF462
#define STATUS_OUTPUT_ON_03         0xF463
#define STATUS_OUTPUT_ON_04         0xF464
#define STATUS_OUTPUT_ON_05         0xF465
#define STATUS_OUTPUT_ON_06         0xF466
#define STATUS_OUTPUT_ON_07         0xF467
    // Description:
    //      Digital output state set to ON

#define STATUS_OUTPUT_OFF_00        0xF480
#define STATUS_OUTPUT_OFF_01        0xF481
#define STATUS_OUTPUT_OFF_02        0xF482
#define STATUS_OUTPUT_OFF_03        0xF483
#define STATUS_OUTPUT_OFF_04        0xF484
#define STATUS_OUTPUT_OFF_05        0xF485
#define STATUS_OUTPUT_OFF_06        0xF486
#define STATUS_OUTPUT_OFF_07        0xF487
    // Description:
    //      Digital output state set to OFF

#define STATUS_ELAPSED_00           0xF4A0
#define STATUS_ELAPSED_01           0xF4A1
#define STATUS_ELAPSED_02           0xF4A2
#define STATUS_ELAPSED_03           0xF4A3
#define STATUS_ELAPSED_04           0xF4A4
#define STATUS_ELAPSED_05           0xF4A5
#define STATUS_ELAPSED_06           0xF4A6
#define STATUS_ELAPSED_07           0xF4A7
    // Description:
    //      Elapsed time
    // Notes:
    //      - Client should include FIELD_ELAPSED_TIME in the event packet,
    //      otherwise this status code would have no meaning.

#define STATUS_ELAPSED_LIMIT_00     0xF4B0
#define STATUS_ELAPSED_LIMIT_01     0xF4B1
#define STATUS_ELAPSED_LIMIT_02     0xF4B2
#define STATUS_ELAPSED_LIMIT_03     0xF4B3
#define STATUS_ELAPSED_LIMIT_04     0xF4B4
#define STATUS_ELAPSED_LIMIT_05     0xF4B5
#define STATUS_ELAPSED_LIMIT_06     0xF4B6
#define STATUS_ELAPSED_LIMIT_07     0xF4B7
    // Description:
    //      Elapsed timer has exceeded a set limit
    // Notes:
    //      - Client should include FIELD_ELAPSED_TIME in the event packet,
    //      otherwise this status code would have no meaning.

// ----------------------------------------------------------------------------
// Analog/etc sensor values (extra data): 0xF600 to 0xF6FF

#define STATUS_SENSOR32_0           0xF600
#define STATUS_SENSOR32_1           0xF601
#define STATUS_SENSOR32_2           0xF602
#define STATUS_SENSOR32_3           0xF603
#define STATUS_SENSOR32_4           0xF604
#define STATUS_SENSOR32_5           0xF605
#define STATUS_SENSOR32_6           0xF606
#define STATUS_SENSOR32_7           0xF607
    // Description:
    //      32-bit unsigned sensor value
    // Notes:
    //      - Client should include FIELD_SENSOR32 in the event packet,
    //      otherwise this status code would have no meaning.
    //      - The server must be able to convert this 32-bit value to something
    //      meaningful to the user.  This can be done using the following formula:
    //         Actual_Value = ((double)Sensor32_Value * <Gain>) + <Offset>;
    //      Where <Gain> & <Offset> are user configurable values provided at setup.
    //      For instance: Assume Sensor32-0 contains a temperature value that can
    //      have a range of -40.0C to +125.0C.  The client would encode -14.7C
    //      by adding 40.0 and multiplying by 10.0.  The resulting value would be
    //      253.  The server would then be configured to know how to convert this
    //      value back into the proper temperature using the above formula by
    //      substituting 0.1 for <Gain>, and -40.0 for <Offset>: eg.
    //          -14.7 = ((double)253 * 0.1) + (-40.0);

#define STATUS_SENSOR32_RANGE_0     0xF620
#define STATUS_SENSOR32_RANGE_1     0xF621
#define STATUS_SENSOR32_RANGE_2     0xF622
#define STATUS_SENSOR32_RANGE_3     0xF623
#define STATUS_SENSOR32_RANGE_4     0xF624
#define STATUS_SENSOR32_RANGE_5     0xF625
#define STATUS_SENSOR32_RANGE_6     0xF626
#define STATUS_SENSOR32_RANGE_7     0xF627
    // Description:
    //      32-bit unsigned sensor value out-of-range violation
    // Notes:
    //      - Client should include FIELD_SENSOR32 in the event packet,
    //      otherwise this status code would have no meaning.

// ----------------------------------------------------------------------------
// Temperature sensor values (extra data): 0xF700 to 0xF7FF

#define STATUS_TEMPERATURE_0        0xF710
#define STATUS_TEMPERATURE_1        0xF711
#define STATUS_TEMPERATURE_2        0xF712
#define STATUS_TEMPERATURE_3        0xF713
#define STATUS_TEMPERATURE_4        0xF714
#define STATUS_TEMPERATURE_5        0xF715
#define STATUS_TEMPERATURE_6        0xF716
#define STATUS_TEMPERATURE_7        0xF717
    // Description:
    //      Temperature value
    // Notes:
    //      - Client should include at least the field FIELD_TEMP_AVER in the 
    //      event packet, and may also wish to include FIELD_TEMP_LOW and
    //      FIELD_TEMP_HIGH.

#define STATUS_TEMPERATURE_RANGE_0  0xF730
#define STATUS_TEMPERATURE_RANGE_1  0xF731
#define STATUS_TEMPERATURE_RANGE_2  0xF732
#define STATUS_TEMPERATURE_RANGE_3  0xF733
#define STATUS_TEMPERATURE_RANGE_4  0xF734
#define STATUS_TEMPERATURE_RANGE_5  0xF735
#define STATUS_TEMPERATURE_RANGE_6  0xF736
#define STATUS_TEMPERATURE_RANGE_7  0xF737
    // Description:
    //      Temperature value out-of-range [low/high/average]
    // Notes:
    //      - Client should include at least one of the fields FIELD_TEMP_AVER,
    //      FIELD_TEMP_LOW, or FIELD_TEMP_HIGH.

#define STATUS_TEMPERATURE          0xF7F1
    // Description:
    //      All temperature averages [aver/aver/aver/...]

// ----------------------------------------------------------------------------
// Miscellaneous: 0xF800 to 0xF8FF

#define STATUS_LOGIN                0xF811
    // Description:
    //      Generic 'login'

#define STATUS_LOGOUT               0xF812
    // Description:
    //      Generic 'logout'

#define STATUS_CONNECT              0xF821
    // Description:
    //      Connect/Hook/On

#define STATUS_DISCONNECT           0xF822
    // Description:
    //      Disconnect/Drop/Off

#define STATUS_ACK                  0xF831
    // Description:
    //      Acknowledge

#define STATUS_NAK                  0xF832
    // Description:
    //      Negative Acknowledge

// ----------------------------------------------------------------------------
// OBC/J1708 status: 0xF900 to 0xF9FF

#define STATUS_OBC_FAULT            0xF911
    // Description:
    //      OBC/J1708 fault code occurred.
    // Notes:
    //      - Client should include the field FIELD_OBC_J1708_FAULT.

#define STATUS_OBC_RANGE            0xF920
    // Description:
    //      Generic OBC/J1708 value out-of-range
    // Notes:
    //      - Client should include at least one of the fields FIELD_OBC_VALUE,
    //      or one of the other FIELD_OBC_xxxxx fields.

#define STATUS_OBC_RPM_RANGE        0xF922
    // Description:
    //      OBC/J1708 RPM out-of-range
    // Notes:
    //      - Client should include the field FIELD_OBC_ENGINE_RPM.

#define STATUS_OBC_FUEL_RANGE       0xF924
    // Description:
    //      OBC/J1708 Fuel level out-of-range (ie. to low)
    // Notes:
    //      - Client should include the field FIELD_OBC_FUEL_LEVEL.

#define STATUS_OBC_OIL_RANGE        0xF926
    // Description:
    //      OBC/J1708 Oil level out-of-range (ie. to low)

#define STATUS_OBC_TEMP_RANGE       0xF928
    // Description:
    //      OBC/J1708 Temperature out-of-range
    // Notes:
    //      - Client should include at least one of the fields FIELD_OBC_xxxx
    //      fields which indicates a J1708 temperature out-of-range.

// ----------------------------------------------------------------------------
// Internal device status

#define STATUS_LOW_BATTERY          0xFD10
    // Description:
    //      Low battery indicator

#define STATUS_POWER_FAILURE        0xFD13
    // Description:
    //      Power-failure indicator

// ----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif
#endif

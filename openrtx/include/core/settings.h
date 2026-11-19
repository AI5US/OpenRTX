/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "hwconfig.h"
#include <stdbool.h>

typedef enum
{
    TIMER_OFF =  0,
    TIMER_5S  =  1,
    TIMER_10S =  2,
    TIMER_15S =  3,
    TIMER_20S =  4,
    TIMER_25S =  5,
    TIMER_30S =  6,
    TIMER_1M  =  7,
    TIMER_2M  =  8,
    TIMER_3M  =  9,
    TIMER_4M  = 10,
    TIMER_5M  = 11,
    TIMER_15M = 12,
    TIMER_30M = 13,
    TIMER_45M = 14,
    TIMER_1H  = 15
}
display_timer_t;

typedef struct
{
    uint8_t brightness;           // Display brightness
    uint8_t contrast;             // Display contrast
    uint8_t sqlLevel;             // Squelch level
    uint8_t voxLevel;             // Vox level
    int8_t  utc_timezone;         // Timezone, in units of half hours
    bool    gps_enabled;          // GPS active
    char    callsign[10];         // Plaintext callsign
    uint8_t display_timer   : 4,  // Standby timer
            m17_can         : 4;  // M17 CAN
    uint8_t vpLevel         : 3,  // Voice prompt level
            vpPhoneticSpell : 1,  // Phonetic spell enabled
            macroMenuLatch  : 1,  // Automatic latch of macro menu
            _reserved       : 3;
    bool    m17_can_rx;           // Check M17 CAN on RX
    char    m17_dest[10];         // M17 destination
    bool    showBatteryIcon;      // Battery display true: icon, false: percentage
    bool    gpsSetTime;           // Use GPS to ajust RTC time
    char    M17_meta_text[53];    // M17 Meta Text to send
#ifdef CONFIG_DSTAR
    char    dstar_ur[9];          // D-STAR companion (UR) callsign
    char    dstar_rpt1[9];        // D-STAR departure repeater (RPT1)
    char    dstar_rpt2[9];        // D-STAR destination repeater (RPT2)
    char    dstar_message[21];    // D-STAR slow data text message
#endif
#ifdef CONFIG_DMR
    uint32_t dmr_id;              // DMR radio ID
    uint32_t dmr_dest;            // DMR destination: talkgroup or radio ID
    uint8_t  dmr_cc        : 4,   // DMR colour code
             dmr_private   : 1,   // DMR private call to dmr_dest
             dmr_slot      : 1,   // DMR repeater timeslot, 0: TS1, 1: TS2
             _dmr_reserved : 2;
    int8_t   dmr_tx_timing;       // DMR repeater TX advance, in symbols
#endif
}
__attribute__((packed)) settings_t;


static const settings_t default_settings =
{
    100,                          // Brightness
#ifdef CONFIG_SCREEN_CONTRAST
    CONFIG_DEFAULT_CONTRAST,      // Contrast
#else
    255,                          // Contrast
#endif
    4,                            // Squelch level, 4 = S3
    0,                            // Vox level
    0,                            // UTC Timezone
    false,                        // GPS enabled
    "N0CALL",                     // Default callsign
    TIMER_30S,                    // 30 seconds
    0,                            // M17 CAN
    0,                            // Voice prompts off
    0,                            // Phonetic spell off
    1,                            // Automatic latch of macro menu enabled
    0,                            // not used
    false,                        // Check M17 CAN on RX
    "",                           // Empty M17 destination
    false,                        // Display battery icon
    false,                        // Update RTC with GPS
    "OpenRTX",                    // Default M17 meta text
#ifdef CONFIG_DSTAR
    "CQCQCQ",                     // D-STAR UR, call everyone
    "DIRECT",                     // D-STAR RPT1, simplex
    "DIRECT",                     // D-STAR RPT2, simplex
    "OpenRTX",                    // D-STAR text message
#endif
#ifdef CONFIG_DMR
    0,                            // DMR ID, not set
    9,                            // DMR destination, talkgroup 9
    1,                            // DMR colour code
    0,                            // DMR group call
    0,                            // DMR timeslot 1
    0,                            // not used
    0,                            // DMR repeater TX advance
#endif
};

#endif /* SETTINGS_H */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**
 * Nintendo Switch Pro Controller button state.
 *
 * Button bit positions match the 0x30 standard input report format
 * from dekuNukem/Nintendo_Switch_Reverse_Engineering.
 */
typedef struct {
    // Right-side buttons (byte 3 of 0x30 report)
    bool y;      // bit 0
    bool x;      // bit 1
    bool b;      // bit 2
    bool a;      // bit 3
    bool sr_r;   // bit 4
    bool sl_r;   // bit 5
    bool r;      // bit 6
    bool zr;     // bit 7

    // Shared buttons (byte 4)
    bool minus;    // bit 0
    bool plus;     // bit 1
    bool rstick;   // bit 2
    bool lstick;   // bit 3
    bool home;     // bit 4
    bool capture;  // bit 5

    // Left-side buttons (byte 5)
    bool down;   // bit 0
    bool up;     // bit 1
    bool right;  // bit 2
    bool left;   // bit 3
    bool sr_l;   // bit 4
    bool sl_l;   // bit 5
    bool l;      // bit 6
    bool zl;     // bit 7

    // Analog sticks (12-bit each, 0-4095, center = 0x800 = 2048)
    uint16_t left_x;
    uint16_t left_y;
    uint16_t right_x;
    uint16_t right_y;
} SwitchButtonState;

/**
 * Initialize button state to neutral (all released, sticks centered).
 */
static inline void switch_pro_report_init(SwitchButtonState* state) {
    memset(state, 0, sizeof(SwitchButtonState));
    state->left_x = 0x800;
    state->left_y = 0x800;
    state->right_x = 0x800;
    state->right_y = 0x800;
}

/**
 * Build a 64-byte 0x30 standard input report from button state.
 *
 * Report layout (from dekuNukem reverse engineering):
 *   Byte 0:     0x30 (report ID)
 *   Byte 1:     timer (incrementing counter 0x00-0xFF)
 *   Byte 2:     0x81 (battery full, Pro Controller + USB powered)
 *   Byte 3:     right-side buttons packed
 *   Byte 4:     shared buttons packed
 *   Byte 5:     left-side buttons packed
 *   Bytes 6-8:  left stick (12-bit X, 12-bit Y, packed little-endian)
 *   Bytes 9-11: right stick (same packing)
 *   Byte  12:   0x80 (vibrator input report)
 *   Bytes 13-48: IMU data (3 frames x 12 bytes: accel XYZ + gyro XYZ, Int16LE)
 *   Bytes 49-63: zeroes (padding)
 */
static inline void switch_pro_report_build(
    const SwitchButtonState* state,
    uint8_t* report,
    uint8_t timer) {
    memset(report, 0, 64);

    report[0] = 0x30; // Report ID
    report[1] = timer; // Incrementing counter
    report[2] = 0x81; // Battery full(8), Pro Controller + USB powered(1)

    // Byte 3: right-side buttons
    report[3] = (state->y ? 0x01 : 0) |
                (state->x ? 0x02 : 0) |
                (state->b ? 0x04 : 0) |
                (state->a ? 0x08 : 0) |
                (state->sr_r ? 0x10 : 0) |
                (state->sl_r ? 0x20 : 0) |
                (state->r ? 0x40 : 0) |
                (state->zr ? 0x80 : 0);

    // Byte 4: shared buttons
    report[4] = (state->minus ? 0x01 : 0) |
                (state->plus ? 0x02 : 0) |
                (state->rstick ? 0x04 : 0) |
                (state->lstick ? 0x08 : 0) |
                (state->home ? 0x10 : 0) |
                (state->capture ? 0x20 : 0);

    // Byte 5: left-side buttons
    report[5] = (state->down ? 0x01 : 0) |
                (state->up ? 0x02 : 0) |
                (state->right ? 0x04 : 0) |
                (state->left ? 0x08 : 0) |
                (state->sr_l ? 0x10 : 0) |
                (state->sl_l ? 0x20 : 0) |
                (state->l ? 0x40 : 0) |
                (state->zl ? 0x80 : 0);

    // Left stick (12-bit X, 12-bit Y packed into 3 bytes little-endian)
    uint16_t lx = state->left_x & 0x0FFF;
    uint16_t ly = state->left_y & 0x0FFF;
    report[6] = lx & 0xFF;
    report[7] = ((lx >> 8) & 0x0F) | ((ly & 0x0F) << 4);
    report[8] = (ly >> 4) & 0xFF;

    // Right stick (same packing)
    uint16_t rx = state->right_x & 0x0FFF;
    uint16_t ry = state->right_y & 0x0FFF;
    report[9] = rx & 0xFF;
    report[10] = ((rx >> 8) & 0x0F) | ((ry & 0x0F) << 4);
    report[11] = (ry >> 4) & 0xFF;

    // Byte 12: vibrator input report (real controllers send 0x80+)
    report[12] = 0x80;

    // Bytes 13-48: IMU data — 3 frames, each 12 bytes (accel XYZ + gyro XYZ, Int16LE)
    // Stationary controller: accel_z ~= 4096 (1G), everything else ~= 0
    // This prevents divide-by-zero in the Switch's orientation math.
    // Frame layout: accel_x(2) accel_y(2) accel_z(2) gyro_x(2) gyro_y(2) gyro_z(2)
    for(int frame = 0; frame < 3; frame++) {
        int off = 13 + frame * 12;
        // accel_x = 0 (bytes off+0, off+1) — already zero
        // accel_y = 0 (bytes off+2, off+3) — already zero
        // accel_z = 4096 = 0x1000 (1G in raw units, Int16LE)
        report[off + 4] = 0x00;
        report[off + 5] = 0x10;
        // gyro_x,y,z = 0 — already zero
    }
    // Bytes 49-63: padding (already zeroed by memset)
}

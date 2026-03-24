#pragma once

#include <gui/view.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t handshake_stage;
    uint8_t last_subcmd;
    uint8_t queue_depth;
    uint8_t spi_read_count;
    uint32_t spi_addrs[4];
    uint8_t subcmd_log[8];
    uint8_t subcmd_log_count;
    uint16_t subcmd_mask;
    uint32_t last_spi_addr;
    uint16_t rx_count;
    uint16_t tx_count;
    uint16_t report_count;
    uint16_t dropped_replies;
    bool handshake_complete;
    bool input_mode_set;
    bool imu_enabled;
    bool usb_connected;    /* handshake complete */
    bool bus_connected;    /* SET_CONFIGURATION received */
    bool init_called;      /* HAL called our init */
    bool ep_configured;    /* endpoints configured */
} UsbDebugModel;

View* usb_debug_view_alloc(void);
void usb_debug_view_free(View* view);

#pragma once

#include <furi.h>
#include <furi_hal_usb.h>
#include "switch_pro_report.h"

/**
 * Custom FuriHalUsbInterface that makes the Flipper Zero appear
 * as a Nintendo Switch Pro Controller (VID 0x057E, PID 0x2009).
 */
extern FuriHalUsbInterface usb_switch_pro;

/**
 * Start the Switch Pro Controller USB interface.
 * Saves the current USB config and switches to Pro Controller mode.
 * Call switch_pro_usb_stop() to restore the previous config.
 */
bool switch_pro_usb_start(void);

/**
 * Stop the Switch Pro Controller USB interface.
 * Stops the report timer and restores the previous USB configuration.
 */
void switch_pro_usb_stop(void);

/**
 * Thread-safe update of the current button state.
 * Called from the main app thread; the FuriTimer callback reads this
 * state to build 0x30 input reports at 125Hz.
 *
 * @param state  Pointer to the desired button state (copied internally).
 */
void switch_pro_usb_set_button_state(const SwitchButtonState* state);

/**
 * Returns true if the Switch has completed the 0x80 handshake
 * and the controller is actively sending input reports.
 */
bool switch_pro_usb_is_connected(void);

/** Returns handshake stage: 0=none, 1-4=0x80 commands received */
uint8_t switch_pro_usb_get_handshake_stage(void);

/** Debug: get last RX bytes and counters */
void switch_pro_usb_get_debug(uint8_t* rx0, uint8_t* rx1, uint16_t* rxn, uint16_t* txn);

/** Debug: number of 0x30 standard reports sent by timer */
uint16_t switch_pro_usb_get_report_count(void);

/** Debug: bitmask of unique subcmds seen (bit0=0x02..bit9=0x48) */
uint16_t switch_pro_usb_get_subcmd_mask(void);

/** Debug: last SPI flash read address */
uint32_t switch_pro_usb_get_last_spi_addr(void);

/** Debug: whether subcmd 0x03 set input mode */
bool switch_pro_usb_get_input_mode_set(void);

/** Debug: USB bus connected (SET_CONFIGURATION received from host) */
bool switch_pro_usb_get_bus_connected(void);

/** Debug: init callback was called by HAL */
bool switch_pro_usb_get_init_called(void);

/** Debug: endpoint config callback fired (cfg=1) */
bool switch_pro_usb_get_ep_configured(void);

/** Debug: number of subcmd replies dropped due to full queue */
uint16_t switch_pro_usb_get_dropped_replies(void);

/** Debug: current reply queue depth */
uint8_t switch_pro_usb_get_queue_depth(void);

/** Debug: whether IMU has been enabled via subcmd 0x40 */
bool switch_pro_usb_get_imu_enabled(void);

/** Debug: number of SPI flash reads received */
uint8_t switch_pro_usb_get_spi_read_count(void);

/** Debug: get first N SPI read addresses */
void switch_pro_usb_get_spi_addrs(uint32_t* out, uint8_t max);

/** Debug: get last N subcmd IDs (up to 8) */
void switch_pro_usb_get_subcmd_log(uint8_t* out, uint8_t* count);

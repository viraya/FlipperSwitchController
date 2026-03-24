/**
 * USB Debug scene: Shows live USB Pro Controller debug info.
 * Auto-refreshes at 2Hz. Back button returns to menu.
 */

#include "../switch_controller_i.h"
#include "../views/usb_debug_view.h"

#define TAG "SceneUsbDebug"
#define USB_DEBUG_REFRESH_MS 500

static FuriTimer* debug_refresh_timer = NULL;

static void debug_refresh_callback(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    uint8_t rx0, rx1;
    uint16_t rxn, txn;
    switch_pro_usb_get_debug(&rx0, &rx1, &rxn, &txn);

    with_view_model(
        app->usb_debug_view,
        UsbDebugModel * m,
        {
            m->handshake_stage = switch_pro_usb_get_handshake_stage();
            m->last_subcmd = rx1;
            m->subcmd_mask = switch_pro_usb_get_subcmd_mask();
            m->last_spi_addr = switch_pro_usb_get_last_spi_addr();
            m->rx_count = rxn;
            m->tx_count = txn;
            m->report_count = switch_pro_usb_get_report_count();
            m->dropped_replies = switch_pro_usb_get_dropped_replies();
            m->queue_depth = switch_pro_usb_get_queue_depth();
            m->spi_read_count = switch_pro_usb_get_spi_read_count();
            switch_pro_usb_get_spi_addrs(m->spi_addrs, 4);
            switch_pro_usb_get_subcmd_log(m->subcmd_log, &m->subcmd_log_count);
            m->handshake_complete = switch_pro_usb_is_connected();
            m->input_mode_set = switch_pro_usb_get_input_mode_set();
            m->imu_enabled = switch_pro_usb_get_imu_enabled();
            m->usb_connected = switch_pro_usb_is_connected();
            m->bus_connected = switch_pro_usb_get_bus_connected();
            m->init_called = switch_pro_usb_get_init_called();
            m->ep_configured = switch_pro_usb_get_ep_configured();
        },
        true);
}

void scene_usb_debug_on_enter(void* context) {
    SwitchControllerApp* app = (SwitchControllerApp*)context;

    /* Start USB if not already started */
    if(!app->usb_started) {
        app->usb_started = switch_pro_usb_start();
        FURI_LOG_I(TAG, "USB start: %s", app->usb_started ? "OK" : "FAIL");
    }

    /* Initial update */
    debug_refresh_callback(app);

    /* Start refresh timer at 2Hz */
    debug_refresh_timer = furi_timer_alloc(
        debug_refresh_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(debug_refresh_timer, furi_ms_to_ticks(USB_DEBUG_REFRESH_MS));

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewUsbDebug);
    FURI_LOG_I(TAG, "USB debug scene entered");
}

bool scene_usb_debug_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);

    if(event.type == SceneManagerEventTypeBack) {
        return false; /* Let scene manager handle back navigation */
    }
    return false;
}

void scene_usb_debug_on_exit(void* context) {
    UNUSED(context);

    if(debug_refresh_timer) {
        furi_timer_stop(debug_refresh_timer);
        furi_timer_free(debug_refresh_timer);
        debug_refresh_timer = NULL;
    }

    FURI_LOG_I(TAG, "USB debug scene exited");
}

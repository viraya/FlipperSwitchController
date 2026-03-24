#include "usb_debug_view.h"
#include "../usb/switch_pro_usb.h"
#include <gui/canvas.h>
#include <stdio.h>

static void usb_debug_draw(Canvas* canvas, void* model) {
    UsbDebugModel* m = (UsbDebugModel*)model;
    char buf[48];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "USB Pro Controller Debug");
    canvas_draw_line(canvas, 0, 12, 128, 12);

    canvas_set_font(canvas, FontSecondary);

    /* Row 1: USB pipeline state + handshake */
    snprintf(buf, sizeof(buf), "I:%s EP:%s Bus:%s HS:%d %s",
        m->init_called ? "Y" : "N",
        m->ep_configured ? "Y" : "N",
        m->bus_connected ? "Y" : "N",
        m->handshake_stage,
        m->handshake_complete ? "OK" : "");
    canvas_draw_str(canvas, 2, 22, buf);

    /* Row 2: Counters + queue + SPI reads */
    snprintf(buf, sizeof(buf), "RX:%u TX:%u Q:%u D:%u S:%u",
        m->rx_count, m->tx_count, m->queue_depth,
        m->dropped_replies, m->spi_read_count);
    canvas_draw_str(canvas, 2, 31, buf);

    /* Row 3: Reports + IMU state */
    snprintf(buf, sizeof(buf), "Rpt:%u IMU:%s SPI:0x%04lX",
        m->report_count,
        m->imu_enabled ? "Y" : "N",
        (unsigned long)m->last_spi_addr);
    canvas_draw_str(canvas, 2, 40, buf);

    /* Row 4: Subcmd sequence log (last 8 received, in order) */
    {
        int pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "Seq:");
        for(uint8_t i = 0; i < m->subcmd_log_count && i < 8; i++) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, " %02X", m->subcmd_log[i]);
        }
        canvas_draw_str(canvas, 2, 49, buf);
    }

    /* Row 5: Missing critical subcmds */
    snprintf(buf, sizeof(buf), "Miss:%s%s%s%s",
        (m->subcmd_mask & 0x100) ? "" : " IMU",
        (m->subcmd_mask & 0x200) ? "" : " Vib",
        (m->subcmd_mask & 0x040) ? "" : " LED",
        (m->subcmd_mask & 0x001) ? "" : " Dev");
    canvas_draw_str(canvas, 2, 58, buf);
}

static bool usb_debug_input(InputEvent* event, void* context) {
    UNUSED(context);
    /* Map Flipper buttons to Switch Pro Controller buttons for testing */
    SwitchButtonState state;
    switch_pro_report_init(&state);

    if(event->type == InputTypePress || event->type == InputTypeLong) {
        switch(event->key) {
        case InputKeyOk:
            /* Send L+R to register on Change Grip/Order screen */
            state.l = true;
            state.r = true;
            break;
        case InputKeyUp:
            state.up = true;
            break;
        case InputKeyDown:
            state.down = true;
            break;
        case InputKeyLeft:
            state.left = true;
            break;
        case InputKeyRight:
            state.right = true;
            break;
        case InputKeyBack:
            return false;
        default:
            break;
        }
        switch_pro_usb_set_button_state(&state);
        return true;
    } else if(event->type == InputTypeRelease) {
        switch_pro_usb_set_button_state(&state);
        if(event->key == InputKeyBack) return false;
        return true;
    }

    return false;
}

View* usb_debug_view_alloc(void) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLockFree, sizeof(UsbDebugModel));
    view_set_draw_callback(view, usb_debug_draw);
    view_set_input_callback(view, usb_debug_input);
    return view;
}

void usb_debug_view_free(View* view) {
    if(view) view_free(view);
}

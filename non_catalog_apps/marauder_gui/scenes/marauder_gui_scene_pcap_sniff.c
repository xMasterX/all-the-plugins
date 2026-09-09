#include "../marauder_gui_app_i.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs a Marauder sniff/wardrive command with "-serial" so the ESP32 streams raw PCAP back over
   the wire, and writes those bytes straight into a sequential .pcap file on the Flipper SD card.
   The actual byte draining/writing happens in the shared app tick (marauder_gui.c) whenever
   app->is_writing_pcap is set; this scene opens/closes the file, shows a live byte count, and
   echoes Marauder's latest text line so it's obvious the command is actually running.

   For the "targeted" PMKID modes (app->pcap_ap_scoped) the flow scanned and selected an AP first;
   here we "select -a" it so Marauder's "-l" (run on selected list) has a target, then toggle that
   selection back off on exit.

   We also append "-c <channel of that AP>". sniffpmkid tracks a single global channel
   (wifi_scan_obj.set_channel) and "-l" only filters WHICH APs are targeted - it does not follow
   their channel. Without "-c" the sniff runs on whatever channel some earlier command happened to
   leave set (which is also exactly what the "Starting TARGETED PMKID sniff ... on channel N"
   line reports), so a target on ch 8 would be sniffed on, say, ch 132 and capture nothing. */

/* AP lines look like "[N][CH:6] ssid rssi" - same parser wifi_attack.c uses. */
static int marauder_gui_scene_pcap_sniff_parse_channel(const char* line) {
    const char* p = strstr(line, "CH:");
    if(!p) return -1;
    return (int)strtol(p + 3, NULL, 10);
}

static void marauder_gui_scene_pcap_sniff_redraw(MarauderGuiApp* app) {
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 0, AlignCenter, AlignTop, FontPrimary, app->pcap_sniff_label);

    if(app->is_writing_pcap) {
        widget_add_string_element(
            app->widget, 64, 13, AlignCenter, AlignTop, FontSecondary, app->pcap_save_name);

        char bytes[32];
        snprintf(
            bytes,
            sizeof(bytes),
            marauder_gui_text(app, "%lu bayt - Geri: Durdur", "%lu bytes - Back: Stop"),
            (unsigned long)app->pcap_bytes);
        widget_add_string_element(
            app->widget, 64, 23, AlignCenter, AlignTop, FontSecondary, bytes);
    } else {
        widget_add_string_element(
            app->widget,
            64,
            18,
            AlignCenter,
            AlignTop,
            FontSecondary,
            marauder_gui_text(app, "SD dosyasi acilamadi", "Cannot open SD file"));
    }

    /* Marauder's latest serial line in a wrapping/scrollable box so long lines (e.g. a deauth
       frame's two MAC addresses) don't run off the 128px edge - or a placeholder so the screen is
       never blank while we wait for the first line to arrive. */
    widget_add_text_scroll_element(
        app->widget,
        0,
        35,
        128,
        29,
        app->last_uart_line[0] ? app->last_uart_line :
                                 marauder_gui_text(app, "Dinleniyor...", "Listening..."));
}

/* Marauder still prints textual status/notification lines (outside the pcap markers) even while
   streaming packets - keep the most recent one for the display. */
static void marauder_gui_scene_pcap_sniff_uart_line(MarauderGuiApp* app, const char* line) {
    strncpy(app->last_uart_line, line, sizeof(app->last_uart_line) - 1);
    app->last_uart_line[sizeof(app->last_uart_line) - 1] = '\0';
}

static void marauder_gui_scene_pcap_sniff_tick(MarauderGuiApp* app) {
    /* Byte count is bumped by the shared tick's pcap drain; just refresh the display. */
    marauder_gui_scene_pcap_sniff_redraw(app);
}

void marauder_gui_scene_pcap_sniff_on_enter(void* context) {
    MarauderGuiApp* app = context;

    app->pcap_bytes = 0;
    app->is_writing_pcap = false;
    app->pcap_save_name[0] = '\0';
    app->last_uart_line[0] = '\0';

    /* Start from a clean demux state (and drop any stale buffered bytes from a previous scene). */
    marauder_uart_reset_capture(app->uart);

    /* Make sure the target folders exist (mkdir is a no-op if they already do). */
    storage_common_mkdir(app->storage, MARAUDER_GUI_DATA_DIR);
    storage_common_mkdir(app->storage, MARAUDER_GUI_PCAP_DIR);

    char* path = sequential_file_resolve_path(
        app->storage, MARAUDER_GUI_PCAP_DIR, app->pcap_sniff_prefix, "pcap");
    if(path != NULL) {
        const char* slash = strrchr(path, '/');
        const char* base = slash ? slash + 1 : path;
        strncpy(app->pcap_save_name, base, sizeof(app->pcap_save_name) - 1);
        app->pcap_save_name[sizeof(app->pcap_save_name) - 1] = '\0';

        if(storage_file_open(app->capture_file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            app->is_writing_pcap = true;
        }
        free(path);
    }

    /* Targeted ("-l") capture: select the picked AP so Marauder's selected-AP list has a target. */
    if(app->pcap_ap_scoped && app->selected_ap_index >= 0 &&
       (size_t)app->selected_ap_index < app->ap_count) {
        /* Show the target AP until Marauder's own lines start arriving. */
        strncpy(
            app->last_uart_line,
            app->ap_list[app->selected_ap_index],
            sizeof(app->last_uart_line) - 1);
        app->last_uart_line[sizeof(app->last_uart_line) - 1] = '\0';

        char cmd[32];
        snprintf(cmd, sizeof(cmd), "select -a %d", app->selected_ap_index);
        marauder_uart_send_line(app->uart, cmd);

        /* Pin the sniff to the target's channel (see the note at the top of this file). */
        int channel =
            marauder_gui_scene_pcap_sniff_parse_channel(app->ap_list[app->selected_ap_index]);
        if(channel > 0) {
            char built[sizeof(app->pcap_cmd_buf)];
            snprintf(built, sizeof(built), "%s -c %d", app->pcap_sniff_cmd, channel);
            snprintf(app->pcap_cmd_buf, sizeof(app->pcap_cmd_buf), "%s", built);
            app->pcap_sniff_cmd = app->pcap_cmd_buf;
        }
    }

    app->uart_line_handler = marauder_gui_scene_pcap_sniff_uart_line;
    app->tick_handler = marauder_gui_scene_pcap_sniff_tick;

    marauder_gui_scene_pcap_sniff_redraw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MarauderGuiViewWidget);

    /* "<cmd> -serial\n" - the -serial flag is what makes Marauder stream PCAP over the wire. */
    marauder_uart_send(app->uart, app->pcap_sniff_cmd);
    marauder_uart_send_line(app->uart, " -serial");
}

bool marauder_gui_scene_pcap_sniff_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void marauder_gui_scene_pcap_sniff_on_exit(void* context) {
    MarauderGuiApp* app = context;

    marauder_uart_send_line(app->uart, "stopscan");

    /* Undo the AP selection so it doesn't leak into the next flow ("select" toggles), matching
       wifi_attack.c's cleanup.

       pcap_ap_scoped is deliberately NOT cleared here: Back from this scene returns to the AP
       scan, not to the PMKID menu, so the user can pick another AP and come straight back in.
       Clearing it would skip the "select -a" above on that second run and Marauder would answer
       "You don't have any targets selected". Every path into this scene sets the flag itself
       (the broadcast rows and the channel picker set it false), so it can't leak. */
    if(app->pcap_ap_scoped && app->selected_ap_index >= 0 &&
       (size_t)app->selected_ap_index < app->ap_count) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "select -a %d", app->selected_ap_index);
        marauder_uart_send_line(app->uart, cmd);
    }

    /* Flush any last pcap bytes still queued, then close the file. */
    app->is_writing_pcap = false;
    if(storage_file_is_open(app->capture_file)) {
        uint8_t buf[128];
        size_t n;
        while((n = marauder_uart_receive_pcap(app->uart, buf, sizeof(buf))) > 0) {
            storage_file_write(app->capture_file, buf, n);
        }
        storage_file_close(app->capture_file);
    }

    /* Clear the demux so a missed [BUF/CLOSE] can't leave it stuck in pcap mode and swallow the
       next scene's text output. */
    marauder_uart_reset_capture(app->uart);

    app->uart_line_handler = NULL;
    app->tick_handler = NULL;
    widget_reset(app->widget);
}

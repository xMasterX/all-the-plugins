#include "../marauder_gui_app_i.h"

/* Which Marauder sniff/capture command each menu row runs. The capture scene appends "-serial"
   itself so Marauder streams the raw PCAP over the wire (see marauder_gui_scene_pcap_sniff.c).
   Order must match marauder_pcap_sniff_menu_items below - the menu fires the row index as its
   custom event.

   Everything here is a broadcast capture that starts straight away; only PMKID takes extra setup
   (passive / active / targeted / on-channel), so it opens its own options menu instead. Note
   "sniffdeauth" is a passive sniffer that ignores AP selection and channel entirely (see
   CommandLine.cpp's SNIFF_DEAUTH_CMD handler), so there is nothing to pick for it either. */
typedef struct {
    const char* cmd; /* NULL = row opens the PMKID options submenu instead of capturing */
    const char* prefix; /* .pcap filename prefix */
} MarauderPcapSniffType;

static const MarauderPcapSniffType marauder_pcap_sniff_types[] = {
    {"sniffraw", "sniffraw"},
    {"sniffbeacon", "sniffbeacon"},
    {"sniffdeauth", "sniffdeauth"},
    {"sniffprobe", "sniffprobe"},
    {NULL, "sniffpmkid"},
    {"sniffpwn", "sniffpwn"},
    {"wardrive", "wardrive"},
};

static const MarauderMenuItem marauder_pcap_sniff_menu_items[] = {
    {"Raw PCAP",
     "Raw PCAP",
     "Tum 802.11 cercevelerini yakalar ve SD karta .pcap olarak kaydeder. En cok veri bu modda gelir.",
     "Captures all 802.11 frames to SD as .pcap. This is the mode that yields the most data."},
    {"Beacon PCAP",
     "Beacon PCAP",
     "Beacon cercevelerini yakalayip .pcap olarak SD karta kaydeder.",
     "Captures beacon frames and saves them to the SD card as .pcap."},
    {"Deauth PCAP",
     "Deauth PCAP",
     "Havadaki deauth cercevelerini pasif olarak yakalar. Kimse deauth atmiyorsa dosya bos kalir.",
     "Passively captures deauth frames in the air. If nobody is deauthing, the file stays empty."},
    {"Probe PCAP",
     "Probe PCAP",
     "Probe request cercevelerini yakalayip .pcap olarak kaydeder.",
     "Captures probe request frames and saves them as .pcap."},
    {"PMKID PCAP>",
     "PMKID PCAP>",
     "WPA2 el sikismasi/PMKID yakalama - pasif, aktif (deauth), hedefli veya kanal secenekleriyle.",
     "WPA2 handshake/PMKID capture - with passive, active (deauth), targeted or channel options."},
    {"Pwnagotchi PCAP",
     "Pwnagotchi PCAP",
     "Pwnagotchi yayinlarini yakalayip .pcap olarak kaydeder.",
     "Captures Pwnagotchi broadcasts and saves them as .pcap."},
    {"Wardrive",
     "Wardrive",
     "Kanallari gezerek AP'leri (GPS varsa konumla) .pcap olarak SD karta kaydeder.",
     "Sweeps channels logging APs (with GPS location if present) to .pcap on the SD card."},
};

void marauder_gui_scene_pcap_sniff_menu_on_enter(void* context) {
    MarauderGuiApp* app = context;
    marauder_gui_menu_set_items(
        app,
        marauder_pcap_sniff_menu_items,
        sizeof(marauder_pcap_sniff_menu_items) / sizeof(marauder_pcap_sniff_menu_items[0]),
        marauder_gui_text(app, "PCAP Kayit", "PCAP Capture"));
}

bool marauder_gui_scene_pcap_sniff_menu_on_event(void* context, SceneManagerEvent event) {
    MarauderGuiApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        size_t count = sizeof(marauder_pcap_sniff_types) / sizeof(marauder_pcap_sniff_types[0]);
        if(event.event < count) {
            const MarauderPcapSniffType* type = &marauder_pcap_sniff_types[event.event];
            if(type->cmd == NULL) {
                scene_manager_next_scene(app->scene_manager, MarauderGuiScenePcapPmkidMenu);
            } else {
                app->pcap_sniff_cmd = type->cmd;
                app->pcap_sniff_prefix = type->prefix;
                app->pcap_sniff_label = marauder_gui_menu_item_label(
                    app, &marauder_pcap_sniff_menu_items[event.event]);
                app->pcap_ap_scoped = false;
                scene_manager_next_scene(app->scene_manager, MarauderGuiScenePcapSniff);
            }
            consumed = true;
        }
    }

    return consumed;
}

void marauder_gui_scene_pcap_sniff_menu_on_exit(void* context) {
    MarauderGuiApp* app = context;
    app->tick_handler = NULL;
}

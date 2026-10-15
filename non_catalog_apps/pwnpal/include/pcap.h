#pragma once

#include <storage/storage.h>
#include <stdint.h>
#include <stdbool.h>

// handshake pcaps, one file per target (<ssid_or_bssid>.pcap)
#define PWNPAL_HS_DIR "/ext/apps_data/pwnpal/handshakes"

// bare 802.11, no radiotap; byte-for-byte Marauder's pcap so aircrack/hcxpcapngtool/tshark read it
#define PCAP_LINKTYPE_IEEE802_11 105
#define PCAP_SNAPLEN 4096

// append one raw 802.11 frame to <dir>/<name>.pcap (global header first if empty).
// open-append-close per frame so a yank mid-capture leaves a valid pcap. false on error.
bool pcap_append_frame(Storage* storage, const char* name, const uint8_t* frame, uint16_t len);

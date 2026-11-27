#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded, passive WPA2 M1/M2 candidate detection. This is not a MIC/password
   verification, nor a PMKID parser. All state is reset at the start of a file. */
#define MARAUDER_PCAP_PAIR_SLOTS 8
#define MARAUDER_PCAP_PAIR_WINDOW_US 10000000ULL

typedef struct {
    bool valid;
    uint8_t ap[6];
    uint8_t station[6];
    uint8_t replay[8];
    uint8_t nonce[32];
    uint64_t time_us;
} MarauderPcapPairSlot;

typedef struct {
    MarauderPcapPairSlot slots[MARAUDER_PCAP_PAIR_SLOTS];
    size_t next;
} MarauderPcapPairTracker;

/* frame_len is the captured record length; len is the available prefix. Return
   true only for an M2 matching a recent M1 for the same AP, client and replay.
   ap_out is filled only on success. Unsupported/malformed packets return false. */
bool marauder_pcap_pair_feed(
    MarauderPcapPairTracker* tracker,
    const uint8_t* data,
    size_t len,
    size_t frame_len,
    uint64_t time_us,
    uint8_t ap_out[6]);

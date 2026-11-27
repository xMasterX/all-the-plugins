#include "marauder_gui_pcap_pair.h"
#include <string.h>

static bool nonzero(const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        if(data[i]) return true;
    }
    return false;
}

static uint16_t be16(const uint8_t* data) {
    return ((uint16_t)data[0] << 8) | data[1];
}

bool marauder_pcap_pair_feed(
    MarauderPcapPairTracker* tracker,
    const uint8_t* data,
    size_t len,
    size_t frame_len,
    uint64_t time_us,
    uint8_t ap_out[6]) {
    if(len < 24 || (data[0] & 0x0C) != 0x08) return false;
    const uint8_t ds = data[1] & 3;
    /* Only unencrypted, unfragmented infrastructure data. Null-data subtypes,
       WDS/IBSS and QoS A-MSDU need different parsing and are not supported. */
    if(ds != 1 && ds != 2) return false;
    if((data[1] & 0x44) || (data[22] & 0x0F) || (data[0] & 0x40)) return false;
    const bool qos = (data[0] & 0x80) != 0;
    size_t header = qos ? 26 : 24;
    if(qos) {
        if(len < header || (data[24] & 0x80)) return false;
        if(data[1] & 0x80) header += 4; /* HT Control */
    }
    static const uint8_t snap[] = {0xAA, 0xAA, 3, 0, 0, 0, 0x88, 0x8E};
    if(len < header + sizeof(snap) + 99 ||
       memcmp(data + header, snap, sizeof(snap)) != 0)
        return false;
    const uint8_t* eapol = data + header + sizeof(snap);
    const uint16_t body_len = be16(eapol + 2);
    if(eapol[1] != 3 || body_len < 95 ||
       header + sizeof(snap) + 4 + body_len > frame_len)
        return false;
    const uint8_t* key = eapol + 4;
    const uint16_t info = be16(key + 1);
    /* RSN descriptor, HMAC-SHA1/AES WPA2 pairwise keys only. Reject request,
       error, install, secure, encrypted-key-data and reserved flags for M1/M2. */
    if(key[0] != 2 || (info & 7) != 2 || !(info & 8) || (info & 0xFE70)) return false;
    if(be16(key + 93) != body_len - 95) return false;
    const bool ack = (info & 0x80) != 0;
    const bool mic = (info & 0x100) != 0;
    const uint8_t* ap = data + (ds == 2 ? 10 : 4);
    const uint8_t* station = data + (ds == 2 ? 4 : 10);
    if((ap[0] & 1) || (station[0] & 1) || !nonzero(ap, 6) ||
       !nonzero(station, 6) || memcmp(ap, station, 6) == 0)
        return false;
    if(!nonzero(key + 13, 32)) return false;

    if(ack && !mic && ds == 2 && !nonzero(key + 77, 16)) {
        /* Retransmissions refresh the matching slot instead of evicting peers. */
        size_t index = MARAUDER_PCAP_PAIR_SLOTS;
        for(size_t i = 0; i < MARAUDER_PCAP_PAIR_SLOTS; i++) {
            MarauderPcapPairSlot* s = &tracker->slots[i];
            if(s->valid && memcmp(s->ap, ap, 6) == 0 &&
               memcmp(s->station, station, 6) == 0 && memcmp(s->replay, key + 5, 8) == 0 &&
               memcmp(s->nonce, key + 13, 32) == 0) {
                index = i;
                break;
            }
        }
        if(index == MARAUDER_PCAP_PAIR_SLOTS) {
            index = tracker->next;
            tracker->next = (tracker->next + 1) % MARAUDER_PCAP_PAIR_SLOTS;
        }
        MarauderPcapPairSlot* s = &tracker->slots[index];
        s->valid = true;
        memcpy(s->ap, ap, 6);
        memcpy(s->station, station, 6);
        memcpy(s->replay, key + 5, 8);
        memcpy(s->nonce, key + 13, 32);
        s->time_us = time_us;
    } else if(!ack && mic && ds == 1 && nonzero(key + 77, 16)) {
        for(size_t i = 0; i < MARAUDER_PCAP_PAIR_SLOTS; i++) {
            const MarauderPcapPairSlot* s = &tracker->slots[i];
            if(s->valid && time_us >= s->time_us &&
               time_us - s->time_us <= MARAUDER_PCAP_PAIR_WINDOW_US &&
               memcmp(s->ap, ap, 6) == 0 && memcmp(s->station, station, 6) == 0 &&
               memcmp(s->replay, key + 5, 8) == 0 &&
               memcmp(s->nonce, key + 13, 32) != 0) {
                memcpy(ap_out, ap, 6);
                return true;
            }
        }
    }
    return false;
}

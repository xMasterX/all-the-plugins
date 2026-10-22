#include "../include/peers.h"

#include <string.h>

void peers_init(PeerList* list) {
    memset(list, 0, sizeof(PeerList));
}

static Peer* peers_find(PeerList* list, const char* key_name, const char* key_id) {
    // Prefer matching on identity (stable); fall back to name.
    bool have_id = key_id && key_id[0] != '\0';
    for(int i = 0; i < MAX_PEERS; i++) {
        if(!list->items[i].used) continue;
        if(have_id && list->items[i].identity[0] != '\0') {
            if(strcmp(list->items[i].identity, key_id) == 0) return &list->items[i];
        } else if(strcmp(list->items[i].name, key_name) == 0) {
            return &list->items[i];
        }
    }
    return NULL;
}

static Peer* peers_alloc_slot(PeerList* list, uint32_t now) {
    for(int i = 0; i < MAX_PEERS; i++) {
        if(!list->items[i].used) return &list->items[i];
    }
    // Full: evict the one we've heard from least recently.
    Peer* oldest = &list->items[0];
    for(int i = 1; i < MAX_PEERS; i++) {
        if(list->items[i].last_seen < oldest->last_seen) oldest = &list->items[i];
    }
    UNUSED(now);
    return oldest;
}

bool peers_update(
    PeerList* list,
    const char* name,
    const char* identity,
    int pwnd_tot,
    int rssi,
    int channel,
    uint32_t now) {
    Peer* p = peers_find(list, name, identity);
    bool is_new = false;

    if(p == NULL) {
        p = peers_alloc_slot(list, now);
        memset(p, 0, sizeof(Peer));
        p->used = true;
        p->first_seen = now;
        is_new = true;
    }

    strncpy(p->name, (name && name[0]) ? name : "???", PEER_NAME_MAX - 1);
    p->name[PEER_NAME_MAX - 1] = '\0';
    if(identity) {
        strncpy(p->identity, identity, PEER_ID_MAX - 1);
        p->identity[PEER_ID_MAX - 1] = '\0';
    }
    p->pwnd_tot = pwnd_tot;
    p->rssi = rssi;
    p->channel = channel;
    p->last_seen = now;
    return is_new;
}

uint32_t peers_prune(PeerList* list, uint32_t now) {
    uint32_t active = 0;
    for(int i = 0; i < MAX_PEERS; i++) {
        if(!list->items[i].used) continue;
        if(now - list->items[i].last_seen > PEER_TTL_SECS) {
            list->items[i].used = false;
        } else {
            active++;
        }
    }
    return active;
}

bool peers_any_bonded(const PeerList* list, uint32_t now) {
    for(int i = 0; i < MAX_PEERS; i++) {
        if(!list->items[i].used) continue;
        if(now - list->items[i].first_seen >= PEER_BONDED_AFTER_SECS) return true;
    }
    return false;
}

int peers_rssi_bars(int rssi) {
    if(rssi >= -67) return 4;
    if(rssi >= -70) return 3;
    if(rssi >= -80) return 2;
    return 1;
}

#pragma once

#include <furi.h>

// pwnagotchis this friend can currently hear (ESP32 PWNPAL_PEER lines).
#define PEER_NAME_MAX 17
#define PEER_ID_MAX 65
#define MAX_PEERS 8

// heard this long -> a lingering buddy, not a passer-by; drives the bonded mood
#define PEER_BONDED_AFTER_SECS 60
// generous TTL: a peer's only heard when our hop lands on its channel, so during an
// attack dwell we're off-channel for long stretches — a shorter TTL blinked friends out
#define PEER_TTL_SECS 180

typedef struct {
    bool used;
    char name[PEER_NAME_MAX];
    char identity[PEER_ID_MAX];
    int pwnd_tot;
    int rssi;
    int channel;
    uint32_t first_seen; // app secs tick, first heard this session
    uint32_t last_seen; // app secs tick, last heard
} Peer;

typedef struct {
    Peer items[MAX_PEERS];
} PeerList;

void peers_init(PeerList* list);

// Record a sighting. Returns true if this is a peer we hadn't seen this session.
// `now` is the app's seconds tick.
bool peers_update(
    PeerList* list,
    const char* name,
    const char* identity,
    int pwnd_tot,
    int rssi,
    int channel,
    uint32_t now);

// Drop stale peers. Returns the number still active.
uint32_t peers_prune(PeerList* list, uint32_t now);

// True if any active peer has lingered long enough to count as a good friend.
bool peers_any_bonded(const PeerList* list, uint32_t now);

// Signal bars 0..4 from an rssi, matching pwnagotchi's thresholds.
int peers_rssi_bars(int rssi);

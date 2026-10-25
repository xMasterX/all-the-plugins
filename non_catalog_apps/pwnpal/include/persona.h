#pragma once

#include <furi.h>
#include "face.h"

// the friend's identity and how it grows; persisted to SD so a stable pwngrid identity
// lets the pwnagotchi keep counting encounters and eventually treat it as a good friend
#define PERSONA_NAME_MAX   17
#define PERSONA_ID_HEX_LEN 64

typedef enum {
    // The original five ordinals are kept stable so old logic still lines up.
    MoodLonely, // stale: attacked APs but caught nothing (pwnagotchi's on_lonely)
    MoodContent, // awake / idling happily (AWAKE face)
    MoodCurious, // a familiar unit just dropped by
    MoodExcited, // sustained activity, or a fresh capture streak / new friend
    MoodBonded, // a good friend is nearby (♥ FRIEND face)
    // --- new moods for the full pwnagotchi machine ---
    MoodBored, // inactive_for >= bored_num_epochs (BORED face)
    MoodSleep, // inactive_for >= 2*sad_num_epochs, drifting off (SLEEP face)
    MoodSad, // inactive_for >= sad_num_epochs (SAD face)
    MoodMotivated, // actively racking up APs this epoch (MOTIVATED face)
    MoodSmart, // a flood of APs this epoch (SMART face)
    MoodHappy, // just grabbed a handshake (HAPPY face)
    MoodCool, // handshake streak this epoch (COOL face)
    // --- appended (ordinals stay stable for the wire/save layout) ---
    MoodGrateful, // a down epoch, but a good friend is around (GRATEFUL face)
    MoodDemotivated, // a deauth/assoc that missed (DEMOTIVATED face)
} PersonaMood;

// persisted-to-SD portion; fixed layout, bump PERSONA_SAVE_VERSION on change. loader
// rejects any blob whose size/version differs, so an old file is cleanly discarded
#define PERSONA_SAVE_MAGIC   0x50574E46u // "PWNF"
#define PERSONA_SAVE_VERSION 3

typedef struct {
    uint32_t magic;
    uint32_t version;
    char name[PERSONA_NAME_MAX];
    char identity[PERSONA_ID_HEX_LEN + 1]; // 64 lowercase hex + NUL
    uint64_t born_unix; // when this persona was first created
    uint64_t total_uptime; // cumulative seconds advertised, all sessions
    uint32_t friends_met; // distinct units met over lifetime (social score, NOT pwnd)
    uint32_t generation; // increments each save; a rough "age"
    // --- v2 additions ---
    uint32_t pwnd_tot; // REAL handshakes/PMKID captured, lifetime (advertised as -pt)
    uint32_t aps_tot; // access points seen, lifetime
    uint32_t epochs_tot; // lifetime epoch count (feeds the level curve)
    // --- v3 additions: recycle-proof capture tallies (table evicts, these don't) ---
    uint32_t crack_tot; // strictly-crackable APs, lifetime (survives AP-table recycling)
    uint32_t pmkid_tot; // captures via PMKID, lifetime
    uint32_t hs_tot; // captures via 4-way handshake, lifetime
} PersonaSaved;

typedef struct {
    PersonaSaved s;

    // Volatile session state, not persisted.
    uint32_t session_uptime; // seconds this session
    uint32_t friends_session; // distinct units met this session
    uint32_t secs_since_peer; // seconds since we last heard any unit
    bool friend_near; // a bonded/good friend is currently in range
    bool hunting; // advertising + capture armed + APs around: engaged, don't decay to sad
    PersonaMood mood;

    // --- full-pwnagotchi brain state (volatile) ---
    uint32_t pwnd_run; // handshakes captured this session (advertised as -pr)
    uint32_t aps_session; // access points seen this session (APS readout)
    uint32_t epoch; // epoch counter this session (advertised as -e)

    uint32_t secs_in_epoch; // seconds accumulated in the current epoch
    uint32_t aps_this_epoch; // APs seen in the current epoch
    uint32_t hs_this_epoch; // handshakes in the current epoch
    uint32_t active_epochs; // consecutive "active" epochs
    uint32_t inactive_epochs; // consecutive "quiet" epochs
    uint32_t quiet_epochs; // consecutive epochs with NO traffic at all (aps/hs/misses)
    uint32_t secs_since_pwnd; // seconds since the last handshake

    uint32_t misses_this_epoch; // deauth/assoc attempts that caught nothing, this epoch
    uint32_t last_epoch_missed; // misses in the just-closed epoch (drives lonely/stale)

    uint32_t mood_lock_secs; // >0: hold a transient reaction face, don't recompute
} Persona;

// Load from SD, or mint a fresh persona (new random identity) if none exists.
Persona* persona_alloc(void);
void persona_free(Persona* p);

// Persist to SD. Call periodically and on exit.
bool persona_save(Persona* p);

// Set the display name (empty -> "flippy"). Persisted on the next persona_save.
void persona_set_name(Persona* p, const char* name);

// Advance one tick. dt = seconds elapsed. Recomputes mood/face.
void persona_tick(Persona* p, uint32_t dt);

// unit heard this tick. is_new if unseen this session, is_bonded if a good friend.
void persona_note_peer(Persona* p, bool is_new, bool is_bonded);

// real WPA handshake/PMKID captured; bumps pwnd counters, flashes face, feeds epochs.
void persona_note_pwnd(Persona* p);

// access point seen; feeds aps_session + epoch activity (MOTIVATED/SMART).
void persona_note_ap(Persona* p);

// deauth/assoc caught nothing (_on_miss): a brief demotivated nudge. optional.
void persona_note_miss(Persona* p);

// Derived getters.
Face persona_face(const Persona* p);
const char* persona_mood_label(const Persona* p);

#include "../include/persona.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h> // offsetof (v2->v3 persona migration)
#include <furi_hal_random.h>
#include <furi_hal_version.h>
#include <storage/storage.h>
#include <datetime/datetime.h>
#include <furi_hal_rtc.h>

#define PERSONA_DIR "/ext/apps_data/pwnpal"
#define PERSONA_PATH PERSONA_DIR "/persona.bin"

// lonely is staleness, not peer-absence: attacked APs this epoch, caught nothing
// (is_stale() == num_missed > max_misses_for_recon). misses past this -> lonely.
#define PERSONA_MAX_MISSES 5 // personality.max_misses_for_recon

// one epoch == one recon window; same wall-time as upstream recon_time
#define PERSONA_EPOCH_SECS 30 // personality.recon_time

// agent.recon() doubles recon_time once inactive_for >= max_inactive_scale; we stretch
// the epoch window the same way, scaling boredom/sadness onset like upstream
#define PERSONA_MAX_INACTIVE_SCALE 2 // personality.max_inactive_scale
#define PERSONA_RECON_INACTIVE_MULT 2 // personality.recon_inactive_multiplier

// per-epoch AP volume as a proxy for upstream's deauth/assoc/handshake activity
#define PERSONA_EPOCH_ACTIVE_APS 4 // >= this -> epoch is "active"
#define PERSONA_SMART_APS 8 // a flood this epoch -> SMART face

// Consecutive-epoch thresholds (real pwnagotchi defaults.toml values).
#define PERSONA_EXCITED_EPOCHS 10 // active_for >= excited_num_epochs -> EXCITED
#define PERSONA_BORED_EPOCHS 15 // inactive_for >= bored_num_epochs -> BORED
#define PERSONA_SAD_EPOCHS 25 // inactive_for >= sad_num_epochs   -> SAD
#define PERSONA_SLEEP_EPOCHS (PERSONA_SAD_EPOCHS * 2) // 2x sad (automata's escalation) -> SLEEP

// Handshake streak inside one epoch that earns the COOL face.
#define PERSONA_COOL_STREAK 4

// silent epochs an engaged unit tolerates before bored/sad is allowed to set in
#define PERSONA_HUNT_PATIENCE 5

// Time-based feels (seconds).
#define PERSONA_CURIOUS_SECS 15 // a peer this recently -> curious
#define PERSONA_PWND_REACT_SECS 8 // hold the capture (happy/cool/excited) face
#define PERSONA_PEER_REACT_SECS 6 // hold the "new friend!" excited face
#define PERSONA_MISS_REACT_SECS 4 // hold the "missed!" demotivated face

static uint64_t persona_now_unix(void) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    return (uint64_t)datetime_datetime_to_timestamp(&dt);
}

static void persona_gen_identity(char* out /* PERSONA_ID_HEX_LEN+1 */) {
    static const char hex[] = "0123456789abcdef";
    uint8_t raw[32];
    furi_hal_random_fill_buf(raw, sizeof(raw));
    for(size_t i = 0; i < sizeof(raw); i++) {
        out[i * 2] = hex[(raw[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[raw[i] & 0xF];
    }
    out[PERSONA_ID_HEX_LEN] = '\0';
}

// mesh display name; user-editable (persona_set_name), separate from the 64-hex identity
#define PERSONA_DEFAULT_NAME "flippy"

void persona_set_name(Persona* p, const char* name) {
    if(!name || !name[0]) name = PERSONA_DEFAULT_NAME;
    strncpy(p->s.name, name, PERSONA_NAME_MAX - 1);
    p->s.name[PERSONA_NAME_MAX - 1] = '\0';
}

static void persona_mint(Persona* p) {
    memset(p, 0, sizeof(Persona));
    p->s.magic = PERSONA_SAVE_MAGIC;
    p->s.version = PERSONA_SAVE_VERSION;
    persona_set_name(p, PERSONA_DEFAULT_NAME);
    persona_gen_identity(p->s.identity);
    p->s.born_unix = persona_now_unix();
    p->s.total_uptime = 0;
    p->s.friends_met = 0;
    p->s.generation = 0;
    // memset above already zeroed v2 counters + every volatile epoch field
    p->mood = MoodContent; // AWAKE on start, like a real pwnagotchi
    p->secs_since_peer = 0; // grace window before it gets lonely
}

Persona* persona_alloc(void) {
    Persona* p = malloc(sizeof(Persona));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool loaded = false;

    if(storage_file_open(file, PERSONA_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        PersonaSaved saved;
        memset(&saved, 0, sizeof(saved)); // so fields beyond a shorter v2 blob stay zero
        uint16_t read = storage_file_read(file, &saved, sizeof(saved));
        // accept v3, or migrate v2. don't rely on the exact size delta (struct padding makes it 8,
        // not 12): require all v2 fields present, then zero the v3 tallies (their bytes in a v2 blob
        // are padding/garbage) and restamp the version.
        bool ok = (saved.magic == PERSONA_SAVE_MAGIC) &&
                  (saved.version == PERSONA_SAVE_VERSION || saved.version == 2) &&
                  (read >= offsetof(PersonaSaved, crack_tot));
        if(ok) {
            if(saved.version != PERSONA_SAVE_VERSION) {
                saved.crack_tot = 0;
                saved.pmkid_tot = 0;
                saved.hs_tot = 0;
                saved.version = PERSONA_SAVE_VERSION;
            }
            memset(p, 0, sizeof(Persona));
            p->s = saved;
            // guard corrupt name/identity from a truncated write
            p->s.name[PERSONA_NAME_MAX - 1] = '\0';
            p->s.identity[PERSONA_ID_HEX_LEN] = '\0';
            if(!p->s.name[0]) persona_set_name(p, PERSONA_DEFAULT_NAME); // guard empty
            // memset above zeroed every volatile epoch counter before p->s = saved
            p->mood = MoodContent;
            p->secs_since_peer = 0;
            loaded = true;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(!loaded) persona_mint(p);
    return p;
}

void persona_free(Persona* p) {
    free(p);
}

bool persona_save(Persona* p) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, PERSONA_DIR);

    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, PERSONA_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        p->s.generation++;
        uint16_t written = storage_file_write(file, &p->s, sizeof(p->s));
        ok = (written == sizeof(p->s));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

// epoch length in seconds; doubles while inactive (mirrors recon_time doubling)
static uint32_t persona_epoch_len(const Persona* p) {
    if(p->inactive_epochs >= PERSONA_MAX_INACTIVE_SCALE)
        return PERSONA_EPOCH_SECS * PERSONA_RECON_INACTIVE_MULT;
    return PERSONA_EPOCH_SECS;
}

// close the epoch: classify, roll active/inactive streaks, reset tallies (Epoch.next).
// active iff any activity or a handshake; the streaks drive the mood machine
static void persona_end_epoch(Persona* p) {
    p->epoch++;
    p->s.epochs_tot++;

    bool got_hs = (p->hs_this_epoch > 0);
    bool active = got_hs || (p->aps_this_epoch >= PERSONA_EPOCH_ACTIVE_APS);
    // any traffic (AP/capture/miss) resets the quiet streak; silence accrues quiet epochs
    bool traffic = got_hs || (p->aps_this_epoch > 0) || (p->misses_this_epoch > 0);
    if(traffic)
        p->quiet_epochs = 0;
    else if(p->quiet_epochs < 0xffffffff)
        p->quiet_epochs++;

    if(active) {
        p->active_epochs++;
        p->inactive_epochs = 0;
    } else if(p->hunting && p->quiet_epochs < PERSONA_HUNT_PATIENCE) {
        // engaged + recent traffic: stay content (firmware reports each AP once, so
        // per-epoch APs dry up mid-hunt); after PATIENCE silent epochs boredom is allowed
        p->active_epochs = 0;
        p->inactive_epochs = 0;
    } else {
        p->active_epochs = 0;
        p->inactive_epochs++;
    }

    // snapshot misses for next window's lonely (next_epoch reads num_missed before reset)
    p->last_epoch_missed = p->misses_this_epoch;
    p->misses_this_epoch = 0;

    p->aps_this_epoch = 0;
    p->hs_this_epoch = 0;
}

// steady-state mood with no reaction held (Automata.next_epoch): activity streak drives
// excited/bored/sad; a good friend nearby turns any down epoch grateful
static PersonaMood persona_baseline_mood(const Persona* p) {
    // sad supersedes bored (both pure inactivity); sleep stands in for automata's
    // set_angry at 2x sad; stale = is_stale() (attacked last epoch, caught nothing)
    bool sleepy = (p->inactive_epochs >= PERSONA_SLEEP_EPOCHS);
    bool sad = (p->inactive_epochs >= PERSONA_SAD_EPOCHS);
    bool bored = (p->inactive_epochs >= PERSONA_BORED_EPOCHS);
    bool stale = (p->last_epoch_missed > PERSONA_MAX_MISSES);
    bool down = sleepy || sad || bored || stale;

    // a good friend in range wins: grateful on a down epoch, bonded otherwise
    if(p->friend_near) return down ? MoodGrateful : MoodBonded;

    // live activity this epoch reacts fastest (AP volume + sustained excited)
    if(p->aps_this_epoch >= PERSONA_SMART_APS) return MoodSmart;
    if(p->active_epochs >= PERSONA_EXCITED_EPOCHS) return MoodExcited;
    if(p->aps_this_epoch >= PERSONA_EPOCH_ACTIVE_APS) return MoodMotivated;

    // stale -> lonely (was_stale -> set_lonely); only fires in active/Deauth mode
    // (passive never misses)
    if(stale) return MoodLonely;

    // a unit just dropped by -> curious (returning unit)
    if(p->secs_since_peer < PERSONA_CURIOUS_SECS) return MoodCurious;

    // slow decay: sleep > sad > bored, all pure inactivity
    if(sleepy) return MoodSleep;
    if(sad) return MoodSad;
    if(bored) return MoodBored;

    return MoodContent; // awake / normal (on_normal)
}

void persona_tick(Persona* p, uint32_t dt) {
    p->session_uptime += dt;
    p->s.total_uptime += dt;
    p->secs_since_peer += dt;
    p->secs_since_pwnd += dt;
    p->secs_in_epoch += dt;

    // epoch boundary: score the window, roll the streaks (window stretches while inactive)
    if(p->secs_in_epoch >= persona_epoch_len(p)) {
        p->secs_in_epoch = 0;
        persona_end_epoch(p);
    }

    // decay a held transient reaction (capture / new-friend flash)
    if(p->mood_lock_secs > dt)
        p->mood_lock_secs -= dt;
    else
        p->mood_lock_secs = 0;

    // hold a reaction while locked, else settle to the baseline
    if(p->mood_lock_secs == 0) p->mood = persona_baseline_mood(p);
}

void persona_note_peer(Persona* p, bool is_new, bool is_bonded) {
    p->secs_since_peer = 0;
    if(is_new) {
        p->friends_session++;
        p->s.friends_met++;
    }
    if(is_bonded) {
        p->friend_near = true;
        p->mood = MoodBonded; // no lock; friend_near keeps the baseline here
    } else if(is_new) {
        p->mood = MoodExcited;
        p->mood_lock_secs = PERSONA_PEER_REACT_SECS; // flash "new friend!"
    } else {
        p->mood = MoodCurious; // a familiar face dropped by
    }
}

void persona_note_pwnd(Persona* p) {
    p->pwnd_run++;
    p->s.pwnd_tot++;
    p->hs_this_epoch++;
    p->secs_since_pwnd = 0;

    // Fresh-capture reaction: 1 -> happy, 2..3 -> excited, streak -> cool.
    if(p->hs_this_epoch >= PERSONA_COOL_STREAK)
        p->mood = MoodCool;
    else if(p->hs_this_epoch >= 2)
        p->mood = MoodExcited;
    else
        p->mood = MoodHappy;

    p->mood_lock_secs = PERSONA_PWND_REACT_SECS; // hold the reaction face
}

void persona_note_ap(Persona* p) {
    p->aps_session++;
    p->s.aps_tot++;
    p->aps_this_epoch++;
    // no direct mood poke: baseline_mood reads aps_this_epoch each tick
}

void persona_note_miss(Persona* p) {
    // _on_miss: an interaction hit nothing -> brief DEMOTIVATED. also feeds the epoch
    // tally; past max_misses_for_recon the epoch is stale and baseline settles to lonely
    p->misses_this_epoch++;
    p->mood = MoodDemotivated;
    p->mood_lock_secs = PERSONA_MISS_REACT_SECS;
}

Face persona_face(const Persona* p) {
    switch(p->mood) {
    case MoodLonely:
        return FaceLonely;
    case MoodContent:
        return FaceAwake;
    case MoodCurious:
        return FaceLookRHappy;
    case MoodExcited:
        return FaceExcited;
    case MoodBonded:
        return FaceFriend;
    case MoodBored:
        return FaceBored;
    case MoodSleep:
        return FaceSleep;
    case MoodSad:
        return FaceSad;
    case MoodMotivated:
        return FaceMotivated;
    case MoodSmart:
        return FaceSmart;
    case MoodHappy:
        return FaceHappy;
    case MoodCool:
        return FaceCool;
    case MoodGrateful:
        return FaceGrateful;
    case MoodDemotivated:
        return FaceDemotivated;
    default:
        return FaceAwake;
    }
}

// voice lines from pwnagotchi voice.py; a few faithful variants per mood, rotated per epoch so a
// sustained mood doesn't repeat one line forever (view.py picks a random voice line each update).
const char* persona_mood_label(const Persona* p) {
    uint32_t r = p->epoch;
    switch(p->mood) {
    case MoodLonely: {
        static const char* v[] = {"I feel so alone ...", "Where is everybody?",
                                  "Nobody wants to play ..."};
        return v[r % 3];
    }
    case MoodContent: {
        static const char* v[] = {"...", "New hunt, new pwns!", "Hack the Planet!"};
        return v[r % 3];
    }
    case MoodCurious: {
        static const char* v[] = {"Unit is nearby!", "A familiar face!"};
        return v[r % 2];
    }
    case MoodExcited: {
        static const char* v[] = {"I'm living the life!", "So much fun!",
                                  "My crime is curiosity"};
        return v[r % 3];
    }
    case MoodBonded: {
        static const char* v[] = {"I love my friends!", "Good friends are near!"};
        return v[r % 2];
    }
    case MoodBored: {
        static const char* v[] = {"I'm bored ...", "Let's go for a walk!"};
        return v[r % 2];
    }
    case MoodSleep: {
        static const char* v[] = {"Zzzzz", "Napping ...", "ZzzZzz"};
        return v[r % 3];
    }
    case MoodSad: {
        static const char* v[] = {"I'm very sad ...", "I'm extremely bored ...", ":("};
        return v[r % 3];
    }
    case MoodMotivated: {
        static const char* v[] = {"Best day of my life!", "So motivated!"};
        return v[r % 2];
    }
    case MoodSmart: {
        static const char* v[] = {"So many networks!!!", "Look at all these APs!"};
        return v[r % 2];
    }
    case MoodHappy: {
        static const char* v[] = {"Cool, got a handshake!", "Gotcha!", "Another one!"};
        return v[r % 3];
    }
    case MoodCool: {
        static const char* v[] = {"I pwn therefore I am.", "On a streak!", "Too cool."};
        return v[r % 3];
    }
    case MoodGrateful: {
        static const char* v[] = {"Good friends are a blessing!", "I love my friends!"};
        return v[r % 2];
    }
    case MoodDemotivated: {
        static const char* v[] = {"Shitty day :/", "That didn't work ...", "Hmph."};
        return v[r % 3];
    }
    default:
        return "";
    }
}

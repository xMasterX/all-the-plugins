// pure 802.11 parsers for pwnpal — hardware-free (only stdint/stddef) so host-testable
// (see tests/). anything touching esp_wifi/Serial belongs in Pwnpal.cpp.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h> // so this compiles into the C fap too, not just the C++ firmware

// does this beacon/probe-response require 802.11w PMF (RSN MFPR bit)? if so deauth is
// futile, PMKID only. walks tagged params to the RSN IE (id 48) caps. bounds-checked,
// false on malformed input.
static inline bool pwnpal_rsn_requires_pmf(const uint8_t* f, int len) {
    int p = 36; // tagged params start after the 24-byte mgmt hdr + 12-byte fixed params
    while(p + 2 <= len) {
        int id = f[p], l = f[p + 1];
        if(p + 2 + l > len) break;
        if(id == 48 && l >= 2) { // RSN IE
            const uint8_t* r = f + p + 2;
            int off = 2; // version
            if(off + 4 > l) return false;
            off += 4; // group cipher suite
            if(off + 2 > l) return false;
            int pc = r[off] | (r[off + 1] << 8); // pairwise cipher count
            off += 2 + 4 * pc;
            if(off + 2 > l) return false;
            int ac = r[off] | (r[off + 1] << 8); // AKM suite count
            off += 2 + 4 * ac;
            if(off + 1 > l) return false;
            return (r[off] & 0x40) != 0; // RSN capabilities bit 6 = MFPR (required)
        }
        p += 2 + l;
    }
    return false;
}

// Extract a non-zero RSN PMKID from an (re)association request's RSN IE, if the client advertised a
// cached one (fast-roaming / PMKID caching). That PMKID cracks with hashcat -m 22000 exactly like an
// AP's M1 PMKID, given the AP MAC + client MAC + ESSID. Walks the tagged IEs to the RSN IE (id 48),
// past version/group/pairwise/AKM/caps to the PMKID list. Bounds-checked; fills pmkid[16] on true.
static inline bool pwnpal_assoc_pmkid(const uint8_t* f, int len, uint8_t pmkid[16]) {
    if(len < 30) return false;
    int sub = f[0] & 0xf0, ie;
    if(sub == 0x00)
        ie = 28; // assoc-req:   24 hdr + capability(2) + listen interval(2)
    else if(sub == 0x20)
        ie = 34; // reassoc-req: + current-AP address(6)
    else
        return false;
    int p = ie;
    while(p + 2 <= len) {
        int id = f[p], l = f[p + 1];
        if(p + 2 + l > len) break;
        if(id == 48 && l >= 2) { // RSN IE
            const uint8_t* r = f + p + 2;
            int off = 2; // version
            if(off + 4 > l) return false;
            off += 4; // group cipher suite
            if(off + 2 > l) return false;
            int pc = r[off] | (r[off + 1] << 8); // pairwise cipher count
            off += 2 + 4 * pc;
            if(off + 2 > l) return false;
            int ac = r[off] | (r[off + 1] << 8); // AKM suite count
            // Only a PSK-family AKM yields a hashcat-crackable PMKID (-m 22000). SAE/OWE/802.1X
            // PMKIDs are what hcx flags "KDV:0, not supported" -- don't harvest/count those.
            bool psk = false;
            for(int a = 0; a < ac; a++) {
                int so = off + 2 + 4 * a;
                if(so + 4 > l) break;
                if(r[so] == 0x00 && r[so + 1] == 0x0f && r[so + 2] == 0xac &&
                   (r[so + 3] == 2 || r[so + 3] == 4 ||
                    r[so + 3] == 6)) { // PSK, FT-PSK, PSK-SHA256
                    psk = true;
                    break;
                }
            }
            if(!psk) return false; // non-PSK (SAE/OWE/enterprise) PMKID -> uncrackable, skip
            off += 2 + 4 * ac;
            if(off + 2 > l) return false;
            off += 2; // RSN capabilities
            if(off + 2 > l) return false;
            int nk = r[off] | (r[off + 1] << 8); // PMKID count
            off += 2;
            if(nk <= 0 || off + 16 > l) return false;
            bool nz = false;
            for(int b = 0; b < 16; b++) {
                pmkid[b] = r[off + b];
                if(pmkid[b]) nz = true;
            }
            return nz; // the first PMKID, if it isn't all-zero
        }
        p += 2 + l;
    }
    return false;
}

// --- EAPOL / 4-way parsing (extracted from reportHandshake; pure + host-tested) ---

// Locate the 802.1X (EAPOL) header: scan for the 0x888e etherType in the LLC/SNAP region and
// return the offset of the 802.1X version byte just past it, or -1 if not present. Bounded to
// the LLC/etherType window (bytes 30..40) so a coincidental 88 8e deeper in the payload — or in
// the MAC header — can't false-match. Handles both the no-QoS (etherType@30) and QoS (@32) layouts.
static inline int pwnpal_eapol_locate(const uint8_t* f, int len) {
    int end = len - 2;
    if(end > 40) end = 40;
    for(int i = 30; i <= end; i++)
        if(f[i] == 0x88 && f[i + 1] == 0x8e) return i + 2;
    return -1;
}

typedef struct {
    bool key_ack, key_mic, secure, install; // key_info bits 7, 8, 9, 6
    uint8_t kdv; // key descriptor version (key_info bits 0-2). 0 = WPA3-SAE etc: not PSK-crackable
    uint8_t nonce[32];
    bool nonce_nz; // Key Nonce: ANonce on M1/M3, SNonce on M2, ~zero on M4
    uint8_t replay[8]; // EAPOL-Key replay counter (pairs M1<->M2)
    uint8_t pmkid[16];
    bool pmkid_nz; // RSN PMKID KDE present + non-zero (M1 PMKID attack)
} PwnpalEapolKey;

// Parse the EAPOL-Key frame whose 802.1X header starts at `eo` (from pwnpal_eapol_locate).
// Returns false if it isn't an EAPOL-Key or is truncated before key_info; on true *out is filled.
// Field offsets (from eo): key_info@5, key_len@7, replay@9(8), nonce@17(32), MIC@81(16),
// key-data-len@97(2), key-data@99 — byte-for-byte the gopacket/bettercap layout. Bounds-checked.
static inline bool pwnpal_eapol_key(const uint8_t* f, int len, int eo, PwnpalEapolKey* out) {
    if(eo < 0 || eo + 6 >= len) return false; // need at least through key_info
    if(f[eo + 1] != 0x03) return false; // 802.1X type must be Key (0x03)
    for(int i = 0; i < 32; i++)
        out->nonce[i] = 0;
    for(int i = 0; i < 8; i++)
        out->replay[i] = 0;
    for(int i = 0; i < 16; i++)
        out->pmkid[i] = 0;
    uint16_t ki = (uint16_t)((f[eo + 5] << 8) | f[eo + 6]);
    out->kdv = (uint8_t)(ki & 0x07); // 1=WPA/RC4 2=WPA2/AES 3=CMAC; 0=SAE etc (not PSK-crackable)
    out->key_ack = (ki & (1 << 7)) != 0;
    out->key_mic = (ki & (1 << 8)) != 0;
    out->secure = (ki & (1 << 9)) != 0;
    out->install = (ki & (1 << 6)) != 0;
    for(int b = 0; b < 8 && eo + 9 + b < len; b++)
        out->replay[b] = f[eo + 9 + b];
    out->nonce_nz = false;
    for(int b = 0; b < 32 && eo + 17 + b < len; b++) {
        out->nonce[b] = f[eo + 17 + b];
        if(out->nonce[b]) out->nonce_nz = true;
    }
    // RSN PMKID KDE in Key Data: DD <len> 00 0F AC 04 <16-byte PMKID>. WPA1 vendor KDEs use OUI
    // 00 50 F2, so the 00 0F AC 04 match can't false-positive on them.
    out->pmkid_nz = false;
    int kdl_off = eo + 97;
    if(kdl_off + 1 < len) {
        int kdl = (f[kdl_off] << 8) | f[kdl_off + 1];
        int kd = kdl_off + 2;
        int kd_end = kd + kdl;
        if(kd_end > len) kd_end = len;
        for(int i = kd; i + 22 <= kd_end; i++) {
            if(f[i] == 0xDD && f[i + 2] == 0x00 && f[i + 3] == 0x0F && f[i + 4] == 0xAC &&
               f[i + 5] == 0x04) {
                for(int b = 0; b < 16; b++) {
                    out->pmkid[b] = f[i + 6 + b];
                    if(out->pmkid[b]) out->pmkid_nz = true;
                }
                break;
            }
        }
    }
    return true;
}

// BSSID from the To-DS/From-DS bits (FC byte 1); optionally also returns the client (non-AP)
// address. Mirrors the derivation in reportHandshake/reportClient. Callers pass frames already
// known to be >= EAPOL-length (pwnpal_eapol_locate succeeded), so addr3@16 is in bounds.
static inline const uint8_t* pwnpal_ds_bssid(const uint8_t* f, int len, const uint8_t** client) {
    (void)len;
    const uint8_t* bssid;
    const uint8_t* cli;
    bool tods = (f[1] & 0x01) != 0;
    bool fromds = (f[1] & 0x02) != 0;
    if(fromds && !tods) {
        bssid = f + 10;
        cli = f + 4; // AP->STA: Addr2=BSSID, Addr1=STA
    } else if(!fromds && tods) {
        bssid = f + 4;
        cli = f + 10; // STA->AP: Addr1=BSSID, Addr2=STA
    } else if(!fromds && !tods) {
        bssid = f + 16;
        cli = f + 10; // IBSS: Addr3=BSSID
    } else {
        bssid = f + 10;
        cli = f + 4; // WDS: fallback
    }
    if(client) *client = cli;
    return bssid;
}

// Classify a parsed EAPOL-Key as which 4-way message it is: 1/2/3, or 0 for M4 / not a handshake
// message. Shared so the firmware (loose count) and the app (strict crackable) agree byte-for-byte.
static inline int pwnpal_eapol_msg(const PwnpalEapolKey* k) {
    if(k->key_ack && !k->key_mic) return 1; // M1 (ANonce; may carry a PMKID)
    if(!k->key_ack && k->key_mic && !k->secure) return 2; // M2 (SNonce + MIC)
    if(k->key_ack && k->key_mic && k->secure) return 3; // M3 (ANonce again, replay = M1 + 1)
    return 0; // M4 (nonce ~zero) or not a 4-way message
}

// --- per-client 4-way tracking (Phase B, two verdicts) --------------------------------------
// One table answers both questions the app asks about an (AP, client):
//   loose  — pwnagotchi/bettercap Handshake.Complete(): a PMKID, or an M2 (SNonce+MIC) seen
//            together with an ANonce source (M1 or M3), with NO replay/time match. Drives the pwn
//            count / skull, so the number matches what a pwnagotchi would claim.
//   strict — hcxpcapngtool-equivalent: a genuinely crackable pair — M1+M2 with the SAME replay
//            counter, or M2+M3 (M3's replay is M1's + 1), within `window` ms — or a PMKID. Drives
//            the per-AP "crackable" ✓.
// We keep the most recent ANonce half (M1, or M3 normalised down to its M1 replay) and the most
// recent M2 half per (AP, client), plus a sticky "seen" bitmask, and re-check on every frame.
// Keeping one half of each kind (not every half) can miss a strict pair only under rare
// same-kind interleavings before the opposite kind arrives; loose is unaffected (sticky bits).
enum {
    PWNPAL_M1 = 1,
    PWNPAL_M2 = 2,
    PWNPAL_M3 = 4,
    PWNPAL_PMKID = 8
};

typedef struct {
    bool used; // slot occupied
    uint8_t ap_idx; // index into the recon table (AP)
    uint8_t client[6]; // the non-AP station
    uint32_t ms; // millis() of the last touch (LRU eviction)
    uint8_t seen; // loose: OR of PWNPAL_M1/M2/M3/PMKID ever seen for this (AP, client)
    bool a_used; // strict: an ANonce half is stored (M1, or M3 normalised)
    uint8_t a_replay[8]; // its replay counter (M3 stored as M1's == M3-1)
    uint8_t a_kdv; // its key descriptor version (0 = not PSK-crackable)
    uint32_t a_ms;
    bool m2_used; // strict: an M2 half is stored
    uint8_t m2_replay[8];
    uint8_t m2_kdv;
    uint32_t m2_ms;
} PwnpalHs;

typedef struct {
    bool loose; // count / skull
    bool strict; // crackable ✓
} PwnpalHsVerdict;

// Decrement an 8-byte big-endian EAPOL replay counter by 1 (M3's counter is M1's + 1, so this
// normalises an M3 half onto its M1 for the strict compare). Returns false on underflow past zero
// (M3 counters are >= 1 in practice, so this only guards the degenerate all-zero input).
static inline bool pwnpal_rc_dec(const uint8_t* in, uint8_t* out) {
    int borrow = 1;
    for(int i = 7; i >= 0; i--) {
        int v = (int)in[i] - borrow;
        if(v < 0) {
            v += 256;
            borrow = 1;
        } else
            borrow = 0;
        out[i] = (uint8_t)v;
    }
    return borrow == 0;
}

// Record one EAPOL message for (ap, client) and return the loose/strict verdicts as they stand
// after this frame. msg is 1/2/3 (which 4-way message; 0/4 are ignored by the caller); pmkid marks
// an M1 that carried an RSN PMKID; kdv is the frame's key descriptor version (0 = WPA3-SAE etc,
// which hashcat can't crack -> excluded from strict, but still counted loose); replay is the
// frame's EAPOL replay counter. Pure + host-tested.
static inline PwnpalHsVerdict pwnpal_hs_note(
    PwnpalHs* t,
    int n,
    uint8_t ap_idx,
    const uint8_t* client,
    int msg,
    bool pmkid,
    uint8_t kdv,
    const uint8_t* replay,
    uint32_t now,
    uint32_t window) {
    PwnpalHsVerdict v = {false, false};
    // find the (ap, client) slot, else a free one, else evict the oldest.
    int slot = -1;
    for(int i = 0; i < n; i++)
        if(t[i].used && t[i].ap_idx == ap_idx && memcmp(t[i].client, client, 6) == 0) {
            slot = i;
            break;
        }
    if(slot < 0)
        for(int i = 0; i < n; i++)
            if(!t[i].used) {
                slot = i;
                break;
            }
    if(slot < 0) {
        slot = 0;
        for(int i = 1; i < n; i++)
            if((uint32_t)(now - t[i].ms) > (uint32_t)(now - t[slot].ms)) slot = i;
        t[slot].used = false;
    }
    PwnpalHs* e = &t[slot];
    if(!e->used) {
        e->used = true;
        e->ap_idx = ap_idx;
        memcpy(e->client, client, 6);
        e->seen = 0;
        e->a_used = false;
        e->m2_used = false;
    }
    e->ms = now;

    if(pmkid) { // PMKID (M1): crackable on its own, no pairing needed.
        e->seen |= PWNPAL_PMKID;
        v.loose = true;
        v.strict = (kdv != 0); // WPA3-SAE PMKID (kdv 0) is not PSK-crackable
        return v;
    }
    if(msg == 1) {
        e->seen |= PWNPAL_M1;
        e->a_used = true;
        memcpy(e->a_replay, replay, 8);
        e->a_kdv = kdv;
        e->a_ms = now;
    } else if(msg == 3) {
        e->seen |= PWNPAL_M3;
        uint8_t norm[8];
        if(pwnpal_rc_dec(replay, norm)) {
            e->a_used = true;
            memcpy(e->a_replay, norm, 8);
            e->a_kdv = kdv;
            e->a_ms = now;
        }
    } else if(msg == 2) {
        e->seen |= PWNPAL_M2;
        e->m2_used = true;
        memcpy(e->m2_replay, replay, 8);
        e->m2_kdv = kdv;
        e->m2_ms = now;
    }

    // loose: an M2 plus any ANonce source, or a PMKID (handled above). kdv-agnostic.
    if((e->seen & PWNPAL_M2) && (e->seen & (PWNPAL_M1 | PWNPAL_M3))) v.loose = true;
    // strict: replay-matched ANonce + M2 halves within the window, both PSK-crackable (kdv != 0).
    if(e->a_used && e->m2_used && e->a_kdv != 0 && e->m2_kdv != 0 &&
       memcmp(e->a_replay, e->m2_replay, 8) == 0) {
        uint32_t dt = e->a_ms > e->m2_ms ? e->a_ms - e->m2_ms : e->m2_ms - e->a_ms;
        if(dt <= window) v.strict = true;
    }
    return v;
}

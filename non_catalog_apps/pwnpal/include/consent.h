#pragma once

#include <stdbool.h>
#include <stdint.h>

// one-time persisted "authorized to test only" acknowledgement. stored separately from
// the persona so a persona reset never re-arms capture; bumping the text re-prompts.
#define PWNPAL_CONSENT_PATH "/ext/apps_data/pwnpal/capture_consent.bin"
#define PWNPAL_CONSENT_MAGIC 0x50574343u // "PWCC"
#define PWNPAL_CONSENT_VERSION 1 // bump when the warning text changes

typedef struct {
    uint32_t magic;
    uint32_t version; // must equal PWNPAL_CONSENT_VERSION to count as given
    uint64_t accepted_unix;
} PwnpalConsent;

// True iff the consent file is present, valid, and matches the current version.
bool consent_is_given(void);

// Write the consent file with the current version + timestamp.
void consent_record(void);

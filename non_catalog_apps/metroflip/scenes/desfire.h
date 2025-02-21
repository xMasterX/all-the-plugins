#ifndef DESFIRE_H
#define DESFIRE_H

#include "../metroflip_i.h"
#include <lib/nfc/protocols/mf_desfire/mf_desfire.h>

typedef enum {
    CARD_TYPE_ITSO,
    CARD_TYPE_OPAL,
    CARD_TYPE_CLIPPER,
    CARD_TYPE_MYKI,
    CARD_TYPE_DESFIRE_UNKNOWN
} DesfireCardType;

bool itso_verify(const MfDesfireData* data);
bool opal_verify(const MfDesfireData* data);
bool clipper_verify(const MfDesfireData* data);
bool myki_verify(const MfDesfireData* data);

#endif // DESFIRE_H

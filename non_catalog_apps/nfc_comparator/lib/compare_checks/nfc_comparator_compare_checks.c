#include "nfc_comparator_compare_checks.h"

NfcComparatorCompareChecks* nfc_comparator_compare_checks_alloc() {
   NfcComparatorCompareChecks* checks = malloc(sizeof(NfcComparatorCompareChecks));
   if(!checks) return NULL;

   checks->nfc_card_path = furi_string_alloc();
   checks->uid = false;
   checks->uid_length = false;
   checks->protocol = false;
   checks->nfc_data = false;
   checks->type = NfcCompareChecksType_Undefined;

   return checks;
}

void nfc_comparator_compare_checks_free(NfcComparatorCompareChecks* checks) {
   if(checks) {
      furi_string_free(checks->nfc_card_path);
      free(checks);
   }
}

void nfc_comparator_compare_checks_reset(NfcComparatorCompareChecks* checks) {
   if(checks) {
      furi_string_reset(checks->nfc_card_path);
      checks->uid = false;
      checks->uid_length = false;
      checks->protocol = false;
      checks->nfc_data = false;
      checks->type = NfcCompareChecksType_Undefined;
   }
}

NfcCompareChecksType nfc_comparator_compare_checks_get_type(const NfcComparatorCompareChecks* checks) {
   furi_assert(checks);
   return checks->type;
}

void nfc_comparator_compare_checks_set_type(
   NfcComparatorCompareChecks* checks,
   NfcCompareChecksType type) {
   furi_assert(checks);
   checks->type = type;
}

void nfc_comparator_compare_checks_compare_cards(
   NfcComparatorCompareChecks* checks,
   const NfcDevice* card1,
   const NfcDevice* card2,
   bool check_data) {
   furi_assert(checks);
   furi_assert(card1);
   furi_assert(card2);

   // Compare protocols
   checks->protocol = nfc_device_get_protocol(card1) == nfc_device_get_protocol(card2);

   // Get UIDs and lengths
   size_t uid_len1 = 0, uid_len2 = 0;
   const uint8_t* uid1 = nfc_device_get_uid(card1, &uid_len1);
   const uint8_t* uid2 = nfc_device_get_uid(card2, &uid_len2);

   // Compare UID length
   checks->uid_length = (uid_len1 == uid_len2);

   // Compare UID bytes if lengths match
   if(checks->uid_length) {
      checks->uid = (memcmp(uid1, uid2, uid_len1) == 0);
   } else {
      checks->uid = false;
   }

   // compare NFC data
   if(check_data) {
      checks->nfc_data = nfc_device_is_equal(card1, card2);
   } else {
      checks->nfc_data = false;
   }
}

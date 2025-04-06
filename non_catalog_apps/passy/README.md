# Passy: A Flipper Zero Passport reader

## Tested with the following countries:
🇺🇸
🇨🇱

(If it works for yours, submit a PR to add your country flag)

## Notes:
 - Caches MRZ info to make subsequent use faster

## Limitations
 - Only requests DG1, the contents of the MRZ

## To do
 - Support other country passports
 - Add event for file not found when selecting file, and UI

## Generate asn:

```bash
asn1c -D ./lib/asn1 -no-gen-example -pdu=all eMRTD.asn1
```

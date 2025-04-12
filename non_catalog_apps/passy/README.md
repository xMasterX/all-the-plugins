# Passy: A Flipper Zero Passport reader

## Tested with the following countries:
🇺🇸
🇨🇱

(If it works for yours, submit a PR to add your country flag)

## Notes:
 - Caches MRZ info to make subsequent use faster

## Limitations
 - Does not parse some of the optional DG (under 'advanced' menu)

## To do
 - Support other country passports

## Generate asn:

```bash
asn1c -D ./lib/asn1 -no-gen-example -pdu=all eMRTD.asn1
```

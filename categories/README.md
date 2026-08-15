# App sub-categories (CI post-build grouping)

These files tell `../categorize_apps.sh` how to sort the built `.fap` files into
sub-folders inside their category folders, so the Flipper app browser shows tidy
sub-categories like `GPIO/ESP32` or `Games/Puzzle`.

## Layout

```
categories/<Category>/<Subcategory>.txt
```

- `<Category>` must match the app's `fap_category` (the folder the build already
  puts it in): `GPIO`, `Games`, `NFC`, `Sub-GHz`, ...
- `<Subcategory>` is the new sub-folder name.
- Each file lists one **appid** per line -- the `.fap` filename, i.e. the app's
  `appid` from its `application.fam` (NOT the source-folder name). Blank lines and
  lines starting with `#` are ignored.

## Editing

- Move an app: add/remove its appid in the relevant `.txt`.
- New sub-category: drop in a new `.txt` file (e.g. `GPIO/I2C.txt`).
- New category: make a new folder (e.g. `Media/`) with `.txt` files inside.

Nothing else needs changing -- the script discovers these files automatically. Apps
not listed anywhere stay in their category root. An appid that isn't actually built
is simply skipped, so a typo just means "nothing moved", never a broken build.

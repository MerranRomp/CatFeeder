# Configuration page

The ESP32-C6 now hosts its own Wi-Fi network and configuration page. It does not
need home Wi-Fi or internet access. Identification and feeder mechanisms are still
mocked. This is the first UI/configuration stage, not a calibrated automatic feeder.

The page supports one configured cat. Refresh after a firmware update to load the current UI.

## Open the page on a phone or laptop

1. Keep the board powered through the CH343 USB connection.
2. Open its serial monitor at 115200 baud and send `w`.
3. Join the printed `CatFeeder-XXXX` Wi-Fi network using the printed password.
   The current board reports `CatFeeder-FFB9`. If your phone warns about no internet,
   choose to stay connected to this local network.
4. Open **http://192.168.4.1** in the connected device's browser.

The password is generated on first setup and retained in the board's NVS storage.
It is not checked into the repository or exposed through the web API. Send `w`
again whenever you need it. Anyone with this network password can edit settings.

## What the first version does

| Setting | Current behavior |
| --- | --- |
| Portion target, grams | Stored target only; 0/blank means unset. Calibration is pending. |
| Daily feeding times | Up to six unique 24-hour times, stored as a plan. Automatic meals are inactive. |
| Cat name and tag ID | One cat profile. Its saved tag becomes the controller's authorized identity. |
| Minimum feeding interval | Applies to repeat-feed protection after a completed mock dispense. |
| Device status | Live mock access/dispensing states, recognition, and uptime. |

Save changes to commit the configuration to the device. Tags and the repeat interval
apply when both operations are no longer running. Saving does not clear faults or
feeding history. Another browser's outdated revision is rejected rather than silently
overwriting more recent settings. Discard restores the last loaded/saved values.

The default demo profile is `Demo cat` with tag `mock-cat`. Serial command `i`
simulates that specific tag. If you remove or change it, `i` will no longer authorize
the demo cat. A real tag reader and automatic tag enrollment are not implemented.
The page has one editable profile without add/remove-cat controls. The API also
rejects additional profiles. An empty authorization list is supported internally
and denies access; an unset profile must be filled in before saving through the UI.
If an earlier prototype saved multiple profiles, the first is loaded along with
the feeding settings. That older record is replaced only on the next explicit save.

Settings survive a reset. **The mock feeding history is still RAM-only**, so reset
clears that history. Do not connect a real dispenser until durable feeding recovery,
mechanical safeguards, and calibration have been implemented and tested.

## Build and upload

Standard PlatformIO builds target `esp32c6-mock` and use ESP-IDF's built-in
Wi-Fi, HTTP, JSON, and NVS components. No external managed components are required.
The page is one embedded HTML/CSS/JS file without an npm build.

For this Windows setup, use the helper to keep generated files out of Documents:

```powershell
.\scripts\firmware.cmd build
.\scripts\firmware.cmd upload COM3
```

Close the serial monitor with Ctrl+C before uploading. The helper copies the named
source directories into `%TEMP%\CatFeeder-firmware-source` and builds under
`%TEMP%\CatFeeder-firmware-build`. It never copies `.git` or private configuration, and
does not delete directories. The 4 MB factory application partition leaves room for
the page and future firmware within the detected 16 MB flash. NVS and PHY locations
are retained. OTA updates are not implemented.

## Preview and browser checks without the ESP32

From the project root:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory web
```

Open **http://127.0.0.1:8765/?preview**. The preview badge identifies this mode.
Changes are stored only in that browser's local storage and never sent to the board.
Without `?preview`, the page expects the device API and reports offline when it is
not available. Stop the local preview server with Ctrl+C when finished.

`scripts/check-ui.mjs` exercises the preview using an isolated Chromium/Edge browser
with DevTools listening on localhost port 9223. With that browser and the static
server running, execute `node scripts/check-ui.mjs`. It checks desktop/mobile
overflow, add/edit/remove controls, duplicate meal times, safe cat-name rendering,
save/reload, discard, and offline behavior. It requires Node 22+ and no npm packages.
The helper's browser session must be a test profile, not your personal session.

The portable settings validation is also covered in the 18 native scenarios run by
`scripts/check.cmd`. Opening the UI over the feeder's Wi-Fi, saving/reloading a
configuration there remain the final device checks.

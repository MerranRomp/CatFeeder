# Software starting point

The README remains the source of project requirements and hardware decisions.
This implementation has a portable C++11 core with mock adapters and a serial
demo entry point for ESP-IDF and Arduino. The current board is an ESP32-C6 using
the `esp32-c6-devkitc-1` profile, assumed with the team's authorization. esptool
identified the chip as ESP32-C6 revision v0.1 and detected 16 MB flash through
the CH343 bridge on COM3. The profile's flash setting is overridden to match.
The team identified the board as nanoESP32-C6 V1.0. Peripheral pins, reader, motors,
and physical safety strategy remain undecided. Configuration uses onboard NVS.
Feeding history remains mocked in RAM; configured daily meal times are stored but inactive.

Validation on 5 November 2026: the C6 build with Wi-Fi and embedded
configuration page succeeded and was uploaded through CH343 on COM3 with flash
hashes verified. Boot, setup credentials (redacted from test output), and mock
serial behavior were checked. All 18 desktop test scenarios passed. Browser checks
passed for responsive layout, preview saving/reload, duplicate-time validation,
safe rendering of cat names, discard, and offline handling. A Wi-Fi client's
end-to-end save still needs a user check. Real feeder identification, safety inputs,
and motion remain mocked.

## Responsibilities

| Location | Responsibility |
| --- | --- |
| `include/catfeeder/Controller.h` | Configuration, input snapshot, adapter contracts, controller API |
| `src/Controller.cpp` | Authorization, independent operation states, deadlines, repeat prevention |
| `include/catfeeder/Settings.h`, `src/Settings.cpp` | Portable settings schema and validation |
| `src/DeviceServices.cpp` | C6 Wi-Fi access point, HTTP API, and NVS configuration storage |
| `web/index.html` | Self-contained responsive configuration UI, embedded in firmware |
| `simulation/Fakes.h` | Mock actuators, example inputs, in-memory feeding history |
| `simulation/main.cpp` | One deterministic example without sleeping or hardware |
| `src/esp32_main.cpp` | ESP-IDF app_main / Arduino setup/loop with serial commands and mock devices only |
| `platformio.ini` | Default C6 ESP-IDF environment and retained original-ESP32 Arduino environment |
| `src/CMakeLists.txt` | ESP-IDF component containing the controller and firmware entry point |
| `sdkconfig.defaults` | UART console defaults for the CH343 serial path |
| `partitions.csv` | 4 MB application partition; existing NVS/PHY locations retained |
| `tests/controller_tests.cpp` | Behavioral checks, including faults and restart scenarios |

The platform entry point samples identification and safety inputs, calls
`update(nowMs, inputs)` frequently, and submits explicit requests when application
policy allows. The core has no Arduino dependency. Identification adapters map
reader-specific data into a stable, consistently formatted identifier string.
There is no reader driver or general-purpose sensor hierarchy yet.

Recognition alone never triggers movement. Requests need a fresh snapshot,
an authorized ID, and an explicitly safe input for the affected operation.
Repeated observations therefore cannot independently trigger repeated portions.
The current policy uses a global minimum interval after successful dispensing;
it does not implement daily quantities, per-cat budgets, or clock-based meals.
All example settings and the `mock-cat` identifier are simulation data.

Each operation progresses from idle to running to complete, or to a latched
fault. Access and dispensing cannot run concurrently. A fault in either blocks
new requests for both. Lost authorization, unknown/unsafe relevant safety data,
stale input, unknown/failed actuator status, and deadlines fault running work.
Command acceptance is distinct from confirmed completion. All calls, including
adapter calls, must return promptly and run from a single caller.

## Safety and recovery boundaries

`handleFault()` means the adapter must execute its validated physical fault
response. It does not imply closing a lid, reversing a motor, or removing power.
Those choices depend on the mechanism. Safe inputs must come from validated
safeguards; recognizing a cat does not establish physical clearance. The mock
safe inputs provide no evidence about animal safety.

Access completion describes the requested operation only. There is no automatic
closing, occupancy tracking, or ongoing enforcement after it completes. The full
access lifecycle must be designed with the mechanism before a real adapter is
connected. Startup never commands movement, including in a fault state; actual
output states during boot and power loss require hardware design as well.

Before dispensing, the controller commits an `InProgress` feeding record. Only
confirmed completion changes it to `Completed`. Unreadable or interrupted history
blocks feeding after restart. A completed record restarts the full minimum
interval on boot because elapsed powered-off time is unknown. There is no
automatic fault reset or uncertain-portion retry. Recovery requires a future,
deliberate procedure that resolves the physical state and feeding record.

The provided `MemoryHistory` only retains data while its object exists. Tests
reuse it across controller reconstruction to exercise the restart policy. It is
not power-loss persistence. A real storage adapter must commit durably before
reporting success, detect corrupt/torn records, and distinguish a known initial
installation from lost history. Its storage and recovery strategy remain open.

Timing uses a wrapping 32-bit monotonic millisecond counter, not wall-clock time.
Configured intervals must be positive and at most 2^31-1 milliseconds. Update
frequently; input timestamps must use that same clock. Every snapshot must reflect
fresh acquisition of all its fields: do not stamp cached readings with a new time.
Portion units are abstract and require hardware calibration; they are not grams.

## Run locally

On Windows with CMake and a C++ compiler, or Visual Studio C++ Build Tools with
the CMake component installed:

```powershell
.\scripts\check.cmd
```

The script configures, builds, runs the behavioral tests, and runs the simulation.
It downloads nothing. Generated files stay in the ignored `build/` directory.
If Windows prevents build tools from writing under Documents, choose a temporary
build directory explicitly:

```powershell
.\scripts\check.cmd "$env:TEMP\CatFeeder-native-build"
```

For another environment with a configured C++ compiler and CMake:

```sh
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Next hardware step

### ESP32 mock firmware

The default `esp32c6-mock` environment uses PlatformIO with ESP-IDF because the
official platform package supports the C6 through that framework. `espressif32`
is pinned to 6.13.0 for a repeatable starting point. PlatformIO installs the required
ESP-IDF framework, RISC-V compiler, and supporting tools when absent. No peripheral
libraries are added. The official
[ESP32-C6-DevKitC-1 profile](https://docs.platformio.org/en/latest/boards/espressif32/esp32-c6-devkitc-1.html)
defaults to 8 MB flash; the connected chip reports 16 MB, configured explicitly here.
The profile is an authorized assumption about the board model, not a verified model ID.

The original `esp32-mock` Arduino environment remains available for the previously
reported WROOM-32 DevKit V1 / clone with 4 MB flash. Build/upload commands below
target the replacement C6; do not use the original-ESP32 image on it.

Root CMake selects ESP-IDF only when PlatformIO supplies `CATFEEDER_ESP_IDF=ON`.
Desktop CMake builds remain independent of ESP-IDF. The IDF component explicitly
lists its two sources, while the Arduino environment uses `build_src_filter`.
ESP-IDF's generated sdkconfig lives in the PlatformIO workspace with build output.
After changing SDK defaults, regenerate the SDK configuration and use a fresh build
workspace so preprocessed linker scripts match the configuration. Reusing linker
output from the initial flash/MMU settings caused a boot failure during bring-up;
the verified C6 image was built in a fresh `CatFeeder-c6` workspace.

Build without touching a connected device:

```powershell
pio run -e esp32c6-mock
```

If `pio` is not on PATH, use the PlatformIO terminal in VS Code, or on this Windows
installation:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32c6-mock
```

If build tools cannot write under Documents, redirect PlatformIO's generated
workspace for the current terminal session before building:

```powershell
$env:PLATFORMIO_WORKSPACE_DIR = "$env:TEMP\CatFeeder-c6"
```

With the board connected through its CH343 USB port, upload and open the serial monitor using
PlatformIO's Upload and Monitor actions, or:

```powershell
pio run -e esp32c6-mock -t upload
pio device monitor -b 115200
```

No upload port is assumed. Specify `--upload-port COMx` when uploading if multiple
devices are connected, and `--port COMx` when opening the monitor, replacing COMx
with the actual board port (COM3 was detected during bring-up; it can change).
Keep application peripherals disconnected for this mock bring-up. The firmware
initializes UART0 with the default console routing to the CH343 bridge. The C6
entry point yields one FreeRTOS tick between non-blocking input polls/controller
updates; the controller itself remains independent of the RTOS. Native USB
Serial/JTAG console communication is not configured for this demo.

On this Windows installation, the verified board's monitor can be opened directly:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor -e esp32c6-mock --port COM3 --baud 115200
```

Send `h` for help. Press Ctrl+C to close the monitor before another upload.

Commands are single characters; line endings are ignored:

| Command | Effect |
| --- | --- |
| `h` | Print help again if the startup message was missed |
| `i` / `u` | Simulate the authorized cat being present / absent |
| `a` | Request mock access |
| `d` | Request one mock dispensing operation |
| `c` | Report the running mock operation complete |
| `s` | Print current states |
| `w` | Show the C6 configuration Wi-Fi network, password, and page address |

Try `d` first: it should be denied because no cat is identified. Send `i`, then
`a`, then `c`. Send `d`, then `c`, then immediately `d` again: the last request
should be denied by the five-second interval. Send each command separately. Mock
operations fault if not completed within ten seconds. Removing the mock cat during
an operation also faults it. Send `s` to inspect state; faults do not clear themselves.

Reset restarts the demo, including **erasing its RAM-only feeding history**. This
does not validate real power-loss recovery and must not be used to drive a real
dispenser. No physical identification, actuator, or sensor is connected by this code.

After build/upload/serial checks, introduce one validated adapter at a time.
Select and validate identification technology before implementing a reader. Define
the physical safety/access lifecycle, fault behavior, durable persistence, and
recovery before enabling real movement. Local Wi-Fi configuration is now implemented; remote networking, displays, and an app remain deferred.

## Local configuration and device status

See [web-ui.md](web-ui.md) for joining the feeder's Wi-Fi network and testing the page.
The user selected a device-hosted Wi-Fi network. Its generated password is stored
in NVS and shown only over the local serial connection (`w`), not through the API.
The UI has no CDN, fonts, cloud accounts, or external runtime dependencies.

The HTTP task handles `GET /`, `GET /api/status`, and `GET`/`POST /api/settings`.
JSON bodies are bounded to 4096 bytes, validated on the device, and committed as
one versioned NVS record. The revision field rejects stale edits from another
browser session. The custom request header and JSON content type prevent ordinary
cross-origin form submissions. Administrative access is shared with anyone holding
the feeder's Wi-Fi password; separate user accounts/TLS are not implemented.

HTTP callbacks do not invoke the controller. The main loop picks up pending policy
changes between operations. `Controller::reconfigure` preserves latched faults,
operation state, and feeding history/cooldown. Flash commits happen in the HTTP
task without holding the mutex used for the small settings/status snapshots.

One cat's name/tag ID, feeding interval, gram targets, and up to six daily meal
times are stored. Only authorization and the repeat interval currently affect mock operation.
The gram target does not change calibrated units, and no clock-based scheduler is
running. These limits are visible in the UI. Settings persist across reset; the
mock feeding history does not.

On Windows, `scripts\firmware.cmd build` builds an isolated temporary source copy.
`scripts\firmware.cmd upload COM3` builds and uploads it. This avoids Windows
blocking the component manager's generated files under Documents. The helper copies
only named project sources and never deletes or mirrors directories. The standard
PlatformIO workflow remains available where generated project files are writable.

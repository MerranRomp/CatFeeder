# CatFeeder — ESP32 Automatic Cat Feeder

## Project overview

CatFeeder is a school mechatronics project to design and prototype an automatic dry-food feeder for **one configured cat**. It should identify the approaching cat, control its access to food, and dispense measured portions automatically.

We want a practical, reliable prototype that can be developed and tested incrementally, without locking the software to components we have not chosen yet.

## Project constraints

- **Team:** Four students studying electrical engineering / mechatronics.
- **Controller:** nanoESP32-C6 V1.0, as identified by the team, with 16 MB flash detected through the CH343 bridge. The `esp32-c6-devkitc-1` profile is used as the compiler target. The original WROOM-32 board was not detected during USB bring-up and is suspected faulty.
- **Budget:** Target of **€75** for the project, unless the team chooses to contribute extra money.
- **Food:** Dry cat food only. Wet food is excluded because of storage, hygiene, and spoilage concerns.
- **Status:** Early software development. Hardware, mechanical design, wiring, and some behaviors are **not finalized**.

## Intended functionality

The feeder is intended to:

1. **Identify a cat** approaching the feeder using an appropriate identification system, with RFID as the current direction.
2. **Control food access** so only authorized cats can reach the food at the appropriate time.
3. **Dispense dry food automatically**, ideally in configurable portions.
4. **Prevent unwanted feeding**, including access by an unrecognized cat and accidental repeated dispensing.
5. **Operate safely and predictably**, especially around moving parts and animals.

A local Wi-Fi configuration page has been requested and implemented for the C6 prototype. Food-level sensing, a display, remote networking, logging, and an app remain possibilities, **not committed requirements**.

## Important: hardware is not finalized

Do **not** assume a specific motor, servo, dispenser mechanism, sensor, pin mapping, power supply, RFID module, or ESP32 variant until it is confirmed.

**RFID compatibility warning:** The MFRC522/RC522 reader previously used for experiments operates at 13.56 MHz and is **not suitable for directly reading standard implanted pet microchips**, which commonly use 134.2 kHz (ISO 11784/11785). Whether the final feeder reads implanted microchips or uses collar tags is undecided. Reader selection, antenna placement, and practical read range must be validated before implementing the final identification hardware.

## Software approach

Prioritize **modularity, clarity, testability, and easy hardware replacement** over complexity.

- Keep application decisions separate from hardware-specific drivers.
- Use interfaces or small abstractions where they make replacing a sensor or motor practical.
- Put adjustable parameters (pins, feeding amounts, timing, authorized IDs) in configuration rather than scattering them through the code.
- Use non-blocking behavior where possible; avoid long `delay()` calls in normal operation.
- Build one working subsystem at a time and verify it before adding more.
- Allow simulation or mocked input where possible so logic can be tested without physical components.
- Treat loss of sensor readings, failed identification, jams, restarts, and power issues as normal cases to handle.
- Never automatically move a mechanism when it could trap or injure a cat. Mechanical and sensor safeguards still need to be designed and tested.

The current development environment is **VS Code + PlatformIO + ESP-IDF** for ESP32-C6 mock firmware bring-up; it remains replaceable. The compiler target is `esp32-c6-devkitc-1`, with its flash setting adjusted to the detected 16 MB. Use the **CH343 USB-to-UART port** for this demo. The previous original-ESP32 Arduino environment is retained as `esp32-mock`. Peripheral pins and feeder hardware remain unselected.

The portable controller, desktop checks, and mock feeder firmware are described in [docs/architecture.md](docs/architecture.md), including current validation results. The C6 also hosts a local configuration page; see [docs/web-ui.md](docs/web-ui.md). Settings and the generated Wi-Fi password use NVS storage. Feeding history is still in memory: the prototype does not operate real feeder mechanisms or preserve feeding history across power loss.

**Current validation:** The C6 firmware with Wi-Fi and web UI built and uploaded through CH343 on COM3. Boot, Wi-Fi setup details, and mock serial behavior were checked. All 18 desktop scenarios passed, and browser checks passed for the desktop/mobile UI, preview persistence, validation, and offline handling. Saving through a Wi-Fi client still needs a user check. Actual feeder hardware remains untested.

**Prototype boundary:** Authorized tags and the minimum feeding interval apply to the mock controller. Gram targets and daily meal times are stored setup values; automatic schedules and gram-based dispensing are not active until timekeeping and dispenser calibration are implemented.

**Latest upload:** The current firmware was uploaded successfully through COM3. Serial help, mock status, and Wi-Fi setup details were verified afterward. All 18 desktop scenarios and the desktop/mobile browser checks passed.

## Proposed software modules (not necessarily implemented)

| Module | Responsibility |
| --- | --- |
| `CatIdentification` | Read and interpret cat identification events. |
| `AccessController` | Decide when food access should be granted or denied. |
| `AccessActuator` | Control the physical lid, door, or other access mechanism. |
| `Dispenser` | Control delivery of dry-food portions. |
| `FeedingScheduler` | Apply feeding times, portion limits, and repeat-feeding rules. |
| `Sensors` | Abstract any presence, position, food-level, or safety sensors selected later. |
| `Config` | Centralize pins, limits, calibration values, and feature settings. |

These are **suggested responsibilities**, not a demand to create seven classes or files immediately. Keep the initial code as simple as the current hardware and testing needs allow.

## Suggested development order

1. Establish an ESP32 project that builds and uploads successfully.
2. Confirm the actual ESP32 board and development framework.
3. Build and test the cat-identification interface using mocked IDs first, then real hardware.
4. Prototype the access-control logic independently of the motor hardware.
5. Connect and test the selected access actuator with safety limits.
6. Prototype dispensing and portion calibration.
7. Integrate the identification, access, and dispensing systems.
8. Add optional features only after the core system works reliably.

The exact order may change as hardware becomes available.

## Guidance for Codex / AI coding assistants

When helping with this repository:

- **Check what actually exists in the repository before editing.** Do not assume proposed modules have already been created.
- **Do not invent confirmed hardware details.** Ask for the missing board/module information when it is essential; otherwise use clearly labeled placeholders or mocks.
- **Discuss the intended change before large refactors or multi-file implementations.** Prefer small, reviewable steps.
- **Explain technical decisions in a way that helps students learn.** Keep code readable and avoid unnecessary abstractions.
- **Separate tested behavior from assumptions.** Never describe untested hardware behavior as working.
- **Do not silently add dependencies, networking, cloud services, or external accounts.** Explain why they would help first.
- **Do not overwrite teammates' work.** Keep Git changes focused and easy to review.
- **Report build/test commands and results accurately.** If hardware cannot be tested, state that explicitly.
- **Use this README as project context, not as evidence that a feature has already been implemented.**

## Current decisions and open questions

| Topic | Status |
| --- | --- |
| Main controller | nanoESP32-C6 reported; DevKitC-1 compiler profile; 16 MB flash detected |
| Food type | Dry food |
| Cat identification | RFID direction; exact tag/chip and reader TBD |
| Access mechanism | TBD |
| Dispensing mechanism | TBD |
| Motors and drivers | TBD |
| Presence / safety sensors | TBD |
| Power supply | TBD |
| Development framework | PlatformIO + ESP-IDF for C6 mock firmware; Arduino environment retained for original ESP32 |
| Wi-Fi | Local password-protected SoftAP and configuration page requested; no cloud services |
| Cat profiles | One configured cat per feeder |
| App / display | Optional; not decided |

**Update this document whenever the team commits to a hardware choice or changes a requirement.**

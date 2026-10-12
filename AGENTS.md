# ESPreSwitch: notes for agents and maintainers

ESPreSwitch is ESPresense's fork of [SwitchbOTA](https://github.com/kendallgoto/switchbota) (GPL-3.0).
It converts a **stock** SwitchBot Plug Mini (ESP32-C3, no USB, case welded shut) straight to
ESPresense over the air. It writes a full ESPresense factory image, so the plug gets ESPresense's
own bootloader and partition table instead of Tasmota's.

## How it works

1. The user points `wohand.com` at the machine running `server/` (DNS override, or `node index.js <ip>`
   for the built-in DNS proxy), then triggers a "firmware update" over BLE (README Part 3).
2. The stock firmware downloads the **stage app** (`app.bin`, built from `espressif/`) into an OTA
   slot. The stage fetches `/payload.md5` (`"<md5> <size>"`) from the server. If it's running below
   the payload's end (`ota_0` at `0x20000`), it copies itself to the next slot (`ota_1` at `0x220000`)
   and reboots. If no slot starts above the payload's end, it stops without erasing anything.
3. Running from above the payload's end, the stage checks `/payload.bin`'s length against that size,
   then **erases flash from `0x0` to the size rounded up to 4 KB** and writes the payload raw from
   offset 0. It MD5-checks both the download and a read-back of flash, and retries until both match.
   With no saved WiFi it joins `espreswitch`/`espreswitch` (`config.h`; was `switchbota` before v0.4.1).
   If WiFi is down for a minute, the stage also opens an AP `espreswitch-xxxxxx` with a page at
   192.168.4.1 to enter a new network. WiFi config is saved to NVS until the erase starts, then RAM only
   (`wifi_stop_persisting()`): NVS (`0x9000` or `0xE000`) is inside the erased region.
   The LEDs (GPIO7/8, active low) show the state, one pattern per stuck state (`espressif/main/led.c`).
   Once erased, only the two erased patterns show. The README "What the LEDs mean" table must
   match `led.c`; update both together.
4. The plug reboots into ESPresense with no settings and opens an open AP `espresense-<mac>`
   (portal at 192.168.4.1).

## Single source of truth: `payload.json`

`{ "url", "md5" }` of the factory image to install.
- Only the server reads it. `server/index.js` downloads the payload, verifies the MD5, and serves
  `/payload.bin` plus `/payload.md5` (`"<md5> <size>"`, 503 until the payload is downloaded and verified).
  It fetches the stage from this repo's latest release and checks it against `app.bin.md5`.
- The stage has nothing payload-specific compiled in, so changing the payload needs no new release.

**Safety bound:** the stage erases `0x0` up to the payload size rounded up to 4 KB, so it must run
from a partition that starts above that, or it erases itself mid-write and bricks the plug. It checks
this on the plug against the real partition table: `0x220000` on stock plugs, `0x1E0000` on the
Tasmota v11 layout. It erases nothing until it has the payload info and the download size matches.

## Build and release

- CI (`.github/workflows/main.yml`) builds the stage with **ESP-IDF v4.4**, the toolchain upstream
  proved on stock plugs, and uploads `app.bin` + `app.bin.md5`. Pushing a `v*` tag publishes a
  release with both; the server downloads `releases/latest`. `workflow_dispatch` is enabled.
- Local build: `docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -e IDF_TARGET=esp32c3 -v "$PWD":/project -w /project/espressif espressif/idf:v4.4 idf.py build`
  - Use `IDF_TARGET`, **not** `idf.py set-target`. `set-target` resets the committed `sdkconfig`
    (drops the custom partition table and sets 2 MB flash).
  - A local build may rewrite `espressif/sdkconfig` with IDF patch-level churn. Revert it before
    committing (`git checkout -- espressif/sdkconfig`).
- Changing the payload: update `payload.json` (`md5sum`), push, then `git pull` and restart the server.
  No stage rebuild or release is needed.

## Flash layouts (4 MB)

| | SwitchBot stock (`espressif/part.csv`) | Tasmota v11.1.0 (after SwitchbOTA) | ESPresense (`partitions_singleapp.csv`) |
|---|---|---|---|
| nvs | `0xe000` 24K | `0x9000` 20K | `0x9000` 20K |
| otadata | `0xa000` | `0xe000` | `0xe000` |
| app slots | `0x20000`, `0x220000` (1904K each) | `0x10000`, `0x1E0000` (1856K each) | `0x10000`, `0x1F0000` (1920K each) |
| spiffs | none (has keydata/region/factory_nvs/energydata) | `0x3B0000` 320K | `0x3F1000` 60K |

ESPresense's IDF firmware mounts SPIFFS by label (`spiffs`) with `format_if_mount_failed`, and
reads the partition table at runtime, so it tolerates either of the last two layouts.

## Status (2026-10-11)

- `v0.4.2` released (stage `app.bin` + `.md5`): payload info from the server, re-checked on every retry; WiFi rescue portal (captive, AP closes before joining, network kept until the erase); LED status. CI is green.
- `payload.json` pins `esp32c3.factory.bin` from the **ESPresense v5.0.0b1 pre-release** (release asset,
  so it doesn't expire). Writing it ends at `0x174000`, below both `0x220000` (stock) and `0x1E0000` (Tasmota v11 layout).
- **Tested end-to-end on 2026-10-11** with v0.4.0:
  - ESP32-C3 dev board flashed with the stock partition table and the stage in `ota_0`: copied itself to
    `ota_1`, wrote the payload and booted ESPresense v5.0.0b1 in 75 s.
  - A real W1901400 on stock firmware **v2.7**: `57 0F 0A 01 0C` made it fetch
    `/version/wocaotech/firmware/WoPlugUS/WoPlugUS_V12.bin`; nothing more until `57 0F 0B`, which installs
    and reboots. The stage was already in `ota_1` (no `/app.bin` copy), wrote the payload first try,
    and the plug came up as ESPresense.
  - Rescue portal (v0.4.2, C3 dev board, an ESP32-S3 posting the form): AP opened at 60 s; after the
    save the stage closes the AP, then joins (with the AP open, the WPA handshake timed out: reason 15);
    the network survived the reboot into `ota_1` (saved to NVS until the erase); converted at 380 s.
  - Not exercised yet: the plug's LED patterns, the Tasmota v11 layout.

### Background: plugs converted via Tasmota

The maintainer's existing plugs (e.g. node `master`) went stock → Tasmota v11.1.0 → ESPresense,
so they keep Tasmota's IDF 4.4 bootloader and layout and report `esp32c3-cdc`. On 2026-10-09,
`master` was OTA'd from a PlatformIO/Arduino build (`d0decca`) to an ESP-IDF 5.4 build (`0d3fb97`).
It confirmed the flash, then dropped off the network entirely until it was power-cycled. After
that it ran the new build normally. So IDF 5.4 builds **do** boot on the Tasmota bootloader, but a
plug can hang on the first reboot after OTA. Expect to power-cycle, and confirm each plug comes
back before flashing the next.

## Open tasks

- [x] Test ESPreSwitch end-to-end on a stock plug (done on firmware v2.7, 2026-10-11).
- [ ] Test the Tasmota v11 layout path (dev board with app0 `0x10000` / app1 `0x1E0000`, or a real converted plug).
- [ ] Re-pin `payload.json` to a stable ESPresense release once one ships `esp32c3.factory.bin`
      (currently on the v5.0.0b1 pre-release).
- [ ] ESPresense: an `esp32c3-switchbot` flavor with the plug's relay/button/LED pin defaults
      (relay outputs: ESPresense/ESPresense#2530), so plugs identify themselves.
- [ ] ESPresense-companion: make `start_firmware_update` refuse to change a node's flavor unless forced.
- [ ] Find out why the plug hung after the OTA reboot (needs serial logs from an opened plug).
- [ ] Known companion issues seen while testing: after a flash, the `arduino_ota` OFF command is
      sent while the node is still rebooting, so it stays ON. A second companion using the default
      MQTT `client_id` kicks the first one off the broker.

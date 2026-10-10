# SwitchPresense: notes for agents and maintainers

SwitchPresense is ESPresense's fork of [SwitchbOTA](https://github.com/kendallgoto/switchbota) (GPL-3.0).
It converts a **stock** SwitchBot Plug Mini (ESP32-C3, no USB, case welded shut) straight to
ESPresense over the air. It writes a full ESPresense factory image, so the plug gets ESPresense's
own bootloader and partition table instead of Tasmota's.

## How it works

1. The user points `wohand.com` at the machine running `server/` (DNS override, or `node index.js <ip>`
   for the built-in DNS proxy), then triggers a "firmware update" over BLE (README Part 3).
2. The stock firmware downloads the **stage app** (`app.bin`, built from `espressif/`) into an OTA
   slot. If the stage lands in `ota_0` (`0x20000`), it copies itself to `ota_1` (`0x220000`) and reboots.
3. Running from `ota_1`, the stage **erases flash from `0x0` to `WRITE_SIZE`** and writes the
   payload (`/payload.bin`) raw from offset 0. It MD5-checks both the download and a read-back
   of flash, and retries until both match.
4. The plug reboots into ESPresense with no settings and opens an open AP `espresense-<mac>`
   (portal at 192.168.4.1).

## Single source of truth: `payload.json`

`{ "url", "md5" }` of the factory image to install.
- `espressif/CMakeLists.txt` downloads it at configure time, verifies the MD5, and generates
  `espressif/main/include/payload_config.h` (gitignored) with `BINARY_MD5` and `WRITE_SIZE`
  (payload size rounded up to 4 KB).
- `server/index.js` reads the same file to download and serve `/payload.bin`. It fetches the stage
  from this repo's latest release and checks it against `app.bin.md5`.

**Safety bound:** the build fails unless `0x20000 < WRITE_SIZE <= 0x220000`. The stage runs from
`ota_1` at `0x220000`, and a bigger payload would overwrite it mid-write and brick the plug. The
stage also uses `WRITE_SIZE` to tell which slot it's running from (`address < WRITE_SIZE` means
`ota_0`). Never hand-edit those values in `config.h`.

## Build and release

- CI (`.github/workflows/main.yml`) builds the stage with **ESP-IDF v4.4**, the toolchain upstream
  proved on stock plugs, and uploads `app.bin` + `app.bin.md5`. Pushing a `v*` tag publishes a
  release with both; the server downloads `releases/latest`. `workflow_dispatch` is enabled.
- Local build: `docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -e IDF_TARGET=esp32c3 -v "$PWD":/project -w /project/espressif espressif/idf:v4.4 idf.py build`
  - Use `IDF_TARGET`, **not** `idf.py set-target`. `set-target` resets the committed `sdkconfig`
    (drops the custom partition table and sets 2 MB flash).
  - A local build may rewrite `espressif/sdkconfig` with IDF patch-level churn. Revert it before
    committing (`git checkout -- espressif/sdkconfig`).
- Changing the payload: update `payload.json` (`md5sum`), push, let CI pass, tag a new release.
  The stage's baked-in MD5 has to match the server's payload, so a payload change without a new
  stage release breaks conversions.

## Flash layouts (4 MB)

| | SwitchBot stock (`espressif/part.csv`) | Tasmota v11.1.0 (after SwitchbOTA) | ESPresense (`partitions_singleapp.csv`) |
|---|---|---|---|
| nvs | `0xe000` 24K | `0x9000` 20K | `0x9000` 20K |
| otadata | `0xa000` | `0xe000` | `0xe000` |
| app slots | `0x20000`, `0x220000` (1904K each) | `0x10000`, `0x1E0000` (1856K each) | `0x10000`, `0x1F0000` (1920K each) |
| spiffs | none (has keydata/region/factory_nvs/energydata) | `0x3B0000` 320K | `0x3F1000` 60K |

ESPresense's IDF firmware mounts SPIFFS by label (`spiffs`) with `format_if_mount_failed`, and
reads the partition table at runtime, so it tolerates either of the last two layouts.

## Status (2026-10-10)

- `v0.1.0` released (stage `app.bin` + `.md5`). CI is green.
- `payload.json` pins the **esp32c3 factory image from ESPresense CI run 37950310137**
  (ESPresense/ESPresense#2532, merged 2026-10-09). CI artifacts expire after ~90 days (around 2027-01-07).
- **Not yet tested on a stock plug.** Do the first run on a plug you're prepared to open.

### Background: plugs converted via Tasmota

The maintainer's existing plugs (e.g. node `master`) went stock → Tasmota v11.1.0 → ESPresense,
so they keep Tasmota's IDF 4.4 bootloader and layout and report `esp32c3-cdc`. On 2026-10-09,
`master` was OTA'd from a PlatformIO/Arduino build (`d0decca`) to an ESP-IDF 5.4 build (`0d3fb97`).
It confirmed the flash, then dropped off the network entirely until it was power-cycled. After
that it ran the new build normally. So IDF 5.4 builds **do** boot on the Tasmota bootloader, but a
plug can hang on the first reboot after OTA. Expect to power-cycle, and confirm each plug comes
back before flashing the next.

## Open tasks

- [ ] Test SwitchPresense end-to-end on a stock plug (firmware v1.4 or older).
- [ ] Re-pin `payload.json` to a release asset once an ESPresense release ships
      `esp32c3.factory.bin` (`v5.0.0-b.0` is still a draft), then tag a new SwitchPresense release.
- [ ] ESPresense: an `esp32c3-switchbot` flavor with the plug's relay/button/LED pin defaults
      (relay outputs: ESPresense/ESPresense#2530), so plugs identify themselves.
- [ ] ESPresense-companion: make `start_firmware_update` refuse to change a node's flavor unless forced.
- [ ] Find out why the plug hung after the OTA reboot (needs serial logs from an opened plug).
- [ ] Known companion issues seen while testing: after a flash, the `arduino_ota` OFF command is
      sent while the node is still rebooting, so it stays ON. A second companion using the default
      MQTT `client_id` kicks the first one off the broker.

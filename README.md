
# ESPreSwitch
![switchbot-product-photo](./images/switchbot-product-photo.jpg)

ESPreSwitch converts a stock SwitchBot Smart Plug Mini straight to [ESPresense](https://espresense.com) over the air, without disassembling the device. It is a fork of [SwitchbOTA](https://github.com/kendallgoto/switchbota) by Kendall Goto, which does the same for Tasmota.

**Why not go through Tasmota?** SwitchbOTA's route (stock → Tasmota v11.1.0 → ESPresense) leaves the plug on Tasmota's bootloader and partition table forever. Current ESPresense builds (pure ESP-IDF v5.4) do boot on it, but they were never built or tested for that layout, and one plug hung after an OTA reboot until it was power-cycled. ESPreSwitch writes an ESPresense *factory image* instead: ESPresense's own bootloader, partition table and app, so the plug ends up laid out like any other ESPresense ESP32-C3 node.

The image it installs is pinned in [`payload.json`](./payload.json) (URL + MD5). The stage binary asks the server for the image's MD5 and size, and refuses to erase anything if the image would reach the partition it is running from.

## SwitchBot Hardware Details



There are two SKUs of the SwitchBot Smart Plug Mini released in the US: 

- The original "Smart Plug Mini" with model number W1901400, released in early 2022. Amazon sells these in [one-packs](https://amzn.to/3VEOGyR), [two-packs](https://amzn.to/4jIEXBy) and [four-packs](https://amzn.to/47dJ4hx)
- The updated "HomeKit Smart Plug Mini" with model number W1901401, released later in 2022. As of 2026-19, they are unavailable.

Some details about the devices are as follows:

- Both devices use identical circuitry. The only difference is the firmware used. More detail can be found in https://github.com/kendallgoto/switchbota/issues/19
- They use Type B NEMA 5-15 plugs and sockets
- They are ultrasonically welded shut, requiring destruction of the plastic housing to access the circuits within. There are no screws to allow access to the internals, the way there is with the Sonoff S31
- They use the Shanghai Belling BL0937 energy measurement IC to track power usage
- They use the Espressif ESP32-C3 chip for WiFi, Bluetooth Low Energy (BLE), and circuit control. See [here](https://tasmota.github.io/docs/ESP32/#esp32-differences) to learn about the different types of ESP32. The MAC addresses for WiFi and BLE follow the [ESP32 standard](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/system/misc_system_api.html#mac-address) of having an offset of two. If the WIFI-MAC is `34:85:18:0F:CC:DC`, then the BLE-MAC is `34:85:18:0F:CC:DE` (DC in hex is 220, DE is 222)
- The upstream SwitchbOTA flashing process is confirmed to work on both device models using firmware v1.4 or older (v1.3, v1.2). ESPreSwitch uses the same stage binary and triggers, only the payload differs

## Disclaimer
Writing to the bootloader over OTA is dangerous and not normally done for good reason. If your device loses power or somehow flashes corrupted data, the device will be bricked and require disassembly to reprogram the device. Only perform this process if you are comfortable disassembling your plug to fix it if it breaks! Of course, do not unplug or otherwise disturb the plug while it is performing the OTA.

**This is a proof of concept and provided with no warranty.** The ESPresense payload has not yet been tested on a stock plug. Try it first on a plug you are prepared to open.

**Already converted a plug via Tasmota?** ESPreSwitch needs the stock SwitchBot firmware; it cannot re-run on a plug that already runs Tasmota or ESPresense.

## Part 1: Plug Setup

- **TL;DR: Connect the plug to your WiFi network**
- Install the SwitchBot app on your phone https://play.google.com/store/apps/details?id=com.theswitchbot.switchbot
- Sign in or create an account (necessary to allow devices to be added/configured)
- Plug the plug into a wall outlet
- In-app, add a device. It's safe to connect it to your WiFi network. 
  - Note down the BLE-MAC during this process, as it will be referenced later
  - This step uses Bluetooth Low Energy (BLE) to scan for devices and send the WiFi network information

![switchbot](./images/switchbot-setup-screen.jpg)


- In-app, check the device firmware by going to its settings. As of 2023-12, there was no firmware update button, and the firmware version was v1.4. If you do see the upgrade button, do not press it and wait for a later part
- DONE! The plug is now ready for the firmware flashing process

## Part 2: Network and Server Setup 

- **TL;DR: Configure your router & DNS to redirect calls to a local IP. Run a local NodeJS server at that IP**
- Note down the LAN/local IP address where the NodeJS server will be run. For this example, it will be `192.168.0.69`
- Configure your router and/or DNS to redirect calls to SwitchBot servers to instead hit your NodeJS server
  - This means that a DNS override or custom DNS record should be added, redirecting both `www.wohand.com` and `wohand.com` to the IP where the NodeJS server will be run. More detail can be found at https://github.com/kendallgoto/switchbota/issues/3#issuecomment-1121828064
  - In pfSense, this can be done in the Services->DNS Resolver or Forwarder by adding a Host Override with Host `www` and Domain `wohand.com` and a second override with Host `wohand` and Domain `com`, both having an IP of 192.168.0.69. Save the override and Apply the settings

![pfsense](./images/pfsense-setup.png)
   - In PiHole, this can be done in the Local DNS->DNS Records by setting the Domain to `www.wohand.com` and `wohand.com` and the IP to 192.168.0.69
  - On a UniFi gateway (UDM, UDM Pro, UCG, UXG), add two **Host (A)** DNS records in UniFi Network, `wohand.com` and `www.wohand.com`, both pointing at 192.168.0.69. In Network 9.x this is Settings → Policy Engine → Policy Table → Create New Policy → DNS; in Network 8.x, Settings → Routing → DNS. UniFi records match exactly, so add both names. Power-cycle the plug afterwards so it drops any cached answer
  - If your router does not support custom DNS entries, it will almost certainly support setting a custom DNS server. This may be found in the DHCP settings or the System settings. Set the custom DNS server to 192.168.0.69
- Run a local NodeJS server to act in place of the real firmware update server and instead deliver modified firmware files: firstly the OTA (stage) file, and then the ESPresense factory image
  - Install NodeJS on your computer https://nodejs.org/en/learn/getting-started/how-to-install-nodejs
  - Using a CLI or Terminal, `cd` into the `server/` directory and run `npm i` (short for `npm install`) to install the dependencies required for this project
  - Once the previous command completes, run `node index.js` to run the server. 
    - OR, if you could not set up a DNS record in the previous step, run `node index.js 192.168.0.69` . This will start the server, in addition to a Man-in-the-middle DNS server that redirects `*wohand.com` calls to the IP of the NodeJS server, rather than the public IP of `*wohand.com`. All other DNS lookups will be unaffected
  - The CLI or Terminal should show `Server listening on port 80`. Keep this window open for the duration of the flashing process
- Test the DNS functionality by opening a new CLI or Terminal and running `ping wohand.com` and `ping www.wohand.com` to confirm that the local IP shows up. In this example, look for 192.168.0.69
- Test the Node server functionality by opening the SwitchBot app and then looking at the terminal where Node is running. A new line should have shown up similar to `::ffff:192.168.0.101 - /version/wocaotech/release.json`. This indicates the server was accessed instead of the real `wohand.com` and that it successfully served a JSON file to the app. The IP 192.168.0.101 is the IP of the phone running the app. 
- DONE! The network is now configured and the server should now be ready to serve firmware files to the plug

## Part 3: Firmware Flashing

- **TL;DR: Send BLE commands to the SwitchBot plug to trigger the firmware upgrade process**
- If you saw an upgrade button in the SwitchBot app, it is now safe to hit that button. No additional actions should be needed, and the firmware replacement process should complete. If no button is present in the SwitchBot app, proceed
- Install an app such as [nRF Connect for Mobile](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp). This will be used to connect to the Bluetooth Low Energy (BLE) radio of the plug to trigger the firmware update process. [Bluetility for Mac](https://github.com/jnross/Bluetility) is also reported to work
- Follow the instructions below, or watch [the video](https://youtu.be/iTexFQ0Th0I?si=zB-leDeiz82yL9Cy&t=635), or read the GitHub issues outlining the process https://github.com/kendallgoto/switchbota/issues/43  https://github.com/kendallgoto/switchbota/issues/3#issuecomment-1121864522
- Using the BLE app of choice, scan for devices and Connect to the device matching the BLE-MAC noted from the SwitchBot app

![nrf1](./images/nrf-connect-screen-1.png)

- Make sure the app is on the Client tab, then expand "Unknown Service" and look at the options within. The last option, "Unknown Characteristic" should have Properties such as "WRITE" and an upload button on the right side. Hit the Upload button on the right side to bring up the Write screen.

![nrf2](./images/nrf-connect-screen-2.png)

- Select the BYTE_ARRAY type from the dropdown and enter in the hex code `57 0F 0A 01 0C` (without spaces). Then it Send to transmit the command to the plug
  - The last two characters represent the firmware version to be flashed, and must be different from the current firmware version. `0C` (as shown above) represents v1.2, `0D` represents v1.3, `0E` represents v1.4. `0C` should be safe to use by default. More detail can be found [here](https://github.com/kendallgoto/switchbota/issues/3#issuecomment-1491004013) 

![nrf3](./images/nrf-connect-screen-3.png)

- Wait a few seconds and look at the NodeJS log to verify the command worked as it should. You should see something such as `::ffff:192.168.0.131 - /version/wocaotech/firmware/WoPlugUS/WoPlugUS_V12.bin` which indicates the NodeJS server was able to send the first `.bin` file to the plug
- Wait about a minute after seeing the NodeJS server log to give the plug time to get ready for the next step.
- Now send a new BYTE_ARRAY command in the BLE app (nRF Connect) with hex `57 0F 0B`. Hit Send to trigger the second and final firmware flash step
- Wait a few seconds again and then look at the NodeJS log to verify the command worked as it should. You should see something such as `::ffff:192.168.0.131 - /payload.bin` which indicates the NodeJS server was able to send the ESPresense factory image to the plug
- DONE! The firmware flashing should be complete

## Part 4: ESPresense Setup

- **TL;DR: Join the plug's `espresense-…` WiFi, enter your WiFi and MQTT details, then set the plug's pins**
- The stage binary wipes the start of flash, so the plug boots ESPresense with no settings. It opens an open WiFi network named `espresense-` followed by its MAC address. Join it from a phone or laptop; the captive portal opens (or browse to http://192.168.4.1)
- Enter your WiFi network, MQTT broker and room name, then save. The plug reboots, joins your network and appears in ESPresense-companion / Home Assistant like any other node
- Open the node's web page, go to **Hardware**, and set the relay, button and LED pins for the plug. The Plug Mini's GPIO map is in the [Blakadder template](https://templates.blakadder.com/switchbot_plugmini_W1901400.html)
- From here on, update the plug like any other ESPresense node. Use the plain `esp32c3` firmware; the plug has no USB, so `-cdc` builds buy nothing

____



## Firmware Update Sequence Detail
The included ESP-IDF code is a lightweight OTA client that directly writes to the embedded flash chip, enabling the install of non-app level code, including modfiying the bootloader and partiton table.

1. The update is triggered by the app with a BLE message, such as `57 0F 0A 01 0C`
2. The device fetches `http://www.wohand.com/version/wocaotech/firmware/WoPlugUS/WoPlugUS_VXX.bin` for the firmware
3. The request is intercepted and served by the NodeJS web server, which downloads the Espressif binary
4. The factory firmware installs the binary to ota_0 or ota_1, depending on past usage
5. The binary runs from its OTA partition. If it's on ota_1, it skips to step 6. Otherwise, it performs another OTA, downloading itself into ota_1 and rebooting.
6. Once on OTA_1, the device fetches `http://www.wohand.com/payload.bin` for OTA
7. The request is intercepted and served by the NodeJS web server, delivering the payload pinned in `payload.json` (an ESPresense factory image)
8. The Espressif binary fetches the payload's MD5 and size from `/payload.md5`, wipes the internal flash up to that size (rounded up to 4 KB) and flashes the payload. It performs two checksums, one of the downloaded binary and another of the flashed binary. It will continue to retry without restarting until the checksums are valid

## Troubleshooting

### What the LEDs mean
If a conversion goes wrong, the LEDs may be all you have to go on. While the ESPreSwitch stage is running, both LEDs blink together to show what it's doing.

**Nothing erased yet: safe to unplug**

| Pattern | Meaning | What to do |
|---|---|---|
| Slow blink, once a second | Looking for WiFi | Wait. After a minute it switches to the rescue network |
| 2 blinks, pause | Can't reach WiFi; rescue network `espreswitch-xxxxxx` is up | Join it and enter a WiFi network (see below) |
| Short blip every 2 seconds | On WiFi, but the ESPreSwitch server isn't answering properly | Check the server is running, the payload has finished downloading, and `wohand.com` resolves to it |
| 3 blinks, long pause | Refused: the payload is too big for this plug's flash layout | Nothing was erased. Pick a smaller payload or unplug |
| Solid | Talking to the server; about to erase or copying itself to the other slot | Wait |

**Flash erased: do NOT unplug**

| Pattern | Meaning | What to do |
|---|---|---|
| Fast blink, 5 times a second | Writing ESPresense | Wait; it reboots into ESPresense when done |
| 5 fast blinks, pause | A write attempt failed (WiFi dropped, server unreachable, or checksum mismatch); retrying | Keep it powered. Check the server. If WiFi is the problem, the rescue network still appears after a minute |

Once the erase starts, the LEDs only ever show these two patterns, even if WiFi drops, so "don't unplug" is always visible. Losing power while they show is the one thing that means opening the plug.

**Other states**

- **Stock SwitchBot behaviour:** the stage isn't running yet.
- **Dark, with an open `espresense-…` WiFi network:** the conversion worked. ESPresense is running with no LED pins configured.
- **Dark, with no network at all:** the plug isn't booting. If it went dark while the erased patterns were showing, it lost power mid-write and needs opening.

The stage never drives the relay. If the button still switches the relay, the plug is running some other firmware (stock, Tasmota or ESPresense), not the stage.

### The plug can't reach WiFi
The stage joins the WiFi network saved in the plug. If that's missing, it tries SSID `switchbota`, password `switchbota`. If it still has no WiFi after a minute, it opens its own open network named `espreswitch-xxxxxx` (the end of its MAC address):

1. Join `espreswitch-xxxxxx` from a phone or laptop and browse to http://192.168.4.1.
2. Enter a WiFi network the plug can use. That network has to resolve `wohand.com` to the ESPreSwitch server: your normal LAN with the DNS override, or any network whose DHCP hands out the server's IP as DNS.
3. The `espreswitch-xxxxxx` network disappears once the plug connects, and the flash carries on.

This works even after the flash is erased, as long as the plug stays powered. The new network is kept in RAM only, so a power cut loses it, but a power cut after the erase means opening the plug anyway.

## Updating the payload
1. Pick an ESPresense build that publishes `esp32c3.factory.bin` (release asset or CI artifact).
2. Put its URL and MD5 in `payload.json` (`md5sum esp32c3.factory.bin`) and push.
3. On the server: `git pull` and restart it.

The stage binary has nothing payload-specific compiled in: it asks the server for the payload's MD5 and size before erasing anything, so no new ESPreSwitch release is needed.

## Credits
ESPreSwitch is a fork of [SwitchbOTA](https://github.com/kendallgoto/switchbota) by Kendall Goto (GPL-3.0); all of the BLE trigger, DNS and stage-binary work is theirs. To support the original author, see their [sponsor page](https://github.com/sponsors/kendallgoto).

## Links and Resources

- The digiblurDIY Youtube channel has a video guide for this process, and the website has additional details, including detail on the alternate firmware ESPHome
  - Video: https://www.youtube.com/watch?v=iTexFQ0Th0I
  - Website: https://digiblur.com/wiki/devices/plugs/switchbot_15_amp_w1901400/
- This project is similar to https://github.com/khcnz/Espressif2Arduino
- GitHub Issues/Discussions
  - Discussion on OTA and BLE procedure https://github.com/kendallgoto/switchbota/issues/3
  - Discussion on Tasmota version and incompatibility with ESPHome https://github.com/kendallgoto/switchbota/issues/31
  - Recent write-up on the flashing process https://github.com/kendallgoto/switchbota/issues/43
- Tasmota
  - Documentation https://tasmota.github.io/docs/
  - GitHub https://github.com/arendst/Tasmota
  - Firmware (ESP32-specific) https://ota.tasmota.com/tasmota32/release/
  - Template for SwitchBot https://templates.blakadder.com/switchbot_plugmini_W1901400.html
- SwitchBot App
  - Google Play Store https://play.google.com/store/apps/details?id=com.theswitchbot.switchbot
  - iOS https://apps.apple.com/us/app/switchbot/id1087374760
- nRF Connect App
  - Google Play Store https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp
  - iOS https://apps.apple.com/us/app/nrf-connect-for-mobile/id1054362403

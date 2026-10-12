// This file is part of switchbota (https://github.com/kendallgoto/switchbota/).
// Copyright (c) 2022 Kendall Goto.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.

// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

// Modified for ESPresense (ESPreSwitch), 2026.

#pragma once

#define BINARY_URL "http://www.wohand.com/payload.bin"
#define PAYLOAD_INFO_URL "http://www.wohand.com/payload.md5"  // "<md5 hex> <size>"
#define OTA_BUF 2048

// The plug's LEDs (Blakadder W1901400 template: Led_i 1 and 2, active low). GPIO6 is the relay: leave it alone.
#define LED1_GPIO 7
#define LED2_GPIO 8

#define FALLBACK_URL "http://www.wohand.com/app.bin"
// Joined only when the plug has no saved WiFi (e.g. a dev board, or a plug that lost its settings)
#define FALLBACK_SSID "espreswitch"
#define FALLBACK_PASS "espreswitch"

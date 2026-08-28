# Shared Energy Telemetry Device

This device belongs to the Wilhelminaweg energy community in Wageningen, a group
of households that share surplus solar energy. What that project sets out to do,
and how, is described in
[docs/energiegemeenschap-wilhelminaweg.md](docs/energiegemeenschap-wilhelminaweg.md)
(in Dutch).

ESP32-S3 firmware that retrieves shared-community energy data from the
Energyboxx API and presents the current power state using an addressable LED
ring. Wi-Fi and API credentials can be configured from a captive web portal,
so credentials do not need to be compiled into the firmware.

## Current behavior

The device requests Energyboxx telemetry once per minute and uses
`community_power_result_kw` to control the active LED ring:

| Resulting power | Meaning | LED ring |
| --- | --- | --- |
| Greater than `+0.05 kW` | Energy available to share | Solid green |
| Less than `-0.05 kW` | Energy must be bought | Solid yellow |
| Between `-0.05 kW` and `+0.05 kW` | Community balanced | Off |
| Wi-Fi or API request unavailable | Data is stale or unavailable | Off |

All eight LEDs are illuminated together.

The second LED ring is initialized but intentionally left unused for future
functionality.

## Hardware

The current configuration targets an ESP32-S3 and two 8-pixel WS2812 LED
rings.

### Pin mapping

| Function | Board pin | ESP32-S3 GPIO |
| --- | --- | --- |
| Unused LED ring | D0 | GPIO 1 |
| Energy-status LED ring | D1 | GPIO 2 |
| Wi-Fi status LED (blue) | D7 / RX | GPIO 44 |
| Power status LED (red) | D8 | GPIO 7 |
| Data status LED (blue) | D9 | GPIO 8 |
| Credential-reset button | — | GPIO 17 |

The three discrete status LEDs are configured as active-high outputs. External
LEDs must have suitable current-limiting resistors. The LED rings are limited
to 10% brightness in `main/main.c`.

### Status LEDs

| LED | Off | Blinking | Solid |
| --- | --- | --- | --- |
| Wi-Fi | Initial connection in progress | Provisioning active or connection failed | Connected |
| Power | Firmware has not initialized | — | Firmware is running |
| Data | — | No valid token or no successful telemetry response | API token and telemetry are working |

Blinking uses a 500 ms on/off interval.

## Provisioning

On startup, the device first tries credentials stored in NVS. If Wi-Fi
credentials are absent, the saved network cannot be reached within 30 seconds,
or the stored API credentials are invalid, it starts a provisioning access
point:

```text
SSID: SETD_Provisioning
Password: none
```

Connect a phone or computer to this network. The captive portal should open
automatically; otherwise, open the access point's gateway address in a browser.
The portal performs two steps:

1. Select a Wi-Fi network and enter its password.
2. Enter and validate the Energyboxx client ID and client secret.

Credentials are saved to ESP-IDF NVS only after their respective connection or
validation succeeds. After provisioning, the captive portal stops and the
device switches to station-only Wi-Fi mode.

## Credential reset and recovery

There are two ways to erase the saved Wi-Fi and Energyboxx credentials:

- Hold the GPIO 17 reset button low for at least three seconds during startup.
- Power-cycle the device three times within ten seconds.

After a successful ten-second boot, the rapid-boot counter is reset. Only a
deliberate power cycle counts towards it: a reset caused by a firmware panic,
a watchdog or a brownout is deliberately ignored, so a software fault cannot
erase the stored credentials on its own.

At runtime the device keeps trying to reconnect after a disconnect, following a
backoff schedule of 0.5 s, 1 s, 2 s, 5 s, 10 s, 30 s, 60 s and then every five
minutes for as long as the network stays away. The Wi-Fi status LED starts
blinking once the schedule reaches its ten-second step. The schedule is the
table `s_retry_schedule` in `main/src/wifi_provisioning.c`.

Failed token or telemetry requests do not stop the firmware: the energy ring is
cleared and the API is retried on a backoff schedule of 10 s, 20 s, 40 s, 80 s,
160 s and then every 5 minutes, until a request succeeds. Every wait, including
the normal one-minute telemetry interval, is spread by up to a fifth so a fleet
that booted together does not keep asking together. The schedule is the table
`s_api_retry_delay_ms` in `main/main.c`.

If nobody uses the provisioning portal for fifteen minutes, the device restarts
and tries its saved credentials again. Any page or action in the portal resets
that clock, so it measures silence rather than elapsed time.

## Building

Install and activate an Espressif ESP-IDF environment, then run:

```bash
idf.py build
```

The target, flash size, partition layout and main-task stack size come from
`sdkconfig.defaults`, which holds only the settings this project deliberately
chooses. The generated `sdkconfig` is not checked in: it is rewritten on every
configure and would bury those few decisions in four thousand lines. Do not run
`idf.py set-target`; it discards `sdkconfig.defaults`.

The project uses the ESP-IDF Component Manager for these dependencies:

- `espressif/led_strip`
- `espressif/cjson`

`sdkconfig.defaults` selects an ESP32-S3 with 8 MB flash and a two-slot OTA
partition layout.

The ESP-IDF version is not pinned yet. The configuration that used to be
checked in was generated by ESP-IDF 6.1.0, while CI builds against 5.5; see M9
in `docs/REVIEW.md`.

## Tests and checks

Modules that do not depend on ESP-IDF are compiled and run on the development
machine:

```bash
make -C test check
```

Every push runs three jobs in GitHub Actions: these host tests, a `cppcheck`
pass over `main/`, and a full ESP32-S3 firmware build. See
`.github/workflows/ci.yml`.

## Flashing and monitoring

With the board connected, replace `PORT` with its serial port:

```bash
idf.py -p PORT flash monitor
```

Exit the serial monitor with `Ctrl+]`.

## Configuration

The principal prototype settings are defined near the top of `main/main.c`:

| Setting | Default | Purpose |
| --- | --- | --- |
| `BRIGHTNESS_PERCENTAGE` | `10.0` | LED-ring brightness |
| `POWER_BALANCE_DEADBAND_KW` | `0.05` | Balanced-power threshold |
| `TELEMETRY_INTERVAL_MS` | `60000` | Time between telemetry requests |
| `WIFI_WAIT_POLL_MS` | `10000` | How often to check whether Wi-Fi is back |
| `RESET_HOLD_MS` | `3000` | Reset-button hold duration |

Three schedules are tables rather than single values, so the whole policy is
visible at a glance:

| Table | File | Governs |
| --- | --- | --- |
| `s_api_retry_delay_ms` | `main/main.c` | Backoff after a failed API round |
| `s_retry_schedule` | `main/src/wifi_provisioning.c` | Wi-Fi reconnect attempts |
| `s_reset_reason` | `main/main.c` | Which resets count towards the credential wipe |

The Energyboxx token and telemetry URLs, token refresh margin, and response
buffer size are defined in `main/src/energyboxx_api.c`. The provisioning
silence timeout is `PROVISIONING_SILENCE_TIMEOUT_MS` in
`main/src/wifi_provisioning.c`.

`main/inc/secrets.h` is ignored by Git and is not used by the current runtime
provisioning flow. Do not commit real credentials.

## Project structure

```text
main/
├── main.c                    Startup, recovery and telemetry task
├── inc/                      Public component headers
└── src/
    ├── api_storage.c         Energyboxx credentials in NVS
    ├── dns_server.c          Captive-portal DNS redirection
    ├── energyboxx_api.c      OAuth token and telemetry requests
    ├── status_led.c          LED rings and discrete status LEDs
    ├── uri_decode.c          Percent-decoding for form values
    ├── wifi_provisioning.c   Wi-Fi state and provisioning AP
    ├── wifi_storage.c        Wi-Fi credentials in NVS
    └── wifi_web.c            Provisioning web interface

docs/REVIEW.md                Pre-production review and its worklist
test/                         Host tests for the ESP-IDF-free modules
```

## Runtime overview

```text
Boot
  -> initialize NVS and LEDs
  -> load and connect saved Wi-Fi
     -> provision Wi-Fi when unavailable
  -> load and validate API credentials
     -> provision API credentials when unavailable
  -> request telemetry
  -> update the energy LED ring
  -> wait 60 seconds and repeat
```

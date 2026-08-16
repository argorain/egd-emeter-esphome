# EGD/E.ON smart meter → Home Assistant (ESPHome)

Firmware for a [Waveshare ESP32-S3-RS485-CAN](https://www.waveshare.com/esp32-s3-rs485-can.htm) board that
reads the RS485 "HAN" push interface documented in `egd_rs485.pdf` (ZPA AM175 / Meter & Control ST402D /
Sagemcom XT211 meters used by EGD/E.ON Distribuce) and exposes all fields directly as Home Assistant entities
over the native ESPHome API — no MQTT, no polling, no Modbus.

## How it works

This is **not** a Modbus meter. Per the datasheet, the meter pushes a full DLMS/COSEM "data-notification"
telegram over RS485 **once every 60 seconds, one-way** (meter → customer only, 9600 Bd, no request/response).
A custom ESPHome external component (`components/dlms_push_meter/`) listens on the UART, decodes the raw
DLMS/A-XDR bytes generically (structures, OBIS codes, integers, strings), and publishes each value to the
Home Assistant entity matching its OBIS code — whichever fields your specific meter/tariff plan actually
sends.

## Repo layout

- `emeter.yaml` — the **only file you need to give Home Assistant**. It just sets a name and pulls
  everything else from this repo via `packages:`.
- `packages/emeter.yaml` — the real device config (board, UART wiring, entities). Fetched over git at
  compile time, so updates here apply automatically next time you build.
- `components/dlms_push_meter/` — the custom external component (C++ decoder + Python schema), also
  fetched over git at compile time via `external_components:`.
- `egd_rs485.pdf` — the vendor datasheet this firmware was built from.

## What you need

- Waveshare ESP32-S3-RS485-CAN board
- USB-C cable (for the very first flash only — after that, updates are OTA)
- A way to wire the meter's RJ12 port to the board's RS485 screw terminals — either an RJ12 breakout/adapter,
  or an RJ12 cable you cut and wire directly
- The Home Assistant **ESPHome** add-on (or standalone ESPHome + `esphome run`, see below)

## 1. Wiring

The meter's communication port is an **RJ12** socket on the meter body, accessible to the customer:

| RJ12 pin | Signal        | Connect to board |
|----------|---------------|-------------------|
| 3        | Data A        | RS485 **A**       |
| 4        | Data B        | RS485 **B**       |
| 6        | Shield (GND)  | **GND**           |
| 1, 2, 5  | Not connected | —                 |

The board exposes RS485 A/B/GND on its screw terminal block. If the board has a termination jumper (marked
120R), enable it only if this is the far end of a long/noisy cable run — for a short direct wire from meter
to board it's usually not needed.

**Do not wire anything to the board's transmit side or expect two-way communication** — the meter interface
is receive-only by design. The firmware keeps the RS485 driver permanently disabled (GPIO21 held low) so the
board only ever listens.

## 2. Flash the firmware

### Option A — Home Assistant ESPHome add-on (recommended)

1. Open the **ESPHome** add-on / dashboard in Home Assistant.
2. **New device → Continue → Skip** the wizard (or "I already have a YAML file"), name it `emeter`.
3. Replace the generated file's contents with this repo's [`emeter.yaml`](emeter.yaml), **filling in the
   four `CHANGE_ME_...` placeholders** in the `substitutions:` block:
   ```yaml
   substitutions:
     name: emeter
     friendly_name: "Elektroměr"

     wifi_ssid: "your WiFi SSID"
     wifi_password: "your WiFi password"
     # Generate with: python3 -c "import base64, os; print(base64.b64encode(os.urandom(32)).decode())"
     api_encryption_key: "a 32-byte base64 key"
     ota_password: "any strong password"

   packages:
     emeter: github://argorain/egd-emeter-esphome/packages/emeter.yaml@master
   ```
   That's the whole file — no `secrets.yaml`, no separate secrets management. Just don't paste your filled-in
   copy anywhere public (this repo's own `emeter.yaml` keeps the placeholders).
4. Click **Install** → **Plug into this computer** (first flash only, needs USB-C) → pick the serial port.
   ESPHome pulls `packages/emeter.yaml` and `components/dlms_push_meter/` from GitHub automatically at
   build time — nothing else to copy.

From then on, any update pushed to this repo's `master` branch is picked up the next time you build/update
the device from the dashboard (git ref is cached for 1 day — force a refresh from the add-on if you need it
sooner). Re-flashing after the first time is OTA, no more USB needed.

### Option B — standalone ESPHome CLI

```bash
python3 -m venv .venv && source .venv/bin/activate && pip install esphome
# fill in the CHANGE_ME_... placeholders in emeter.yaml's substitutions block first
esphome run emeter.yaml
```

Pick the USB serial port when prompted for the first flash; subsequent runs go over OTA once the device is
online. Logs go out over the native USB port and over the network (API/OTA) — the board's only UART is
dedicated to the meter.

## 3. Add to Home Assistant

If you flashed via the ESPHome add-on, the device is added automatically once it comes online.

Otherwise: if you run the Home Assistant ESPHome integration with mDNS discovery, the device (`emeter.local`)
should be auto-discovered — go to **Settings → Devices & Services** and look for a new ESPHome device, then
enter the `api_encryption_key` you set in `emeter.yaml` when prompted.

Or add it manually: **Settings → Devices & Services → Add Integration → ESPHome**, host `emeter.local`
(or its IP), and paste the encryption key.

You'll get entities for:
- Import/export power (total + per phase) — `W`
- Import/export cumulative energy (total + rate 1–4) — `Wh`
- Power limiter setting — `W`
- Active tariff (e.g. `T1`/`T2`/`T3`), meter serial number, device name, consumer message — text
- Main supply connected + relay 1–6 connected — on/off

Any field your meter doesn't actually push (e.g. unused tariff rates, unused relays) simply stays
`unknown` — the decoder matches by OBIS code, not position, so it's safe to leave every entity enabled.

### Detecting new/unmapped fields (e.g. after a meter firmware update)

Two diagnostic entities cover the reverse case — codes the meter sends that this firmware doesn't recognize:

- **Unknown OBIS Fields** (sensor) — count of OBIS codes in the most recent telegram that matched none of
  the entities above. Normally `0`. Set a Home Assistant automation on this going above `0` to get notified.
- **Unknown OBIS Codes** (text) — the actual codes and values, e.g. `1-0:3.8.0.255=1234; 0-0:96.50.0.255="X"`.

The same detail is also logged at WARN level every cycle a new field appears
(`Unrecognized OBIS in telegram: ...`), so `esphome logs emeter.yaml` works too if you'd rather not add
entities. To wire a genuinely new field into Home Assistant once you've identified it, add its OBIS code to
`components/dlms_push_meter/const.py` and a matching key in the relevant `sensor.py` / `text_sensor.py` /
`binary_sensor.py` platform file, using the existing entries as a template.

Note this only catches *new OBIS codes*. If the meter starts sending a field's value in a data type the
decoder doesn't understand at all (rare — it covers the full standard DLMS type set), that one telegram
fails to parse and is logged as `Unknown A-XDR tag ...` instead; the next telegram a minute later is
unaffected.

## Troubleshooting

- **No entities update at all**: check the ESPHome logs (`esphome logs emeter.yaml` over WiFi, or via the
  Home Assistant device page). You should see a `dlms_push_meter: Decoded N OBIS item(s) from ... byte frame`
  line roughly once a minute. If you see `No data-notification tag found` or `Unknown A-XDR tag`, the meter
  is transmitting but the bytes are being corrupted — verify A/B aren't swapped and the shield/GND is
  connected.
- **Still nothing / garbled**: the datasheet only specifies the baud rate (9600); it defaults to 8 data bits,
  no parity, 1 stop bit in `packages/emeter.yaml`. If logs show consistent garbage, try changing `parity: NONE`
  to `parity: EVEN` in that file's `uart:` block, push, and rebuild.
- **Values look present but wrong**: the datasheet's own sample telegram already reports power/energy in
  base units (W / Wh) with no scaling exponent, and the firmware trusts that as-is. If your meter reports
  differently, note the OBIS code and raw value from the logs and it can be re-scaled per field in
  `components/dlms_push_meter/sensor.py`.
- **Checking exactly what the meter sent, per field**: set the component's log level to `VERBOSE` (either
  globally with `logger: level: VERBOSE`, or scoped with `logger: logs: {dlms_push_meter: VERBOSE}` to avoid
  the noise of verbose logging everywhere else) and rebuild. Every decoded telegram then logs one line per
  OBIS code with its raw parsed value, e.g. `1-0:2.8.0.255 = 38543`, so you can confirm a suspicious reading
  is really what the meter transmitted rather than a firmware decoding issue.


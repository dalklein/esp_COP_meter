## ESP8266 COP HeatMeter

A low-cost Coefficient-Of-Performance meter for an air-to-water heat pump,
built around an ESP8266 NodeMCU v2 with an SSD1306 OLED (Ideaspark
"NodeMCU+OLED" board, roughly $7 assembled).

The meter measures hydronic-loop flow and cross-exchanger temperatures,
pulls the heat pump's electrical consumption from existing MQTT sources,
derives instantaneous and cumulative COP, and publishes everything under
the `HPEM/` MQTT topic tree for Node-RED / InfluxDB / Grafana consumption.

More discussion about the project and the heat-pump system:
<https://www.reddit.com/r/DIYHeatPumps/comments/189mlyy/diy_airtowater_cop_measurement/>

### Repository layout

- `ESP_HeatMeter/` — **current working firmware**. Also serves as a
  local snapshot of the code running (or staged to run) on the device.
  - `ESP_HeatMeter.ino` — active sketch
  - `ESP_HeatMeter (v2).not_ino` — earlier experimental revision, kept
    alongside for reference
  - `arduino_secrets.h` — **not tracked** (in `.gitignore`); contains
    Wi-Fi SSID/password and MQTT user/pass. Create your own from the
    `#define` skeleton in the sketch.
- `sketch14_012424_sCOP/` — prior generation sketch, left in place for
  history / diffing.
- `old_setting_up_sketches/` — incremental learning sketches from
  bring-up; ignored in `.gitignore`.
- `setup_notes_8266-ssd1306_OLED.txt`, `pinout_setup_...odt` — pin-out
  notes and wiring reference for the Ideaspark NodeMCU+OLED board.

### Hardware

- ESP8266 NodeMCU v2 + SSD1306 128×64 OLED (`u8g2` library, I2C)
- Hall-effect turbine flow meter on the hydronic loop (interrupt-driven
  pulse counter, calibrated in LPM)
- DS18B20 1-Wire temperature probes — up to ~12 channels, labelled for
  process use:
  - `EWT`, `LWT` — entering / leaving water temps across the refrigerant
    heat exchanger (used for heat-output calculation)
  - `LIQ`, `VAP` — refrigerant liquid / vapor line temps
  - `RET_Bd`, `RET_HW`, `RET_Bsmt`, `RET_N`, `RET_S` — zone return-water
    probes around the house
  - `LAT`, `EAT` — leaving / entering air temps on the duct coil
- OLED splash shows the firmware version string (currently `v041827`)

### Computed quantities

- `heatQkW` — instantaneous heat output = flow × ΔT(EWT − LWT) × ρ·cp
- `heatEWh` — running heat energy, integrated from heatQkW
- `elecPkW` — instantaneous electrical input (see "Dual power source"
  below)
- `elecEWh` — running electrical energy (mixed integration /
  metered-delta, see below)
- `COP` — instantaneous heatQkW / elecPkW, gated by a minimum flow
  threshold (`minFlowLPM = 2.0`) to suppress noise when the pump is off
- `cCOP` — cumulative COP over the current run
- `sCOP` — seasonal COP, accumulated across runs
- `heat_sEQkWh`, `elec_sEkWh` — seasonal heat and electrical energy
  totals

### MQTT interface

Published under `HPEM/`:

```
heatQkW  heatEWh  COP  cCOP  sCOP  heat_sEQkWh  elec_sEkWh
elecPkW  elecEWh  ePwrSrc  flowLPM
EWT  LWT  VAP  LIQ  deltaT
RET_Bd  RET_HW  RET_Bsmt  RET_N  RET_S
LAT  EAT
debug    (per-iteration diagnostic trace — see below)
```

Subscribed:

```
gridvue/power/sensor/ashpgrid/state   — grid-side ASHP watts (ESPHome gridvue)
emporia/ashp_og/power                 — off-grid ASHP watts (Emporia Vue via HA)
emporia/ashp_og/energy_today          — off-grid cumulative kWh for the day
```

### Dual electrical power source

The heat pump can run either from the grid or from the off-grid
battery/inverter system depending on a transfer switch. Two upstream
monitors each see only their own side:

- **Grid feed**: an Emporia Vue-2 monitor flashed with ESPHome publishes
  a ~5-second `gridvue/power/sensor/ashpgrid/state` reading in watts.
- **Off-grid feed**: a separate Emporia Vue (cloud-integrated via Home
  Assistant) exposes circuit-level power and daily-energy sensors,
  republished to MQTT by HA automations.

The sketch arbitrates per-message: whichever source reports the larger
power is treated as "active" (`ePwrSource` = 1 grid / 2 off-grid /
0 idle, threshold 5 W). Only one side carries current at a time in
practice, so the larger value is the true consumption.

#### Energy accounting by source

- **On-grid**: the ESPHome feed updates at ~5 s cadence, so elecEWh is
  integrated from `elecPkW × Δt`.
- **Off-grid**: HA's Emporia Vue integration publishes `energy_today`
  (cumulative kWh) at a coarser cadence. elecEWh is advanced by the
  metered delta between consecutive `energy_today` messages, and
  `elecPkW` is *derived* from `Δenergy / Δt` rather than integrated.

This hybrid approach avoids double-counting and keeps daily totals
aligned with whichever meter actually saw the current.

#### Emporia-cloud glitch guard (v041827)

`sensor.ashp_og_energy_today` is a `total_increasing` counter fed from
polled cloud data. The cloud occasionally reports an oversized
cumulative jump that self-corrects on the next poll — e.g. on
2026-04-18 at 14:08 EDT the counter stepped +205 Wh in 60 s while the
averaged power held at ~1960 W, producing a bogus 12.3 kW "spike" on
the derived power. The 20 kW sanity cap that previously guarded
`derivedPkW` was too loose to catch this.

The sketch now cross-checks `derivedPkW` against `ogW` (the averaged
power from `emporia/ashp_og/power`): when `ogW > 100 W` and
`derivedPkW > 2 × ogW`, the delta is declared a glitch, and
`ogDeltaWh` is reconstructed as `ogW × Δt`. Both the published
`elecPkW` and the `elecEWh` accumulator are fed the reconstructed
value, so neither the power plot nor the daily-energy total inherits
the artifact. The `HPEM/debug` topic emits `dPkW`, `ogW`, and a
`GLITCH` marker when the guard fires, so these events are visible
after the fact without needing serial capture.

### Debug topic

`HPEM/debug` publishes a short trace on every electrical-side
iteration. Two formats:

- On-grid integration: `GR add=<Wh-added> PkW=<kW> dt=<ms> EWh=<total>`
- Off-grid consume: `OG dWh=<delta-Wh> EWh=<total> dtOg=<ms>
  dPkW=<derived kW> ogW=<avg-W>[ GLITCH]`

These are intentionally noisy while the dual-power accounting is still
being validated in the field. They'll be removed in a future flash
once the numbers hold steady.

### Known limitations

- **IRAM headroom**: the build sits at 93 % IRAM with "Basic SSL
  ciphers" selected. Any new `IRAM_ATTR` / ISR code risks the link
  overflowing. Flash RAM and main RAM have plenty of room.
- **OTA works.** ~~At 93 % IRAM the Wi-Fi / SSL / OTA stacks coexist
  only marginally; prefer USB flashing.~~ **Corrected 2026-08-04.**
  That claim was wrong, and blaming IRAM was a misattribution: IRAM has
  been at 93 % since the OTA code was first added in Nov 2023, and the
  setup notes from that moment record it *working* ("but it works",
  then two days later "it's really nice to update remotely!"), as does
  the Mar 2024 entry "Uploaded ota with arduino IDE". A constant cannot
  explain a change in behaviour.

  The real cause was the **host firewall**. ArduinoOTA is a *callback*
  protocol: the workstation opens a listener, invites the ESP, and the
  **ESP connects back** to fetch the image. The laptop runs ufw with
  `default deny (incoming)`, so the callback was silently dropped and
  the push timed out looking exactly like an unresponsive device.
  Verified 2026-08-04: with a hole opened for `192.168.15.43`, a 342 KB
  image uploaded first try in seconds.

  Use `~/.local/bin/cop_ota <firmware.bin>`. It pins the host callback
  port to 18266 (espota's default is random 10000-60000, which cannot be
  firewalled narrowly), opens ufw for `.43` only, closes it in a
  `finally`, and passes the OTA password to espota's `serve()`
  in-process so it never lands in `ps`.
- **OTA now requires a password** (added 2026-08-04). Before that,
  `auth_upload=no` — any host on the LAN could push arbitrary firmware
  to a device wired into the heating system. The password lives in
  KeePassXC as `Device/cop_heatmeter_ota` and reaches the build via
  `OTA_PASS` in `arduino_secrets.h`.
- **SSL ciphers**: the Arduino IDE's SSL-cipher dropdown does not
  persist between sessions, so double-check "Basic SSL ciphers" before
  every GUI compile. This does **not** apply to command-line builds —
  the FQBN default already selects `ssl_basic`:

      /usr/bin/arduino --verify --board esp8266:esp8266:nodemcuv2 \
        --pref build.path=<dir> ESP_HeatMeter.ino

### Ecosystem

Data flows into the household IOTstack (Mosquitto → Node-RED →
InfluxDB → Grafana). The "Heat Pump" and "Heat Pump cCOP sCOP"
Grafana dashboards plot these topics directly; the Garage Dash plots
`HPEM_elecPkW` as the `ASHP_kw` series alongside grid / off-grid
totals.

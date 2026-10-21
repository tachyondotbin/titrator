# Syringe-Pump Titrator with pH Measurement

Arduino-IDE (STM32duino) firmware for an automatic syringe-pump titrator on
**STM32G431CBU6**. Dispenses titrant in controlled volumes, measures pH, runs
endpoint and full-curve titrations, and streams CSV data to a PC over USB.

## Hardware

```
pH electrode --> LMP91200 (pH AFE) --> ADS1256 AIN0 --SPI1--> STM32G431CBU6
TMC2209 (STEP/DIR) --> syringe lead screw     | 2 limit switches = EMPTY / FULL
Servo --> 3-way fill valve (titrant / dispense / rinse)
```

### Default pin map (edit in `config.h`)

| Function            | Pin  | Notes |
|---------------------|------|-------|
| TMC2209 STEP        | PA0  | |
| TMC2209 DIR         | PA1  | flip `DIR_DISPENSE_SIGN` if wrong way |
| TMC2209 EN          | PA8  | active LOW |
| Limit EMPTY (home)  | PA3  | to GND, internal pull-up |
| Limit FULL          | PA4  | to GND, internal pull-up |
| Servo valve         | PA2  | |
| ADS1256 SCK         | PA5  | SPI1 |
| ADS1256 DOUT (MISO) | PA6  | SPI1 |
| ADS1256 DIN  (MOSI) | PA7  | SPI1 |
| ADS1256 CS          | PB0  | |
| ADS1256 DRDY        | PB1  | active LOW |
| ADS1256 RESET       | PB2  | optional |

> Microstepping is set by the TMC2209 **MS1/MS2 pins in hardware** — set
> `MICROSTEPS` in `config.h` to match. ADS1256 `PGA` = 2 gives ±2.5 V range,
> 4 gives ±1.25 V (with `VREF` = 2.5 V).

### ADS1256 pH input options (`config.h`)

- **Single-ended (default)** — `ADS_PH_DIFFERENTIAL 0`, reads `ADS_PH_CHANNEL`
  (AIN0) vs AINCOM.
- **Differential** — set `ADS_PH_DIFFERENTIAL 1` to read
  `ADS_PH_CHANNEL` (+) − `ADS_PH_NEG_CHANNEL` (−), e.g. AIN0−AIN1. Rejects
  common-mode noise; use when the probe has a separate reference lead.
- **Data rate** — `ADS_DATARATE` (e.g. `ADS_DR_2_5SPS` … `ADS_DR_30000SPS`).
  Lower = lower noise / higher ENOB / slower. For a slow pH signal,
  `ADS_DR_2_5SPS`..`ADS_DR_10SPS` is ideal. (Read timeouts already cover the
  slow rates.) Use `ph_debug` to find the best PGA/channel/rate first.

## Build (Arduino IDE)

- Board: **Generic STM32G4 series → STM32G431CBU6**
- USB support: **CDC (generic 'Serial' supersede U(S)ART)**
- Upload: STM32CubeProgrammer (SWD) or DFU
- Libraries: `AccelStepper`, `Servo`, `EEPROM` (bundled with STM32 core)

Open the `titrator` folder (all `.ino`/`.cpp`/`.h` compile together) and upload.

## Calibration before first real run

1. Set the mechanical constants in `config.h` (`SYRINGE_VOLUME_UL`,
   `SYRINGE_STROKE_MM`, `LEADSCREW_PITCH_MM`, `STEPS_PER_REV`, `MICROSTEPS`).
2. `HOME`, then dispense a known volume and verify against measured travel.
3. pH 2-point: place probe in buffer 1.65 → `CAL1 1.65`; buffer 9.18 →
   `CAL2 9.18`; then `CALSAVE` (stored in emulated EEPROM, reloaded at boot).

## Serial commands (115200 baud, newline-terminated)

| Command | Meaning |
|---------|---------|
| `PING` | → `PONG` |
| `STATUS` | state, syringe volume, valve, switches, cal |
| `HOME` | home to EMPTY switch, zero position |
| `FILL [ul]` | valve→FILL, aspirate (no arg = fill to FULL switch) |
| `DISPENSE <ul>` | valve→DISPENSE, expel volume |
| `VALVE <DISPENSE\|FILL\|RINSE>` | move valve |
| `READ` | pH, mV, raw volts |
| `CAL1 <pH>` / `CAL2 <pH>` | capture cal point at current reading |
| `CALSAVE` | persist calibration |
| `TITRATE EP <targetPH> [dose_ul] [tol] [max_ul]` | dose to endpoint pH (e.g. `TITRATE EP 1.0` or `TITRATE EP 12.0`); stops on target, overshoot, max_ul, or stall |
| `TITRATE CURVE <step_ul> <max_ul> [settle_ms]` | full curve, reports equivalence |
| `STOP` | abort current operation |
| `RESET` | abort + return to IDLE (re-HOME before dosing) |

Titration streams CSV rows: `t_ms,total_ul,pH,mV,phase`.

## PC software

**GUI (recommended)** — `gui/` holds a PyQt6 desktop app: full control panel,
live readout, and live plots of pH & mV, dpH/dmL & dmV/dmL, and d²pH/dmL² vs mL,
with equivalence-point detection and CSV save/reload. See `gui/README.md`.

```
cd gui && pip install -r requirements.txt && python titrator_gui.py
```

**Headless logger** — `tools/titrator_log.py` just tee's the serial stream to a
timestamped CSV (no GUI deps beyond pyserial):

```
pip install pyserial
python tools/titrator_log.py --list                       # find the port
python tools/titrator_log.py -p COM5 -c "TITRATE CURVE 50 500 3000"
python tools/titrator_log.py -p /dev/ttyACM0 -i           # interactive
```

Both run on Windows / Linux / macOS. The GUI's `titrator_core.py` (serial +
analysis) is GUI-agnostic, so a web dashboard can reuse it later.

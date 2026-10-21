# Titrator GUI

PyQt6 desktop app to control the STM32 titrator over USB serial and plot
titration curves live (and reload saved runs).

## Install

```
cd gui
python -m venv .venv
# Windows: .venv\Scripts\activate    Linux/mac: source .venv/bin/activate
pip install -r requirements.txt
```

## Run

```
python titrator_gui.py
```

1. Pick the serial port, **Connect**.
2. **HOME** the syringe, then **FILL**.
3. Calibrate pH: put probe in buffer 1.65 → **CAL1**, buffer 9.18 → **CAL2**,
   then **CALSAVE**.
4. Choose **Endpoint** or **Curve**, set fields, **Start titration**.
   - Endpoint to pH 1 or pH 12: set *EP target pH* = `1.0` or `12.0`. A
     *max uL* safety bound and stall detection stop a runaway dose.
5. Watch the live plots; **Save CSV** when done. **Open CSV** reloads any saved
   run or raw device log for offline analysis.

## Plots (all vs volume in mL)

| Plot | Curves |
|------|--------|
| 1 | pH (left axis) & mV (right axis) |
| 2 | dpH/dmL (left) & dmV/dmL (right) |
| 3 | d²pH/dmL² |

Equivalence point (max \|dpH/dmL\|) is marked with a dashed line and shown in
the **Live** panel. The *Smooth window* spin box applies a moving-average to
the derivatives to tame noise (odd window, 1 = off).

**Plot navigation**: drag to pan, right-drag / scroll to zoom, and
**double-click any plot to reset all axes to auto-range** (fit the data). Plots
auto-fit live data until you interact; double-click returns to auto-fit.

The control panel sits in a scroll area, so on short windows it scrolls instead
of squeezing the Titration controls out of view.

## Architecture

- `titrator_core.py` — framework-agnostic: `SerialLink` (threaded serial I/O),
  `TitrationData` (CSV parse, derivatives, equivalence, save/load), and command
  builders. **No GUI dependency**, so a web dashboard (Flask/Plotly) can reuse
  it later without duplicating serial or analysis logic.
- `titrator_gui.py` — PyQt6 + pyqtgraph front-end on top of the core.

## Notes

- pH 1 / pH 12 are **extrapolated** beyond the 1.65–9.18 calibration buffers;
  endpoint detection still works but absolute accuracy degrades at the extremes.
  For best accuracy near an extreme, calibrate with a buffer close to it.
- The GUI parses the same `t_ms,total_ul,pH,mV,phase` stream the firmware emits;
  volume is converted µL → mL for display.

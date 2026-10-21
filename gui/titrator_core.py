"""
titrator_core.py - framework-agnostic core for the titrator GUI.

Two reusable pieces, no GUI dependency (only pyserial + numpy):

  SerialLink     - threaded serial reader/writer for the STM32 titrator.
                   Lines arriving from the device are pushed to a thread-safe
                   queue; commands are written with write().

  TitrationData  - accumulates titration CSV rows, computes derivatives
                   (dpH/dmL, dmV/dmL, d2pH/dmL2) and the equivalence point,
                   and saves/loads CSV files.

A PyQt6 GUI, a web dashboard, or a plain script can all sit on top of these.
"""
from __future__ import annotations

import csv
import queue
import threading
from dataclasses import dataclass, field
from typing import Optional

import numpy as np

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    serial = None
    list_ports = None


# ----------------------------------------------------------------------------
# Serial link
# ----------------------------------------------------------------------------
def available_ports() -> list[tuple[str, str]]:
    """Return [(device, description), ...] for all serial ports."""
    if list_ports is None:
        return []
    return [(p.device, p.description) for p in list_ports.comports()]


class SerialLink:
    """Background-thread serial reader/writer.

    Incoming lines (stripped) land in `.rx` (a queue.Queue). Call drain() from
    the GUI thread to pull whatever has arrived without blocking.
    """

    def __init__(self) -> None:
        self._ser: Optional["serial.Serial"] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self.rx: "queue.Queue[str]" = queue.Queue()

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def open(self, port: str, baud: int = 115200) -> None:
        if serial is None:
            raise RuntimeError("pyserial not installed (pip install pyserial)")
        self.close()
        self._ser = serial.Serial(port, baud, timeout=0.2)
        self._stop.clear()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1.0)
            self._thread = None
        if self._ser:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None

    def write(self, command: str) -> None:
        """Send one command line (newline appended)."""
        if not self.is_open:
            raise RuntimeError("serial port not open")
        self._ser.write((command.strip() + "\n").encode())

    def drain(self) -> list[str]:
        """Non-blocking: return all lines received since the last call."""
        out: list[str] = []
        while True:
            try:
                out.append(self.rx.get_nowait())
            except queue.Empty:
                break
        return out

    def _reader(self) -> None:
        buf = b""
        while not self._stop.is_set() and self._ser:
            try:
                chunk = self._ser.read(256)
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").strip("\r\n ")
                if text:
                    self.rx.put(text)


# ----------------------------------------------------------------------------
# Titration data + analysis
# ----------------------------------------------------------------------------
@dataclass
class TitrationData:
    """Holds one titration run and derived curves. Volume stored in mL."""

    t_ms: list[float] = field(default_factory=list)
    vol_ml: list[float] = field(default_factory=list)
    ph: list[float] = field(default_factory=list)
    mv: list[float] = field(default_factory=list)
    phase: list[str] = field(default_factory=list)

    smooth_window: int = 1  # odd >=1; 1 disables smoothing

    # --- ingestion -----------------------------------------------------------
    def clear(self) -> None:
        self.t_ms.clear(); self.vol_ml.clear(); self.ph.clear()
        self.mv.clear(); self.phase.clear()

    def parse_line(self, line: str) -> bool:
        """Try to add a CSV data row 't_ms,total_ul,pH,mV,phase'.

        Returns True if the line was a data row (not OK/ERR/#/banner text).
        """
        if not line or line[0] in "#" or not line[0].isdigit():
            return False
        parts = line.split(",")
        if len(parts) < 5:
            return False
        try:
            t = float(parts[0])
            ul = float(parts[1])
            ph = float(parts[2])
            mv = float(parts[3])
        except ValueError:
            return False
        self.t_ms.append(t)
        self.vol_ml.append(ul / 1000.0)   # uL -> mL
        self.ph.append(ph)
        self.mv.append(mv)
        self.phase.append(parts[4].strip())
        return True

    def __len__(self) -> int:
        return len(self.vol_ml)

    # --- analysis ------------------------------------------------------------
    def _arr(self) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        return (np.asarray(self.vol_ml, float),
                np.asarray(self.ph, float),
                np.asarray(self.mv, float))

    def _smooth(self, y: np.ndarray) -> np.ndarray:
        w = self.smooth_window
        if w <= 1 or y.size < w:
            return y
        if w % 2 == 0:
            w += 1
        pad = w // 2
        # edge-pad so the moving average doesn't introduce zero-pad artifacts
        # at the ends (which would otherwise move the derivative peak).
        yp = np.pad(y, pad, mode="edge")
        kernel = np.ones(w) / w
        return np.convolve(yp, kernel, mode="valid")

    def derivative(self, y: np.ndarray, v: np.ndarray) -> np.ndarray:
        """d(y)/d(v) using non-uniform gradient; smoothed if enabled."""
        if v.size < 2:
            return np.zeros_like(v)
        # de-duplicate volume to avoid divide-by-zero in gradient
        d = np.gradient(self._smooth(y), v)
        return d

    def dph_dml(self):
        v, ph, _ = self._arr()
        return v, self.derivative(ph, v)

    def dmv_dml(self):
        v, _, mv = self._arr()
        return v, self.derivative(mv, v)

    def d2ph_dml2(self):
        v, ph, _ = self._arr()
        if v.size < 3:
            return v, np.zeros_like(v)
        first = self.derivative(ph, v)
        second = np.gradient(first, v)
        return v, second

    def equivalence_volume(self) -> Optional[float]:
        """Volume (mL) at the steepest |dpH/dmL| - the equivalence point."""
        v, d = self.dph_dml()
        if v.size < 2:
            return None
        return float(v[int(np.argmax(np.abs(d)))])

    # --- persistence ---------------------------------------------------------
    def save_csv(self, path: str) -> None:
        with open(path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["t_ms", "volume_mL", "pH", "mV", "phase"])
            for i in range(len(self)):
                w.writerow([self.t_ms[i], self.vol_ml[i], self.ph[i],
                            self.mv[i], self.phase[i]])

    def load_csv(self, path: str) -> None:
        """Load a CSV saved by this class OR a raw device log.

        Accepts columns volume_mL (mL) or total_ul (uL).
        """
        self.clear()
        with open(path, newline="", encoding="utf-8") as f:
            reader = csv.reader(f)
            header = next(reader, None)
            if header is None:
                return
            cols = [h.strip().lower() for h in header]

            def idx(*names):
                for n in names:
                    if n in cols:
                        return cols.index(n)
                return None

            i_t = idx("t_ms", "time", "host_time")
            i_ml = idx("volume_ml")
            i_ul = idx("total_ul", "volume_ul")
            i_ph = idx("ph")
            i_mv = idx("mv")
            i_ph_ = idx("phase")
            for row in reader:
                if not row or (i_ph is not None and i_ph < len(row)
                               and not _is_float(row[i_ph])):
                    continue
                try:
                    t = float(row[i_t]) if i_t is not None else 0.0
                    if i_ml is not None:
                        vol = float(row[i_ml])
                    elif i_ul is not None:
                        vol = float(row[i_ul]) / 1000.0
                    else:
                        continue
                    ph = float(row[i_ph]) if i_ph is not None else 0.0
                    mv = float(row[i_mv]) if i_mv is not None else 0.0
                except (ValueError, IndexError):
                    continue
                self.t_ms.append(t)
                self.vol_ml.append(vol)
                self.ph.append(ph)
                self.mv.append(mv)
                self.phase.append(row[i_ph_] if i_ph_ is not None
                                  and i_ph_ < len(row) else "")


def _is_float(s: str) -> bool:
    try:
        float(s)
        return True
    except ValueError:
        return False


# ----------------------------------------------------------------------------
# Command builders (single source of truth for the serial protocol)
# ----------------------------------------------------------------------------
def cmd_titrate_ep(target_ph: float, dose_ul: float = 0, tol: float = 0,
                   max_ul: float = 0) -> str:
    parts = ["TITRATE", "EP", f"{target_ph:g}"]
    if dose_ul: parts.append(f"{dose_ul:g}")
    if tol:     parts.append(f"{tol:g}")
    if max_ul:  parts.append(f"{max_ul:g}")
    return " ".join(parts)


def cmd_titrate_curve(step_ul: float, max_ul: float,
                      settle_ms: float = 0) -> str:
    parts = ["TITRATE", "CURVE", f"{step_ul:g}", f"{max_ul:g}"]
    if settle_ms:
        parts.append(f"{int(settle_ms)}")
    return " ".join(parts)

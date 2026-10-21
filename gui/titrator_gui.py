#!/usr/bin/env python3
"""
titrator_gui.py - PyQt6 desktop control + live plotting for the STM32 titrator.

Features
  * Serial connect (port picker), full control panel:
      HOME / FILL / DISPENSE / VALVE / READ / CAL1 / CAL2 / CALSAVE
      TITRATE EP (target pH incl. 1..12) / TITRATE CURVE / STOP
  * Live readout: pH, mV, total mL, device state.
  * Four live plots vs volume (mL):
      1. pH  &  mV
      2. dpH/dmL  &  dmV/dmL
      3. d2pH/dmL2
      (equivalence point marked at max |dpH/dmL|)
  * Save current run to CSV; reload any saved CSV / device log for offline view.

Run:  python titrator_gui.py
Deps: PyQt6, pyqtgraph, numpy, pyserial   (see requirements.txt)
"""
from __future__ import annotations

import sys

import numpy as np
import pyqtgraph as pg
from PyQt6 import QtCore, QtWidgets

from titrator_core import (SerialLink, TitrationData, available_ports,
                           cmd_titrate_ep, cmd_titrate_curve)

pg.setConfigOptions(antialias=True, background="w", foreground="k")

PEN_PH   = pg.mkPen("#1f77b4", width=2)
PEN_MV   = pg.mkPen("#d62728", width=2)
PEN_DPH  = pg.mkPen("#2ca02c", width=2)
PEN_DMV  = pg.mkPen("#ff7f0e", width=2)
PEN_D2   = pg.mkPen("#9467bd", width=2)
PEN_EQ   = pg.mkPen("#888888", width=1, style=QtCore.Qt.PenStyle.DashLine)


class TwinPlot:
    """A pyqtgraph PlotItem with a second, right-hand Y axis sharing X."""

    def __init__(self, plot: pg.PlotItem, left_label, right_label,
                 left_pen, right_pen, bottom_label="Volume (mL)"):
        self.plot = plot
        plot.setLabel("bottom", bottom_label)
        plot.setLabel("left", left_label, color=left_pen.color().name())
        plot.showGrid(x=True, y=True, alpha=0.3)
        plot.addLegend(offset=(10, 10))

        self.left_curve = plot.plot(pen=left_pen, name=left_label)

        self.vb_right = pg.ViewBox()
        plot.showAxis("right")
        plot.scene().addItem(self.vb_right)
        plot.getAxis("right").linkToView(self.vb_right)
        plot.getAxis("right").setLabel(right_label, color=right_pen.color().name())
        self.vb_right.setXLink(plot)
        self.vb_right.enableAutoRange(axis=pg.ViewBox.YAxis, enable=True)
        self.right_curve = pg.PlotDataItem(pen=right_pen, name=right_label)
        self.vb_right.addItem(self.right_curve)

        plot.vb.sigResized.connect(self._sync)
        self._sync()

    def _sync(self):
        self.vb_right.setGeometry(self.plot.vb.sceneBoundingRect())
        self.vb_right.linkedViewChanged(self.plot.vb, self.vb_right.XAxis)

    def set_data(self, x, y_left, y_right):
        self.left_curve.setData(x, y_left)
        self.right_curve.setData(x, y_right)

    def autorange(self):
        self.plot.enableAutoRange(x=True, y=True)
        self.vb_right.enableAutoRange(x=True, y=True)


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("STM32 Titrator - pH / mL")
        self.resize(1280, 820)

        self.link = SerialLink()
        self.data = TitrationData()
        self.last_ph = float("nan")
        self.last_mv = float("nan")
        self.state = "?"

        self._build_ui()

        self.timer = QtCore.QTimer(self)
        self.timer.setInterval(100)  # 10 Hz
        self.timer.timeout.connect(self._tick)
        self.timer.start()

    # ---------------------------------------------------------------- UI
    def _build_ui(self):
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        root = QtWidgets.QHBoxLayout(central)

        # Controls live inside a scroll area so they keep their natural size and
        # scroll (never squeeze/clip) when the window is short, and stay a fixed
        # width when it is wide.
        scroll = QtWidgets.QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setWidget(self._build_controls())
        scroll.setFixedWidth(360)
        scroll.setSizePolicy(QtWidgets.QSizePolicy.Policy.Fixed,
                             QtWidgets.QSizePolicy.Policy.Expanding)

        root.addWidget(scroll, 0)
        root.addWidget(self._build_plots(), 1)

    def _build_controls(self) -> QtWidgets.QWidget:
        panel = QtWidgets.QWidget()
        panel.setMinimumWidth(330)
        v = QtWidgets.QVBoxLayout(panel)

        # --- connection ---
        g = QtWidgets.QGroupBox("Connection")
        gl = QtWidgets.QGridLayout(g)
        self.port_combo = QtWidgets.QComboBox()
        self._refresh_ports()
        btn_refresh = QtWidgets.QPushButton("↻")
        btn_refresh.setFixedWidth(32)
        btn_refresh.clicked.connect(self._refresh_ports)
        self.btn_connect = QtWidgets.QPushButton("Connect")
        self.btn_connect.clicked.connect(self._toggle_connect)
        gl.addWidget(QtWidgets.QLabel("Port"), 0, 0)
        gl.addWidget(self.port_combo, 0, 1)
        gl.addWidget(btn_refresh, 0, 2)
        gl.addWidget(self.btn_connect, 1, 0, 1, 3)
        v.addWidget(g)

        # --- live readout ---
        g = QtWidgets.QGroupBox("Live")
        gl = QtWidgets.QGridLayout(g)
        self.lbl_ph = QtWidgets.QLabel("-")
        self.lbl_mv = QtWidgets.QLabel("-")
        self.lbl_ml = QtWidgets.QLabel("-")
        self.lbl_state = QtWidgets.QLabel("?")
        self.lbl_eq = QtWidgets.QLabel("-")
        for lbl in (self.lbl_ph, self.lbl_mv):
            lbl.setStyleSheet("font-size:20px; font-weight:bold;")
        gl.addWidget(QtWidgets.QLabel("pH"), 0, 0);   gl.addWidget(self.lbl_ph, 0, 1)
        gl.addWidget(QtWidgets.QLabel("mV"), 1, 0);   gl.addWidget(self.lbl_mv, 1, 1)
        gl.addWidget(QtWidgets.QLabel("mL"), 2, 0);   gl.addWidget(self.lbl_ml, 2, 1)
        gl.addWidget(QtWidgets.QLabel("state"), 3, 0); gl.addWidget(self.lbl_state, 3, 1)
        gl.addWidget(QtWidgets.QLabel("eq. mL"), 4, 0); gl.addWidget(self.lbl_eq, 4, 1)
        v.addWidget(g)

        # --- manual control ---
        g = QtWidgets.QGroupBox("Manual")
        gl = QtWidgets.QGridLayout(g)
        b_home = QtWidgets.QPushButton("HOME"); b_home.clicked.connect(lambda: self.send("HOME"))
        b_read = QtWidgets.QPushButton("READ"); b_read.clicked.connect(lambda: self.send("READ"))
        self.fill_ul = QtWidgets.QLineEdit(); self.fill_ul.setPlaceholderText("uL (blank=full)")
        b_fill = QtWidgets.QPushButton("FILL")
        b_fill.clicked.connect(lambda: self.send("FILL" + (f" {self.fill_ul.text()}" if self.fill_ul.text().strip() else "")))
        self.disp_ul = QtWidgets.QLineEdit(); self.disp_ul.setPlaceholderText("uL")
        b_disp = QtWidgets.QPushButton("DISPENSE")
        b_disp.clicked.connect(lambda: self.send(f"DISPENSE {self.disp_ul.text().strip()}"))
        self.valve_combo = QtWidgets.QComboBox(); self.valve_combo.addItems(["DISPENSE", "FILL", "RINSE"])
        b_valve = QtWidgets.QPushButton("VALVE")
        b_valve.clicked.connect(lambda: self.send(f"VALVE {self.valve_combo.currentText()}"))
        gl.addWidget(b_home, 0, 0); gl.addWidget(b_read, 0, 1)
        gl.addWidget(self.fill_ul, 1, 0); gl.addWidget(b_fill, 1, 1)
        gl.addWidget(self.disp_ul, 2, 0); gl.addWidget(b_disp, 2, 1)
        gl.addWidget(self.valve_combo, 3, 0); gl.addWidget(b_valve, 3, 1)
        v.addWidget(g)

        # --- calibration ---
        g = QtWidgets.QGroupBox("pH calibration (2-point)")
        gl = QtWidgets.QGridLayout(g)
        self.cal1_ph = QtWidgets.QLineEdit("1.65")
        self.cal2_ph = QtWidgets.QLineEdit("9.18")
        b_cal1 = QtWidgets.QPushButton("CAL1"); b_cal1.clicked.connect(lambda: self.send(f"CAL1 {self.cal1_ph.text().strip()}"))
        b_cal2 = QtWidgets.QPushButton("CAL2"); b_cal2.clicked.connect(lambda: self.send(f"CAL2 {self.cal2_ph.text().strip()}"))
        b_save = QtWidgets.QPushButton("CALSAVE"); b_save.clicked.connect(lambda: self.send("CALSAVE"))
        gl.addWidget(self.cal1_ph, 0, 0); gl.addWidget(b_cal1, 0, 1)
        gl.addWidget(self.cal2_ph, 1, 0); gl.addWidget(b_cal2, 1, 1)
        gl.addWidget(b_save, 2, 0, 1, 2)
        v.addWidget(g)

        # --- titration ---
        g = QtWidgets.QGroupBox("Titration")
        gl = QtWidgets.QGridLayout(g)
        self.mode_ep = QtWidgets.QRadioButton("Endpoint"); self.mode_ep.setChecked(True)
        self.mode_curve = QtWidgets.QRadioButton("Curve")
        gl.addWidget(self.mode_ep, 0, 0); gl.addWidget(self.mode_curve, 0, 1)
        # EP fields
        self.ep_target = QtWidgets.QLineEdit("7.0")
        self.ep_dose = QtWidgets.QLineEdit("25")
        self.ep_tol = QtWidgets.QLineEdit("0.05")
        self.ep_max = QtWidgets.QLineEdit("2000")
        gl.addWidget(QtWidgets.QLabel("EP target pH"), 1, 0); gl.addWidget(self.ep_target, 1, 1)
        gl.addWidget(QtWidgets.QLabel("dose uL"), 2, 0);      gl.addWidget(self.ep_dose, 2, 1)
        gl.addWidget(QtWidgets.QLabel("tol pH"), 3, 0);       gl.addWidget(self.ep_tol, 3, 1)
        gl.addWidget(QtWidgets.QLabel("max uL"), 4, 0);       gl.addWidget(self.ep_max, 4, 1)
        # CURVE fields
        self.cv_step = QtWidgets.QLineEdit("50")
        self.cv_max = QtWidgets.QLineEdit("500")
        self.cv_settle = QtWidgets.QLineEdit("3000")
        gl.addWidget(QtWidgets.QLabel("step uL"), 5, 0);   gl.addWidget(self.cv_step, 5, 1)
        gl.addWidget(QtWidgets.QLabel("max uL"), 6, 0);    gl.addWidget(self.cv_max, 6, 1)
        gl.addWidget(QtWidgets.QLabel("settle ms"), 7, 0); gl.addWidget(self.cv_settle, 7, 1)
        b_start = QtWidgets.QPushButton("Start titration"); b_start.clicked.connect(self._start_titration)
        b_stop = QtWidgets.QPushButton("STOP"); b_stop.setStyleSheet("background:#d62728;color:white;")
        b_stop.clicked.connect(lambda: self.send("STOP"))
        gl.addWidget(b_start, 8, 0); gl.addWidget(b_stop, 8, 1)
        v.addWidget(g)

        # --- data / files ---
        g = QtWidgets.QGroupBox("Data")
        gl = QtWidgets.QGridLayout(g)
        b_new = QtWidgets.QPushButton("New run (clear)"); b_new.clicked.connect(self._new_run)
        b_savecsv = QtWidgets.QPushButton("Save CSV"); b_savecsv.clicked.connect(self._save_csv)
        b_open = QtWidgets.QPushButton("Open CSV"); b_open.clicked.connect(self._open_csv)
        gl.addWidget(QtWidgets.QLabel("Smooth window"), 0, 0)
        self.smooth_spin = QtWidgets.QSpinBox(); self.smooth_spin.setRange(1, 51); self.smooth_spin.setSingleStep(2); self.smooth_spin.setValue(1)
        self.smooth_spin.valueChanged.connect(self._on_smooth)
        gl.addWidget(self.smooth_spin, 0, 1)
        gl.addWidget(b_new, 1, 0, 1, 2)
        gl.addWidget(b_savecsv, 2, 0); gl.addWidget(b_open, 2, 1)
        v.addWidget(g)

        v.addStretch(1)

        # --- log ---
        self.log = QtWidgets.QPlainTextEdit(); self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(500); self.log.setFixedHeight(120)
        v.addWidget(self.log)
        return panel

    def _build_plots(self) -> QtWidgets.QWidget:
        self.glw = pg.GraphicsLayoutWidget()
        p1 = self.glw.addPlot(row=0, col=0)
        self.tp_raw = TwinPlot(p1, "pH", "mV (mV)", PEN_PH, PEN_MV)
        p2 = self.glw.addPlot(row=1, col=0)
        self.tp_d1 = TwinPlot(p2, "dpH/dmL", "dmV/dmL", PEN_DPH, PEN_DMV)
        self.p3 = self.glw.addPlot(row=2, col=0)
        self.p3.setLabel("bottom", "Volume (mL)")
        self.p3.setLabel("left", "d2pH/dmL2", color=PEN_D2.color().name())
        self.p3.showGrid(x=True, y=True, alpha=0.3)
        self.curve_d2 = self.p3.plot(pen=PEN_D2)

        # equivalence markers
        self.eq_lines = [pg.InfiniteLine(angle=90, movable=False, pen=PEN_EQ)
                         for _ in range(3)]
        for ln, p in zip(self.eq_lines, (p1, p2, self.p3)):
            ln.hide(); p.addItem(ln)

        # Double-click anywhere on the graphs resets all axes to auto-range.
        self.glw.scene().sigMouseClicked.connect(self._on_plot_click)
        return self.glw

    def _on_plot_click(self, ev):
        if ev.double():
            self._reset_views()

    def _reset_views(self):
        try:
            self.tp_raw.autorange()
            self.tp_d1.autorange()
            self.p3.enableAutoRange(x=True, y=True)
        except Exception as e:
            self._log(f"# reset error: {e}")

    # ------------------------------------------------------------ serial
    def _refresh_ports(self):
        self.port_combo.clear()
        for dev, desc in available_ports():
            self.port_combo.addItem(f"{dev}  ({desc})", dev)
        if self.port_combo.count() == 0:
            self.port_combo.addItem("no ports", None)

    def _toggle_connect(self):
        if self.link.is_open:
            self.link.close()
            self.btn_connect.setText("Connect")
            self._log("# disconnected")
            return
        dev = self.port_combo.currentData()
        if not dev:
            self._log("# no port selected")
            return
        try:
            self.link.open(dev, 115200)
            self.btn_connect.setText("Disconnect")
            self._log(f"# connected {dev}")
        except Exception as e:
            self._log(f"# ERROR open: {e}")

    def send(self, command: str):
        command = command.strip()
        if not command:
            return
        if not self.link.is_open:
            self._log("# not connected")
            return
        try:
            self.link.write(command)
            self._log(f">> {command}")
        except Exception as e:
            self._log(f"# ERROR send: {e}")

    def _start_titration(self):
        if self.mode_ep.isChecked():
            cmd = cmd_titrate_ep(_f(self.ep_target), _f(self.ep_dose),
                                 _f(self.ep_tol), _f(self.ep_max))
        else:
            cmd = cmd_titrate_curve(_f(self.cv_step), _f(self.cv_max),
                                    _f(self.cv_settle))
        self._new_run()
        self.send(cmd)

    # ------------------------------------------------------------- timer
    def _tick(self):
        # Never let a bad line / transient error kill the timer (would freeze
        # the whole UI). Catch, log, and keep going.
        try:
            new_rows = False
            for line in self.link.drain():
                try:
                    is_data = self.data.parse_line(line)
                except Exception:
                    is_data = False
                if is_data:
                    new_rows = True
                    self.last_ph = self.data.ph[-1]
                    self.last_mv = self.data.mv[-1]
                else:
                    self._handle_text(line)
            self._update_readout()
            if new_rows:
                self._update_plots()
        except Exception as e:
            self._log(f"# tick error: {e}")

    def _handle_text(self, line: str):
        self._log(line)
        if line.startswith("STATE="):
            self.state = line.split()[0].split("=", 1)[1]
        elif line.startswith("OK pH="):  # READ reply
            for tok in line.split():
                if tok.startswith("pH="):
                    self.last_ph = _f(tok[3:])
                elif tok.startswith("mV="):
                    self.last_mv = _f(tok[3:])

    def _update_readout(self):
        self.lbl_ph.setText(f"{self.last_ph:.3f}" if self.last_ph == self.last_ph else "-")
        self.lbl_mv.setText(f"{self.last_mv:.2f}" if self.last_mv == self.last_mv else "-")
        self.lbl_ml.setText(f"{self.data.vol_ml[-1]:.3f}" if len(self.data) else "-")
        self.lbl_state.setText(self.state)

    def _update_plots(self):
        try:
            v = np.asarray(self.data.vol_ml, float)
            ph = np.asarray(self.data.ph, float)
            mv = np.asarray(self.data.mv, float)
            self.tp_raw.set_data(v, ph, mv)
            _, d1ph = self.data.dph_dml()
            _, d1mv = self.data.dmv_dml()
            self.tp_d1.set_data(v, d1ph, d1mv)
            _, d2 = self.data.d2ph_dml2()
            self.curve_d2.setData(v, d2)

            eq = self.data.equivalence_volume()
            if eq is not None and len(self.data) >= 3:
                self.lbl_eq.setText(f"{eq:.3f}")
                for ln in self.eq_lines:
                    ln.setPos(eq); ln.show()
            else:
                self.lbl_eq.setText("-")
        except Exception as e:
            self._log(f"# plot error: {e}")

    # -------------------------------------------------------------- data
    def _new_run(self):
        self.data.clear()
        for ln in self.eq_lines:
            ln.hide()
        self._update_plots()

    def _on_smooth(self, val):
        self.data.smooth_window = val
        self._update_plots()

    def _save_csv(self):
        if len(self.data) == 0:
            self._log("# nothing to save")
            return
        path, _sel = QtWidgets.QFileDialog.getSaveFileName(
            self, "Save titration CSV", "titration.csv", "CSV (*.csv)")
        if path:
            self.data.save_csv(path)
            self._log(f"# saved {path}")

    def _open_csv(self):
        path, _sel = QtWidgets.QFileDialog.getOpenFileName(
            self, "Open CSV", "", "CSV/Log (*.csv *.log);;All (*)")
        if path:
            self.data.load_csv(path)
            self._log(f"# loaded {path} ({len(self.data)} rows)")
            self._update_plots()

    def _log(self, msg: str):
        self.log.appendPlainText(msg)

    def closeEvent(self, ev):
        self.link.close()
        super().closeEvent(ev)


def _f(s) -> float:
    try:
        return float(str(s).strip())
    except (ValueError, AttributeError):
        return 0.0


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()

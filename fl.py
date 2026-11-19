
#!/usr/bin/env python3
"""fl.py - fast build + flash for the titrator (STM32G431CBU6 via ST-Link).
 
Usage (from anywhere, the script finds the project by its own location):
    ./fl.py                 build + flash
    ./fl.py -m              build + flash + serial monitor
    ./fl.py -n              flash the existing build/ without rebuilding
    ./fl.py -b              build only
    ./fl.py -o              flash through openocd (from .elf) instead of st-flash
    ./fl.py -p              probe: show chip / ST-Link info
    ./fl.py -r              reset the MCU
    ./fl.py -e              mass erase (also wipes emulated-EEPROM pH calibration!)
    ./fl.py --monitor-only  just open the serial monitor
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
 
ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
SKETCH = "titrator.ino"
BIN = BUILD / f"{SKETCH}.bin"
ELF = BUILD / f"{SKETCH}.elf"
 
# USB CDC is required: the firmware talks over Serial = USB CDC (see config.h)
FQBN = (
    "STMicroelectronics:stm32:GenG4:"
    "pnum=GENERIC_G431CBUX,usb=CDCgen,xserial=generic,upload_method=swdMethod"
)
FLASH_ADDR = "0x08000000"
OPENOCD_TARGET = "target/stm32g4x.cfg"
BAUD = 115200
PORT_GLOB = "/dev/ttyACM*"
 
 
def die(msg):
    print(f"✗ {msg}", file=sys.stderr)
    sys.exit(1)
 
 
def need(tool, hint=""):
    if shutil.which(tool) is None:
        die(f"'{tool}' not found. {hint}".strip())
 
 
def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
 
 
def tail(text, n=15):
    return "\n".join(text.strip().splitlines()[-n:])
 
 
def build():
    need("arduino-cli", "Install: sudo pacman -S arduino-cli")
    print("• build ...", flush=True)
    t = time.time()
    r = run(["arduino-cli", "compile", "--fqbn", FQBN,
             "--output-dir", str(BUILD), str(ROOT)])
    out = r.stdout + r.stderr
    if r.returncode != 0:
        print(out)
        die("build failed")
    for line in out.splitlines():
        if line.startswith(("Sketch uses", "Global variables")):
            print("  " + line)
    print(f"  ok ({time.time() - t:.1f}s)")
 
 
def flash_stlink():
    need("st-flash", "Install: sudo pacman -S stlink")
    if not BIN.exists():
        die(f"{BIN} not found - build first (drop -n)")
    print("• flash (st-flash) ...", flush=True)
    t = time.time()
    r = run(["st-flash", "--reset", "write", str(BIN), FLASH_ADDR])
    out = r.stdout + r.stderr
    if r.returncode != 0 or "verified" not in out:
        print(tail(out))
        die("flash failed (is the ST-Link connected? try ./fl.py -p)")
    for line in out.splitlines():
        if "Attempting to write" in line:
            print("  " + line.split("INFO common_flash.c:")[-1].strip())
    print(f"  verified ({time.time() - t:.1f}s)")
 
 
def flash_openocd():
    need("openocd", "Install: sudo pacman -S openocd")
    if not ELF.exists():
        die(f"{ELF} not found - build first (drop -n)")
    print("• flash (openocd) ...", flush=True)
    t = time.time()
    r = run(["openocd", "-f", "interface/stlink.cfg", "-f", OPENOCD_TARGET,
             "-c", f'program "{ELF}" verify reset exit'])
    out = r.stdout + r.stderr
    if r.returncode != 0 or "Verified OK" not in out:
        print(tail(out))
        die("openocd flash failed")
    print(f"  verified ({time.time() - t:.1f}s)")
 
 
def probe():
    need("st-info", "Install: sudo pacman -S stlink")
    r = run(["st-info", "--probe"])
    # st-info chatters on stderr in debug builds, the useful part is on stdout
    print(r.stdout.strip() or tail(r.stderr))
    if r.returncode != 0:
        sys.exit(1)
 
 
def reset():
    need("st-flash", "Install: sudo pacman -S stlink")
    r = run(["st-flash", "reset"])
    if r.returncode != 0:
        print(tail(r.stdout + r.stderr))
        die("reset failed")
    print("✓ reset")
 
 
def erase():
    need("st-flash", "Install: sudo pacman -S stlink")
    ans = input("Mass erase also wipes the stored pH calibration. Continue? [y/N] ")
    if ans.strip().lower() != "y":
        print("cancelled")
        return
    r = run(["st-flash", "erase"])
    if r.returncode != 0:
        print(tail(r.stdout + r.stderr))
        die("erase failed")
    print("✓ erased")
 
 
def wait_port(timeout=8.0):
    """After a reset the USB CDC device re-enumerates; wait for it."""
    time.sleep(1.0)  # let the old ttyACM disappear first
    end = time.time() + timeout
    while time.time() < end:
        ports = sorted(glob.glob(PORT_GLOB))
        if ports:
            return ports[0]
        time.sleep(0.2)
    return None
 
 
def monitor(port=None):
    port = port or wait_port()
    if not port:
        die(f"no {PORT_GLOB} appeared - is the USB cable plugged into the board?")
    try:
        import serial  # pyserial
    except ImportError:
        if shutil.which("picocom"):
            os.execvp("picocom", ["picocom", "-b", str(BAUD), port])
        die("need pyserial (pip install pyserial) or picocom for the monitor")
    print(f"• monitor {port} @ {BAUD}  (Ctrl+C to exit)")
    try:
        with serial.Serial(port, BAUD, timeout=0.2) as s:
            while True:
                line = s.readline()
                if line:
                    print(line.decode(errors="replace").rstrip(), flush=True)
    except KeyboardInterrupt:
        print()
    except serial.SerialException as e:
        die(f"serial error: {e}")
 
 
def main():
    ap = argparse.ArgumentParser(description="titrator: build + flash STM32G431 via ST-Link")
    ap.add_argument("-n", "--no-build", action="store_true", help="flash existing build/")
    ap.add_argument("-b", "--build-only", action="store_true", help="build, do not flash")
    ap.add_argument("-o", "--openocd", action="store_true", help="flash with openocd instead of st-flash")
    ap.add_argument("-m", "--monitor", action="store_true", help="open serial monitor after flashing")
    ap.add_argument("--monitor-only", action="store_true", help="only open the serial monitor")
    ap.add_argument("--port", help="serial port for the monitor (default: auto /dev/ttyACM*)")
    ap.add_argument("-p", "--probe", action="store_true", help="show chip / ST-Link info and exit")
    ap.add_argument("-r", "--reset", action="store_true", help="reset the MCU and exit")
    ap.add_argument("-e", "--erase", action="store_true", help="mass erase and exit")
    a = ap.parse_args()
 
    if a.probe:
        return probe()
    if a.reset:
        return reset()
    if a.erase:
        return erase()
    if a.monitor_only:
        return monitor(a.port)
 
    t0 = time.time()
    if not a.no_build:
        build()
    if not a.build_only:
        (flash_openocd if a.openocd else flash_stlink)()
    print(f"✓ done in {time.time() - t0:.1f}s")
 
    if a.monitor and not a.build_only:
        monitor(a.port)
 
 
if __name__ == "__main__":
    main()
 
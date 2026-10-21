#!/usr/bin/env python3
"""
titrator_log.py - cross-platform serial logger for the STM32 titrator.

Opens the USB-CDC port, optionally sends a command, and tee's every line from
the device to the console AND a timestamped CSV/log file. Works on
Windows / Linux / macOS.

Examples:
    # list serial ports
    python titrator_log.py --list

    # log a fixed-increment curve to a file, sending the command on connect
    python titrator_log.py -p COM5 -c "TITRATE CURVE 50 500 3000"

    # just stream/log whatever the device prints, type commands interactively
    python titrator_log.py -p /dev/ttyACM0 -i

Requires: pyserial  ->  pip install pyserial
"""
import argparse
import datetime as dt
import sys
import threading

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial not installed. Run: pip install pyserial")


def list_serial_ports():
    ports = list_ports.comports()
    if not ports:
        print("No serial ports found.")
    for p in ports:
        print(f"{p.device}\t{p.description}")


def reader_thread(ser, logfile):
    """Read device lines forever, echo to stdout and append to logfile."""
    while True:
        try:
            line = ser.readline().decode(errors="replace").rstrip("\r\n")
        except (serial.SerialException, OSError):
            break
        if line == "":
            continue
        stamp = dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        print(line)
        logfile.write(f"{stamp},{line}\n")
        logfile.flush()


def main():
    ap = argparse.ArgumentParser(description="Titrator serial logger")
    ap.add_argument("-p", "--port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-c", "--cmd", help="command to send right after connecting")
    ap.add_argument("-o", "--out", help="output log file (default: auto timestamp)")
    ap.add_argument("-i", "--interactive", action="store_true",
                    help="forward stdin lines to the device as commands")
    ap.add_argument("--list", action="store_true", help="list ports and exit")
    args = ap.parse_args()

    if args.list:
        list_serial_ports()
        return
    if not args.port:
        ap.error("--port is required (use --list to find it)")

    fname = args.out or "titrator_" + dt.datetime.now().strftime("%Y%m%d_%H%M%S") + ".log"
    print(f"Logging to {fname}  (Ctrl-C to stop)")

    with serial.Serial(args.port, args.baud, timeout=1) as ser, \
         open(fname, "w", encoding="utf-8") as logfile:
        logfile.write("# host_time,device_line\n")

        t = threading.Thread(target=reader_thread, args=(ser, logfile), daemon=True)
        t.start()

        if args.cmd:
            ser.write((args.cmd.strip() + "\n").encode())

        try:
            if args.interactive:
                for line in sys.stdin:
                    ser.write((line.strip() + "\n").encode())
            else:
                t.join()
        except KeyboardInterrupt:
            print("\nStopped.")


if __name__ == "__main__":
    main()

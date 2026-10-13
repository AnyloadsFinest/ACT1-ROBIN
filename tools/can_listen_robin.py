#!/usr/bin/env python3
"""
Listen on a USB-CAN adapter for ACT1-ROBIN bench traffic (SH-C30A / candleLight).

SH-C30A (DSD TECH) uses candleLight firmware and the gs_usb protocol — not a COM
port. On Windows, install WinUSB for the device via Zadig, then:

  pip install "python-can[gs-usb]"

Default ROBIN bench beacon (CONFIG_ACT1_CAN_BENCH_BEACON): 1 Hz, standard ID 0x7FE,
8 bytes: b'ACT1' + big-endian uint32 counter. Bit rate must match F7.3.2 (default 250k).

Hardware: CANH-CANH, CANL-CANL; enable 120 ohm on adapter (R120 ON) if the bus has
only two nodes and no other terminator.

Open hardware: https://github.com/dsdtech-official/can-adapters/tree/main/SH-C30A
"""

from __future__ import annotations

import argparse
import struct
import sys
import time


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Read CAN frames from SH-C30A (gs_usb)")
    p.add_argument(
        "--bitrate",
        type=int,
        default=250_000,
        help="CAN bit rate in bit/s (match scale F7.3.2, default 250000)",
    )
    p.add_argument(
        "--index",
        type=int,
        default=0,
        help="gs_usb device index if multiple adapters (default 0)",
    )
    p.add_argument(
        "--decode-beacon",
        action="store_true",
        default=True,
        help="Pretty-print ROBIN 0x7FE ACT1 bench beacon (default on)",
    )
    p.add_argument(
        "--no-decode-beacon",
        action="store_false",
        dest="decode_beacon",
        help="Print all frames as hex only",
    )
    return p.parse_args()


def format_frame(msg, decode_beacon: bool) -> str:
    ts = getattr(msg, "timestamp", time.time())
    id_str = f"{msg.arbitration_id:03X}"
    if msg.is_extended_id:
        id_str = f"{msg.arbitration_id:08X}x"
    data = bytes(msg.data[: msg.dlc])
    hex_data = " ".join(f"{b:02X}" for b in data)
    line = f"{ts:.3f}  ID={id_str}  DLC={msg.dlc}  {hex_data}"
    if (
        decode_beacon
        and not msg.is_extended_id
        and msg.arbitration_id == 0x7FE
        and len(data) >= 8
        and data[:4] == b"ACT1"
    ):
        counter = struct.unpack(">I", data[4:8])[0]
        line += f"  | ROBIN bench beacon seq={counter}"
    return line


def _ensure_pyusb_backend() -> None:
    """Windows: PyUSB needs libusb-package; wrong find_library hook causes NoBackendError."""
    try:
        import libusb_package
        import usb.backend.libusb1 as libusb1

        if libusb1.get_backend() is None:
            libusb1.get_backend(find_library=libusb_package.find_library)
    except ImportError:
        pass


def main() -> int:
    args = parse_args()
    _ensure_pyusb_backend()

    try:
        import can
    except ImportError:
        print("Install: pip install \"python-can[gs-usb]\"", file=sys.stderr)
        return 1

    try:
        bus = can.Bus(interface="gs_usb", channel=args.index, bitrate=args.bitrate)
    except Exception as e:
        print(
            "Failed to open gs_usb. SH-C30A needs candleLight + WinUSB (Zadig), not serial.\n"
            f"Error: {e}",
            file=sys.stderr,
        )
        return 1

    print(
        f"Listening on gs_usb index {args.index} @ {args.bitrate} bit/s "
        "(Ctrl+C to stop)\n"
    )
    try:
        while True:
            msg = bus.recv(timeout=1.0)
            if msg is None:
                continue
            print(format_frame(msg, args.decode_beacon))
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        bus.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

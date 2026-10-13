"""Open gs_usb, print frames for N seconds, exit (for automation)."""
import struct
import sys
import time

import libusb_package
import usb.backend.libusb1 as libusb1

libusb1.get_backend(find_library=libusb_package.find_library)

import can

duration = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0
bitrate = int(sys.argv[2]) if len(sys.argv) > 2 else 250_000

bus = can.Bus(interface="gs_usb", channel=0, bitrate=bitrate)
print(f"Listening {duration:.0f}s @ {bitrate} bit/s...", flush=True)
t0 = time.time()
n = 0
while time.time() - t0 < duration:
    m = bus.recv(timeout=0.5)
    if m is None:
        continue
    n += 1
    data = bytes(m.data[: m.dlc])
    hexs = " ".join(f"{b:02X}" for b in data)
    extra = ""
    if m.arbitration_id == 0x7FE and len(data) >= 8 and data[:4] == b"ACT1":
        extra = f" | ROBIN beacon seq={struct.unpack('>I', data[4:8])[0]}"
    print(f"ID={m.arbitration_id:03X} DLC={m.dlc} {hexs}{extra}", flush=True)
bus.shutdown()
print(f"Done. frames={n}", flush=True)

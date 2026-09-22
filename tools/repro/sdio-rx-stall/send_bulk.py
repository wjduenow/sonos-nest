#!/usr/bin/env python3
"""Bulk TCP sender for the esp-hosted-mcu#243 reproducer.

Streams to the ESP32-P4's sink at a steady rate. The default 3 Mbit/s is deliberately modest:
the fault is about the SIZE of a single queued read, not about throughput, and a gentle stream
that still fills the co-processor's Tx queue reproduces it as reliably as a flood while making
"it stopped" unambiguous. Run it from any Linux/macOS box on the same network:

    python3 send_bulk.py 192.168.1.50            # 3 Mbit/s, forever
    python3 send_bulk.py 192.168.1.50 --mbit 8   # push harder
    python3 send_bulk.py 192.168.1.50 --burst 64 # 64 KB bursts, bigger single reads

Healthy: "sent=… (+N KB/s)" keeps climbing.
The bug: the device stops acknowledging, so this script's own sends block and the rate drops to
zero within a few seconds, while the device prints STALL DETECTED and never receives again.
"""
import argparse
import socket
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("host", help="the ESP32-P4's IP address (it prints it at boot)")
    ap.add_argument("--port", type=int, default=5001)
    ap.add_argument("--mbit", type=float, default=3.0, help="target rate, Mbit/s (default 3)")
    ap.add_argument("--burst", type=int, default=16, help="bytes per send, KB (default 16)")
    args = ap.parse_args()

    chunk = b"\xa5" * (args.burst * 1024)
    per_second = max(1, int(args.mbit * 1e6 / 8 / len(chunk)))
    print(f"-> {args.host}:{args.port}  {args.mbit} Mbit/s  "
          f"{args.burst} KB x {per_second}/s", flush=True)

    s = socket.create_connection((args.host, args.port), timeout=10)
    # Block rather than buffer without bound, so a device that stops acknowledging shows up here
    # as a stalled send instead of as silently growing kernel queues.
    s.settimeout(30)

    sent = 0
    t_last = time.monotonic()
    sent_last = 0
    try:
        while True:
            start = time.monotonic()
            for _ in range(per_second):
                try:
                    s.sendall(chunk)
                except socket.timeout:
                    print(f"\nSEND BLOCKED for 30 s after {sent/1e6:.1f} MB — "
                          f"the device stopped acknowledging. That is the stall.", flush=True)
                    return 2
                sent += len(chunk)
            now = time.monotonic()
            if now - t_last >= 1.0:
                rate = (sent - sent_last) / (now - t_last)
                print(f"sent={sent/1e6:8.1f} MB (+{rate/1024:7.0f} KB/s)", flush=True)
                t_last, sent_last = now, sent
            slack = 1.0 - (now - start)
            if slack > 0:
                time.sleep(slack)
    except KeyboardInterrupt:
        print(f"\nstopped after {sent/1e6:.1f} MB", flush=True)
        return 0
    except (BrokenPipeError, ConnectionResetError) as e:
        print(f"\nconnection dropped after {sent/1e6:.1f} MB: {e}", flush=True)
        return 1
    finally:
        s.close()


if __name__ == "__main__":
    sys.exit(main())

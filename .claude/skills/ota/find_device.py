#!/usr/bin/env python3
"""Discover the sonos-nest device IP via mDNS (ArduinoOTA advertises _arduino._tcp).

WSL2 has no avahi/mDNS resolver, so query the LAN directly with python-zeroconf.
Prints "<name> <ip>" for each match and exits 0 if at least one is found, else 1.

Usage:  python3 find_device.py [hostname-substring]   (default: "sonos-nest")
"""
import sys, socket, time

try:
    from zeroconf import Zeroconf, ServiceBrowser
except ImportError:
    sys.stderr.write("zeroconf not installed; run: pip install zeroconf\n")
    sys.exit(2)

want = (sys.argv[1] if len(sys.argv) > 1 else "sonos-nest").lower()
found = {}


class Listener:
    def add_service(self, zc, type_, name):
        if want not in name.lower():
            return
        info = zc.get_service_info(type_, name)
        if info and info.addresses:
            ip = socket.inet_ntoa(info.addresses[0])
            found[name.split(".")[0]] = ip

    def update_service(self, *a):
        pass

    def remove_service(self, *a):
        pass


zc = Zeroconf()
# ArduinoOTA advertises _arduino._tcp; browse a couple of fallbacks too.
for svc in ("_arduino._tcp.local.", "_esp._tcp.local.", "_http._tcp.local."):
    ServiceBrowser(zc, svc, Listener())
time.sleep(5)
zc.close()

for name, ip in found.items():
    print(f"{name} {ip}")
sys.exit(0 if found else 1)

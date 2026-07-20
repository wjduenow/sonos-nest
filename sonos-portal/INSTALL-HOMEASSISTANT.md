# Installing Sonos Nest Portal as a Home Assistant add-on

Sonos Nest Portal ships as a Home Assistant **add-on** — the Supervisor builds it on your device from
this folder (no Docker registry, no prebuilt image, no command line needed). Once installed it
shows up in the HA sidebar, and your sonos-nest devices register with it automatically.

## Requirements

- A **Supervisor-based** Home Assistant install: **Home Assistant OS** or **Supervised**.
  (Home Assistant **Container** and **Core** have no add-on store — for those, run the portal with
  `docker compose up` instead; see the main [README](README.md).) Check **Settings → About**: if
  you see **Settings → Add-ons**, you have the Supervisor.
- The HA host must be on the **same LAN/VLAN** as your Sonos speakers and your sonos-nest devices.
  Discovery is mDNS (multicast); it does not cross subnets/VLANs.

---

## Method A — Add as a custom repository (recommended)

The Supervisor pulls the add-on straight from GitHub and builds it. Nothing to download by hand.

1. In HA, go to **Settings → Add-ons → Add-on Store**.
2. Top-right **⋮ (three dots) → Repositories**.
3. Paste this URL and click **Add**, then **Close**:
   ```
   https://github.com/wjduenow/sonos-nest
   ```
4. Back in the store, scroll to the **Sonos Nest add-ons** section and click **Sonos Nest Portal**.
5. Click **Install**. The first build takes a few minutes (it fetches the base image + Python
   deps). When it finishes:
   - Turn on **Start on boot** and **Watchdog**.
   - Leave **Show in sidebar** on (that's Ingress — how you open the dashboard).
6. Click **Start**, then **Open Web UI** (or the **Sonos Nest Portal** sidebar entry).

That's it. Power on a sonos-nest device on the same network and it appears within a few seconds.

---

## Method B — Local add-on (offline / no GitHub)

Use this if the HA host can't reach GitHub, or you want to run a modified copy.

1. Install the **Samba share** or **SSH & Web Terminal** add-on so you can reach the HA
   `/addons` folder.
2. Copy this entire `sonos-portal/` folder into `/addons/`, so it lands at:
   ```
   /addons/sonos-portal/config.yaml
   /addons/sonos-portal/Dockerfile
   /addons/sonos-portal/app/…
   ```
3. In **Settings → Add-ons → Add-on Store**, click **⋮ → Reload**.
4. A **Local add-ons** section now lists **Sonos Nest Portal**. Open it, **Install**, then **Start**
   (same options as Method A step 5–6).

---

## Configuration

The add-on works with **no configuration**. There is one optional setting on the add-on's
**Configuration** tab:

| Option        | When to set it                                                                 |
|---------------|--------------------------------------------------------------------------------|
| `portal_host` | Only if the dashboard shows the wrong "Advertising … at `<ip>`" address at the bottom (e.g. the HA host has several network interfaces and mDNS picked a non-LAN one). Set it to the HA host's LAN IP. Leave blank to auto-detect. |

The device registry persists automatically in the add-on's `/data`, so your device list survives
restarts and updates.

## Verify it's working

- Open the dashboard. The footer should read **Advertising `_sonosportal._tcp.local.` at
  `<HA-LAN-IP>`:8000**.
- Power on (or reboot) a sonos-nest device. Within a few seconds it appears as a tile with an
  online dot. Click **Open config** to jump to that device's web page (the round *nest* has no web
  page, so its button is disabled — it still shows as present).
- No devices yet, but you want to prove the API? From any machine on the LAN:
  ```bash
  curl -X POST http://<HA-LAN-IP>:8000/api/register \
    -H 'Content-Type: application/json' -d @sample.json
  ```
  The sample device should appear on the dashboard.

## Updating

- **Method A:** bump `version:` in `config.yaml` (or wait for a repo update); HA shows an
  **Update** button on the add-on page.
- **Method B:** replace the folder contents, **⋮ → Reload**, then **Rebuild** on the add-on page.

## Troubleshooting

- **The add-on doesn't appear after adding the repo.** You're likely on HA Container/Core (no
  Supervisor). Use `docker compose up` instead (main README).
- **Devices never show up.** They and the HA host must be on the same LAN/VLAN — mDNS multicast
  doesn't route between subnets. This is also why the add-on runs with host networking; that's
  already set in the manifest, nothing to configure.
- **Footer shows a weird IP / devices can't reach the portal.** Set `portal_host` to the HA host's
  LAN IP (see Configuration) and restart the add-on.
- **Firmware side:** each device caches the portal's address in NVS and heartbeats every ~45 s; a
  device flips offline on the dashboard ~2 min after it stops beating. If the portal is restarted
  and forgets a device, the device re-registers on its next heartbeat automatically.

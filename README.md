# AUST Bus Management System

A campus bus tracking and management system that shows students where each
university bus is **right now**, so they arrive at the right stop at the right
time.

This repository is the home of the whole project: the **hardware firmware** that
runs on each bus (written by the EEE team) and the **handover guideline** that tells
the CSE team exactly how to build the cloud database and live map that consume the
data.

> **Status: Prototype.** We are validating the GPS + GSM hardware and finalising the
> firmware before the first live road test on a university bus.

---

## Table of contents

- [System overview](#system-overview)
- [Repository structure](#repository-structure)
- [How it works (the data flow)](#how-it-works-the-data-flow)
- [Hardware](#hardware)
- [Firmware (EEE team)](#firmware-eee-team)
- [Blynk test mode](#blynk-test-mode)
- [Cloud / database (CSE team)](#cloud--database-cse-team)
- [Getting started](#getting-started)
- [Contributing](#contributing)
- [Roadmap](#roadmap)
- [Notes and constraints](#notes-and-constraints)

---

## System overview

Three layers:

1. **Hardware layer (on the bus)** — an ESP32 reads its GPS position (NEO-7M) and
   sends it to the cloud over the mobile network (SIM800L GPRS). An MPU6050 is
   wired but not yet active (reserved for future driver-safety alerts).
2. **Cloud layer** — a REST API receives each bus position and stores it in a
   database. During the testing phase the device can instead post to the **Blynk**
   IoT platform for quick validation on a phone.
3. **End-user layer** — students view live bus positions on a map, either on the
   Blynk app (testing) or on the CSE team's live web map (production).

The current phase is **prototype & testing**: validate the hardware, then hand the
data contract over to CSE to build the web side.

---

## Repository structure

```
.
├── BUS_TRACKER/                  # ESP32 firmware (EEE team)
│   ├── BUS_TRACKER.ino           # the main sketch (GPS + GSM + reporting)
│   └── BLYNK_TUTORIAL.md         # step-by-step guide for testing with Blynk
└── CSE_GUIDELINE.md              # handover guide for CSE (API contract + full
                                  # reference server code embedded)
```

---

## How it works (the data flow)

```
┌────────────────────────────────────┐
│  HARDWARE — one device per bus     │
│  ESP32 + NEO-7M GPS + SIM800L GSM  │
└──────────────┬─────────────────────┘
               │  POST /api/bus-location  (JSON, every 10 s)
               ▼
┌────────────────────────────────────┐
│  CLOUD — REST API (CSE team)       │
│  validate  →  store  →  serve      │
└──────────────┬─────────────────────┘
               │
               ▼
┌────────────────────────────────────┐
│  END USERS — live web map          │
│  marker per bus + route + speed    │
└────────────────────────────────────┘
```

The device posts one report every **10 seconds**, **only when the GPS fix is
accurate** (≥ 4 satellites and HDOP ≤ 3.0). Values are averaged over the window,
and the timestamp is **UTC**.

Example payload:

```json
{
  "bus_id": "BUS-001",
  "latitude": 12.345678,
  "longitude": 76.123456,
  "speed_kmh": 34.2,
  "timestamp": "2026-08-17T10:00:00Z"
}
```

---

## Hardware

### Components

| Component | Model | Qty | Status |
| :--- | :--- | :--- | :--- |
| Microcontroller | ESP32 (Dev Module) | 1 | Active |
| GPS module | NEO-7M | 1 | Active |
| GSM/GPRS module | SIM800L | 1 | Active |
| IMU / safety sensor | MPU6050 | 1 | Wired, not yet in firmware |
| Battery | Li-ion 6000 mAh (with BMS) | 1 | Power for SIM800L |
| Voltage regulator | LM2596 buck module | 1 | 5 V for ESP32 in vehicle |

### Wiring (common ground on all modules!)

| Module | Pin | ESP32 pin | Notes |
| :--- | :--- | :--- | :--- |
| NEO-7M GPS | VCC | 3.3V | |
| | GND | GND | |
| | TX | GPIO 16 | GPS → ESP32 (UART2 RX) |
| | RX | GPIO 17 | ESP32 → GPS (UART2 TX) |
| SIM800L GSM | VCC | Li-ion (+) directly | Do **not** use ESP32 5V/3.3V — needs up to 2 A peak |
| | GND | Common GND | Tie battery negative to ESP32 GND |
| | TXD | GPIO 26 | GSM → ESP32 (UART1 RX) |
| | RXD | GPIO 27 | ESP32 → GSM (UART1 TX) |
| MPU6050 | VCC | 3.3V | Not active yet |
| | GND | GND | |
| | SDA | GPIO 21 | I2C data |
| | SCL | GPIO 22 | I2C clock |
| ESP32 | VIN/5V | USB or LM2596 5 V | USB during development |

> **Power warning:** the SIM800L must be powered directly from the Li-ion cell
> through its BMS — its GSM transmit bursts can draw up to 2 A. All grounds must be
> tied together.

---

## Firmware (EEE team)

The single-file sketch is `BUS_TRACKER/BUS_TRACKER.ino`:

- Reads NMEA from the NEO-7M over UART2 (GPIO 16/17) with TinyGPSPlus.
- Boots the SIM800L over UART1 (GPIO 26/27) with TinyGSM and opens a GPRS data
  connection.
- Every 10 s posts a JSON report to the REST API (or to Blynk in test mode).
- Accuracy: only reports fixes with ≥ `MIN_SATELLITES` satellites and
  `HDOP ≤ MAX_HDOP`, and averages the samples gathered during each window.

### Placeholders to fill in before going live

| Constant | Purpose | Example |
| :--- | :--- | :--- |
| `APN`, `GPRS_USER`, `GPRS_PASS` | Your telecom operator's APN | `"internet"` |
| `SERVER`, `PORT`, `ENDPOINT` | CSE team's API host and path | `"your-api-server.com"`, `80`, `"/api/bus-location"` |
| `BUS_ID` | Unique bus identifier | `"BUS-001"` |
| `BLYNK_TEMPLATE_ID`, `BLYNK_AUTH_TOKEN` | Only needed in Blynk test mode | — |

### Required Arduino libraries

`TinyGPSPlus`, `TinyGSM`, `ArduinoHttpClient` (production mode), and `Blynk`
(test mode only).

### Flashing

1. Install the libraries via **Sketch → Include Library → Manage Libraries…**
2. Board: **ESP32 Dev Module**, select the correct COM port.
3. Open the sketch, set `USE_BLYNK_TEST` to `1` (test) or `0` (production).
4. Upload and watch the Serial Monitor at **115200 baud**.

---

## Blynk test mode

For a quick hardware validation without building any server, set `USE_BLYNK_TEST`
to `1` in the sketch. The device then pushes the location to Blynk **Virtual Pin V0**
(map widget) and speed to **V1** instead of the REST API.

Follow the complete step-by-step setup (account, template, datastreams, auth token,
dashboard) in `BUS_TRACKER/BLYNK_TUTORIAL.md`.

---

## Cloud / database (CSE team)

Everything CSE needs is in `CSE_GUIDELINE.md`. It includes:

- The **exact API contract** the device posts to: `POST /api/bus-location` with the
  JSON body shown above, expected response codes (`201` / `400` / `401`).
- Facts to design around: 10 s cadence, UTC timestamps, valid-fix-only data,
  out-of-order reports, and normal data gaps.
- A full **reference implementation** (Node.js + Express + SQLite + Leaflet live map)
  embedded as ready-to-copy files — including a simulator so CSE can test before any
  hardware is connected.
- Deployment notes, an optional `api_key` shared-secret check, and a test plan.

### API contract summary

```
POST /api/bus-location
Content-Type: application/json

{ "bus_id": "BUS-001", "latitude": 12.345678, "longitude": 76.123456,
  "speed_kmh": 34.2, "timestamp": "2026-08-17T10:00:00Z" }
```

Responses: `201` stored · `400` invalid payload · `401` bad `api_key` (optional).

---

## Getting started

**EEE — validate the hardware**

1. Read `BUS_TRACKER/BLYNK_TUTORIAL.md`.
2. Set `USE_BLYNK_TEST = 1`, fill in the Blynk placeholders.
3. Flash and confirm the map marker moves on your phone.

**CSE — build the cloud + map**

1. Read `CSE_GUIDELINE.md` (sections 1–7).
2. Save the four reference files into a `cse-server/` folder, run
   `npm install && npm start`.
3. Pass the test plan in section 7 of the guideline.
4. Send the EEE team the host / port / `api_key` (checklist in section 10).

---

## Contributing

This is a cross-department student project. Contributions are welcome from anyone:

- **EEE contributors** — work in `BUS_TRACKER/`:
  - Keep the firmware single-file and simple; no unnecessary extra files.
  - When you change the JSON payload, update the example in `CSE_GUIDELINE.md`
    (section 2) so CSE is never surprised.
  - Any new dependency must be added to the "Required Arduino libraries" list in
    the README.
- **CSE contributors** — work from `CSE_GUIDELINE.md`:
  - Keep the REST contract backward compatible (`bus_id`, `latitude`, `longitude`,
    `speed_kmh`, `timestamp`).
  - Add your server work as new files; do not overwrite the guideline's embedded
    reference code without updating the guideline too.

General rules:

- Open an issue first if a change affects the data contract or the wiring.
- Follow the existing code style of the file you edit.
- Do not commit credentials — all keys stay as placeholders.

---

## Roadmap

- [x] Hardware wiring + module verification (GPS, GSM)
- [x] Prototype firmware with accuracy gating (HDOP / satellite count / averaging)
- [x] Blynk test-mode integration
- [ ] Live road test on a university bus (1 unit)
- [ ] CSE cloud API + live map in production
- [ ] Firebase + custom Android app (long-term replacement for Blynk)
- [ ] Activate MPU6050 for hard-braking / accident alerts
- [ ] Geofencing alerts when a bus approaches a stop
- [ ] Web admin dashboard for fleet managers (routes, reports)

---

## Notes and constraints

- The SIM800L needs a **2G / 2G-fallback** network with an active data plan; APN
  settings come from the local telecom operator.
- The NEO-7M needs a **clear sky view**; the first fix can take 5–10 minutes.
- All bus-to-cloud communication is over **GPRS** (no Wi-Fi in the vehicle).
- The MPU6050 is wired but **not yet active** in firmware.
- This project is released under the **MIT License** — see `LICENSE`.

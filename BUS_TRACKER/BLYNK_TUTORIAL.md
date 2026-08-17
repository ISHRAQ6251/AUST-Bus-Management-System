# Blynk Test Mode Tutorial

This tutorial explains how to test the bus tracker with the **Blynk IoT app**
before connecting it to the CSE team's real web database.

In test mode the sketch sends the location to Blynk **Virtual Pin V0**
(a map widget on your phone) instead of doing the REST API upload.

---

## What you need

- The sketch from this folder (`BUS_TRACKER.ino`)
- The Blynk IoT app (iOS / Android) or the Blynk Web Dashboard
- A free account at https://blynk.cloud
- The SIM800L powered from the Li-ion battery (not from the ESP32 5V/3.3V!)
- A 2G/GPRS-capable SIM with an active data plan

---

## Step 1 - Install the Arduino libraries

In the Arduino IDE open **Sketch > Include Library > Manage Libraries** and install:

1. `TinyGPSPlus` (Mikal Hart)
2. `TinyGSM` (Volodymyr Shymanskyy)
3. `ArduinoHttpClient` (Arduino) - only needed for production mode, but install it anyway
4. `Blynk` (the new Blynk IoT library, not the old "Blynk Legacy")

Only the `Blynk` library is required for test mode.

## Step 2 - Create a Template and a Device in the Blynk Console

1. Log in at https://blynk.cloud
2. Click **New Template** (top-right).
3. Name it e.g. `Bus Tracker`, choose hardware **ESP32**, connection **GSM/GPRS**.
   Click **Done**. The console shows a **Template ID** (looks like `TMPLxxxxxxx`).
4. Open the template, go to **Datastreams**, click **New Datastream**, choose
   **Virtual Pin**, set **Virtual Pin = V0**, **Data Type = Location** (this one
   stores lat + lon together). Add a second datastream **Virtual Pin = V1**,
   **Data Type = Double** (for speed). Save.
5. Go to **Devices**, click **New Device**, select your template, and give it a
   name (e.g. `Bus 1`). 
6. Open the device and copy its **Auth Token** (also shown in the app).

## Step 3 - Fill in the placeholders in the sketch

In `BUS_TRACKER.ino` change:

```cpp
#define USE_BLYNK_TEST 1          // was 0
```

and inside the `#if USE_BLYNK_TEST` block:

```cpp
#define BLYNK_TEMPLATE_ID  "TMPLxxxxxxx"        // your Template ID
#define BLYNK_DEVICE_NAME  "Bus Tracker"
#define BLYNK_AUTH_TOKEN   "your-blynk-auth-token"   // your Auth Token
```

Also set the GPRS APN (get it from your telecom operator), e.g.:

```cpp
const char APN[] = "internet";   // or "airtelgprs.com" etc.
```

## Step 4 - Upload and open the Serial Monitor

1. Board: **ESP32 Dev Module**, correct COM port.
2. Upload the sketch.
3. Open **Serial Monitor** at **115200 baud**.
4. You should see `GSM: restarting module... done`, then `waiting for network...`,
   then `GPRS: connecting... OK`.
5. Point the GPS antenna at the sky (near a window or outside). The first fix can
   take **5-10 minutes**. You will see `No accurate fix this window` until then.
6. Once fixed, the sketch prints the JSON report and `Sent to Blynk: V0 (map), V1 (speed)`.

## Step 5 - Build the dashboard

In the Blynk app (or Web Dashboard):

1. Open your device -> **Edit** dashboard.
2. Add a **Map** widget. In its settings, bind it to datastream **V0**.
   The bus location now appears as a marker on the map and updates every 10 s.
3. Add a **Gauge** widget bound to **V1** to show live speed in km/h.
4. Exit edit mode and the widgets run live.

## Step 6 - Verify the data

- The map marker should jump as you move the module around.
- In the Blynk console, open the device -> **Datastreams**, and you will see the
  latest V0 values, proving the data arrived.
- If the marker never appears, go to the Troubleshooting table below.

## Step 7 - Back to production mode

When you are done testing, set `USE_BLYNK_TEST` back to `0` and fill in the
real server fields:

```cpp
const char APN[]     = "your-network.apn";
const char SERVER[]  = "your-api-server.com";
const uint16_t PORT  = 80;
const char ENDPOINT[] = "/api/bus-location";
```

The CSE team will give you the real server address and the exact JSON field
names they expect. Re-upload and the device now posts to the web database.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
| :--- | :--- | :--- |
| `GSM: restarting module... done` never appears | SIM800L not powered / not wired | Power SIM800L from the Li-ion battery through its BMS, common ground with ESP32, check GPIO26/27 |
| `waiting for network... FAILED` | No signal / no SIM / no 2G | Check SIM is active, SIM800L has a good antenna, area has 2G coverage |
| `GPRS: connecting... FAILED` | Wrong APN | Ask the telecom operator for the correct APN / user / pass |
| `No accurate fix this window` | GPS has no fix yet | Move antenna to clear sky; first fix can take 5-10 min; check GPIO16/17 wiring |
| Map marker never appears | Datastream V0 not set to **Location** type, or wrong Auth Token | Re-check Step 2 and Step 3 |
| `Sent to Blynk` prints but map is empty | Map widget bound to wrong pin | Bind the Map widget to V0 in dashboard edit mode |

---

## Notes

- The timestamp sent in production mode is the GPS **UTC** time. Treat it as UTC
  on the server side (convert to local time when displaying).
- SBAS/DGPS augmentation is enabled by default on the NEO-7M; the sketch keeps
  only high-quality fixes via the HDOP and satellite-count gates.
- Blynk is for testing only. The production path is the REST API upload, which
  the CSE engineers will use.

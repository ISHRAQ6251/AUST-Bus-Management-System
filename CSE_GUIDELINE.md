# Handover Guideline for the CSE Department

**University Bus Tracking & Management System — what CSE must build and do**

This document tells the CSE team everything they need to build the web side of the
bus tracking system: a database that receives the bus locations, and a map that
displays them. The hardware side (ESP32 + GPS + GSM, in `BUS_TRACKER/`) is already
designed and posts data to the API described below.

---

## 1. Your role in the system

```
[ ESP32 on each bus ]  --GPRS/HTTP every 10 s-->  [ Your REST API ]  ->  [ Database ]
                                                                          |
                                                                          v
[ Student phones / laptops ]  <-- map page polls--  [ Your web map ] <----+ 
```

- Every bus carries a device that sends its position over the mobile network.
- Your job: **receive** those positions, **store** them, and **show** them on a live map.
- You do **not** need to touch the hardware or the ESP32 firmware.

---

## 2. The exact data contract (read carefully)

### Endpoint the device posts to

| Item | Value |
| :--- | :--- |
| Method | `POST` |
| Path | `/api/bus-location` |
| Protocol | plain HTTP (port 80 by default — see note in section 8) |
| Content-Type | `application/json` |

### Request body (JSON) — this is fixed, do not rename fields without telling EEE

```json
{
  "bus_id": "BUS-001",
  "latitude": 12.345678,
  "longitude": 76.123456,
  "speed_kmh": 34.2,
  "timestamp": "2026-08-17T10:00:00Z"
}
```

### Field meanings

| Field | Type | Notes |
| :--- | :--- | :--- |
| `bus_id` | string | Unique identifier per bus (`BUS-001`, `BUS-002`, ...). |
| `latitude` | number, 6 dp | WGS-84. Range -90 .. 90. |
| `longitude` | number, 6 dp | WGS-84. Range -180 .. 180. |
| `speed_kmh` | number | Ground speed in km/h. |
| `timestamp` | string | **UTC** time, `YYYY-MM-DDTHH:MM:SSZ`. Not local time! |

### Response codes you must return

| Code | Meaning |
| :--- | :--- |
| `201` | Stored OK |
| `400` | Malformed body (missing/invalid field) — return a JSON error explaining why |
| `401` | Bad `api_key` (only if you turn on the shared-secret check, section 9) |

---

## 3. Facts about the incoming data you must design around

1. **One report every 10 seconds per bus.** A 2-hour route = ~720 rows per bus.
2. **Only valid fixes are sent.** The device refuses to send when the GPS is poor
   (fewer than 4 satellites or HDOP > 3.0). If a row exists, it is a good position —
   you do **not** need extra outlier filtering.
3. **Values are already smoothed.** Latitude/longitude/speed are averages over the
   last 10 s window on the device. Take them as-is.
4. **`timestamp` is UTC.** Always convert to local time when displaying to users.
5. **Gaps are normal.** If a bus enters a tunnel/garage there is simply no data.
   Do not extrapolate through a gap.
6. **Rare fallback timestamp.** If the GPS clock had not locked yet, the device sends
   `0000-00-00T00:00:00Z`. Treat it as "unknown time" (do not display it as 1 Jan 0000).
7. **Out of order is possible.** GPRS is not perfect; a late report can arrive after a
   newer one. Design the "latest position" query to use insertion order, not the
   timestamp alone (the reference server handles this — section 6).

---

## 4. What you must build (3 parts)

### A. REST endpoint `POST /api/bus-location`
- Parse the JSON body.
- Validate every field (types and ranges as in the table above).
- Insert one row into the database.
- Return `201` on success, `400` with an error message on bad input.

### B. Database
Minimal table (you may extend it — fleet info, routes, drivers, etc.):

```sql
CREATE TABLE bus_locations (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  bus_id      TEXT    NOT NULL,
  latitude    REAL    NOT NULL,
  longitude   REAL    NOT NULL,
  speed_kmh   REAL,
  timestamp   TEXT    NOT NULL,      -- device time, UTC
  received_at TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')) -- server time, UTC
);
CREATE INDEX idx_bus_time ON bus_locations (bus_id, timestamp);
```

Keep both `timestamp` (device UTC) and `received_at` (server UTC): comparing them
tells you network/data latency.

### C. Live map page
- Any map library (Leaflet + OpenStreetMap is free, no API key — used in the reference).
- Poll the server every ~5 s and draw one marker per bus, coloured/clickable per bus.
- Draw the recent route (a polyline) behind each marker.
- Show speed and time in the marker popup.
- **Important:** convert the UTC `timestamp` to the viewer's local time for display.

---

## 5. Suggested extra API endpoints (for your own map page)

| Endpoint | Purpose |
| :--- | :--- |
| `GET /api/buses` | Latest position of every bus (the map page polls this) |
| `GET /api/buses/:busId/history` | Recent track of one bus (for the route line) |

Both are implemented in the reference server (section 6).

---

## 6. Reference implementation (ready to run)

A complete, working Node.js + Express + SQLite server is printed below. Create a folder
`cse-server/` (with a `public/` subfolder), save the four files exactly as named, then
run it. Everything is included: the API, the database, the live map page, and a
simulator so you can test without any hardware.

### File 1 — `cse-server/package.json`

```json
{
  "name": "bus-tracker-api",
  "version": "1.0.0",
  "private": true,
  "description": "REST API + live map for the university bus tracker (CSE team)",
  "main": "server.js",
  "scripts": {
    "start": "node server.js",
    "simulate": "node simulate.js"
  },
  "engines": {
    "node": ">=18"
  },
  "dependencies": {
    "better-sqlite3": "^11.3.0",
    "express": "^4.19.2"
  }
}
```

### File 2 — `cse-server/server.js`

```js
const express = require('express');
const Database = require('better-sqlite3');

const app = express();
const db = new Database('bus.db');
db.pragma('journal_mode = WAL');

// ---- Schema --------------------------------------------------------
db.exec(`
  CREATE TABLE IF NOT EXISTS bus_locations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    bus_id      TEXT    NOT NULL,
    latitude    REAL    NOT NULL,
    longitude   REAL    NOT NULL,
    speed_kmh   REAL,
    timestamp   TEXT    NOT NULL,
    received_at TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
  );
  CREATE INDEX IF NOT EXISTS idx_bus_time ON bus_locations (bus_id, timestamp);
`);

app.use(express.json({ limit: '10kb' }));

// Optional shared-secret check (recommended once it leaves the lab network)
const REQUIRE_API_KEY = process.env.REQUIRE_API_KEY === '1';
const API_KEY = process.env.API_KEY || '';

function isValidBody(b) {
  if (!b || typeof b !== 'object') return false;
  if (typeof b.bus_id !== 'string' || b.bus_id.length === 0) return false;
  if (typeof b.latitude  !== 'number' || b.latitude  < -90  || b.latitude  > 90)  return false;
  if (typeof b.longitude !== 'number' || b.longitude < -180 || b.longitude > 180) return false;
  if (typeof b.speed_kmh !== 'number' || b.speed_kmh < 0 || b.speed_kmh > 250)    return false;
  if (typeof b.timestamp !== 'string') return false;
  return true;
}

// ---- Device -> DB ----------------------------------------------------
app.post('/api/bus-location', (req, res) => {
  if (REQUIRE_API_KEY && req.body.api_key !== API_KEY) {
    return res.status(401).json({ error: 'invalid api_key' });
  }
  const b = req.body;
  if (!isValidBody(b)) {
    return res.status(400).json({ error: 'invalid payload', expected: 'bus_id, latitude, longitude, speed_kmh, timestamp' });
  }
  db.prepare(
    'INSERT INTO bus_locations (bus_id, latitude, longitude, speed_kmh, timestamp) VALUES (?, ?, ?, ?, ?)'
  ).run(b.bus_id, b.latitude, b.longitude, b.speed_kmh, b.timestamp);
  res.status(201).json({ ok: true });
});

// ---- Map page: latest fix for every bus ------------------------------
app.get('/api/buses', (req, res) => {
  const rows = db.prepare(`
    SELECT bl.bus_id, bl.latitude, bl.longitude, bl.speed_kmh, bl.timestamp
    FROM bus_locations bl
    JOIN (SELECT bus_id, MAX(id) AS max_id FROM bus_locations GROUP BY bus_id) m
      ON bl.id = m.max_id
  `).all();
  res.json(rows);
});

// ---- Map page: recent route of one bus -------------------------------
app.get('/api/buses/:busId/history', (req, res) => {
  const rows = db.prepare(`
    SELECT latitude, longitude, speed_kmh, timestamp
    FROM bus_locations
    WHERE bus_id = ?
    ORDER BY id DESC
    LIMIT 500
  `).all(req.params.busId);
  res.json(rows.reverse());
});

app.use(express.static('public'));

const PORT = process.env.PORT || 80;
app.listen(PORT, '0.0.0.0', () => {
  console.log(`Bus Tracker API running at http://0.0.0.0:${PORT}`);
  console.log(`Live map:  http://localhost:${PORT}/`);
  console.log(`POST endpoint: http://localhost:${PORT}/api/bus-location`);
  if (REQUIRE_API_KEY) console.log('api_key validation: ON');
});
```

### File 3 — `cse-server/public/index.html`

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Bus Tracker - Live Map</title>
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <style>
    html, body, #map { height: 100%; margin: 0; }
    #info {
      position: absolute; top: 10px; right: 10px; z-index: 1000;
      background: rgba(255,255,255,0.9); padding: 6px 10px;
      border-radius: 6px; font: 13px sans-serif; box-shadow: 0 1px 4px rgba(0,0,0,0.3);
    }
  </style>
</head>
<body>
  <div id="info">Refresh: <span id="lastUpdate">-</span></div>
  <div id="map"></div>

  <script>
    // CHANGE THIS to your campus coordinates
    const CAMPUS = [12.9716, 77.5946]; // default: Bengaluru area
    const ZOOM = 14;

    const map = L.map('map').setView(CAMPUS, ZOOM);
    L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxZoom: 19,
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
    }).addTo(map);

    const markers = {}; // bus_id -> marker
    const routes  = {}; // bus_id -> polyline

    function formatTime(ts) {
      if (!ts) return 'no time';
      const d = new Date(ts); // 'YYYY-MM-DDTHH:MM:SSZ' is UTC
      return isNaN(d) ? ts : d.toLocaleString();
    }

    function popupContent(bus) {
      return '<b>' + bus.bus_id + '</b><br>' +
             'Speed: ' + bus.speed_kmh.toFixed(1) + ' km/h<br>' +
             'At: ' + formatTime(bus.timestamp);
    }

    async function refresh() {
      try {
        const buses = await (await fetch('/api/buses')).json();
        for (const bus of buses) {
          const pos = [bus.latitude, bus.longitude];

          if (markers[bus.bus_id]) {
            markers[bus.bus_id].setLatLng(pos).setPopupContent(popupContent(bus));
          } else {
            markers[bus.bus_id] = L.marker(pos)
              .addTo(map)
              .bindPopup(popupContent(bus));
          }

          // recent route line
          const hist = await (await fetch('/api/buses/' + bus.bus_id + '/history')).json();
          const latlngs = hist.map(h => [h.latitude, h.longitude]);
          if (routes[bus.bus_id]) {
            routes[bus.bus_id].setLatLngs(latlngs);
          } else if (latlngs.length > 1) {
            routes[bus.bus_id] = L.polyline(latlngs, { color: '#2563eb', weight: 3 }).addTo(map);
          }
        }
        document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
      } catch (err) {
        document.getElementById('lastUpdate').textContent = 'error: ' + err;
      }
    }

    refresh();
    setInterval(refresh, 5000); // match the device's 10 s reports
  </script>
</body>
</html>
```

### File 4 — `cse-server/simulate.js`

```js
const BUS_ID = process.argv[2] || 'BUS-001';
const URL = process.env.URL || 'http://localhost:80/api/bus-location';

// A small loop the "bus" drives around (change to your campus route)
const loop = [
  [12.9710, 77.5940],
  [12.9750, 77.5960],
  [12.9780, 77.6000],
  [12.9740, 77.6040],
  [12.9700, 77.6010],
];

let i = 0;

async function sendOne() {
  const [lat, lon] = loop[i % loop.length];
  i++;
  const payload = {
    bus_id: BUS_ID,
    latitude: lat + (Math.random() - 0.5) * 0.0004, // tiny jitter like real GPS
    longitude: lon + (Math.random() - 0.5) * 0.0004,
    speed_kmh: 20 + Math.random() * 20,
    timestamp: new Date().toISOString(), // UTC, same as the device
  };

  try {
    const r = await fetch(URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    console.log(new Date().toISOString(), '->', r.status, JSON.stringify(payload));
  } catch (err) {
    console.error('POST failed:', err.message);
  }
}

console.log('Simulating bus', BUS_ID, 'posting to', URL, 'every 10 s');
sendOne();
setInterval(sendOne, 10000);
```

### Run it

```bash
# 1. Install dependencies (Node.js >= 18 required)
cd cse-server
npm install

# 2. Start the server
npm start
```

Open **http://localhost/** in a browser — you should see the live map page.

> If port 80 is taken (or you are not root), start with a different port and
> remember to tell EEE to change the `PORT` constant in `BUS_TRACKER.ino` to match:
> `PORT=3000 npm start` and browse to http://localhost:3000/

### Try it without hardware

```bash
# A curl that looks exactly like a real device post:
curl -X POST http://localhost/api/bus-location \
     -H "Content-Type: application/json" \
     -d '{"bus_id":"BUS-001","latitude":12.9710,"longitude":77.5940,"speed_kmh":30,"timestamp":"2026-08-17T10:00:00Z"}'

# ...or run the simulator (posts every 10 s, like the ESP32):
node simulate.js BUS-002
```

After a few posts, refresh **http://localhost/** and you will see the marker moving.
Open the database with `sqlite3 bus.db` (or any SQLite viewer) to inspect the rows.

### What the reference does / how to extend it
- Stores every post, indexes by `(bus_id, timestamp)`.
- `GET /api/buses` returns the newest row per bus using **insertion order** so a late
  GPRS report never becomes the "current" position incorrectly.
- Validation rejects bad ranges (`latitude > 90`, negative speed, missing fields, ...).
- The map page shows markers + route lines and converts UTC to local time.

---

## 7. Test plan (do this before connecting real hardware)

1. `npm install` and `npm start` — server boots, prints its URL.
2. Send one valid curl (section 6) → returns `201`.
3. Send an invalid curl (e.g. `latitude: 999`) → returns `400` with an error message.
4. Run `node simulate.js BUS-001` for ~1 minute → ~6 rows appear; map marker moves.
5. Confirm the map popup shows speed and a correct local time.
6. Restart the server → data is still there (it is on disk in `bus.db`).

---

## 8. Network / deployment notes

- **Plain HTTP on port 80.** The SIM800L GSM module cannot reliably do HTTPS/TLS,
  so the device posts over plain HTTP. For the prototype, run the API on HTTP.
- **Keep it on the campus/LAN network if possible.** On plain HTTP you do not want
  the endpoint exposed to the whole internet (anyone could post fake data). During
  testing, run it on the same network the devices are on, or a private VPS with a
  firewall that only allows your test network.
- **Fix the server address.** The ESP32 needs a reachable host: either a public IP,
  a domain, or a LAN IP. Give EEE the exact host (and port if not 80) so they can set
  `SERVER` in the firmware.
- If you later need HTTPS, you would put the API behind an HTTPS reverse proxy
  (nginx/Caddy) — the device still talks plain HTTP to the proxy on the LAN.
  This is out of scope for the prototype.

---

## 9. Security (recommended, easy)

Add a shared secret to keep unauthorised people from writing fake rows. The reference
server supports it out of the box:

```bash
# start with validation on
REQUIRE_API_KEY=1 API_KEY=your-secret-123 npm start
```

Then the device must send `"api_key": "your-secret-123"` inside the JSON body. Ask the
EEE team to add that one field to `sendReport()` in `BUS_TRACKER.ino` and hardcode the
same secret. When `REQUIRE_API_KEY` is unset (default), posts are accepted without it,
so the prototype works with the current firmware unchanged.

---

## 10. What EEE needs back from you (checklist)

When the API is ready, give EEE these values:

1. **Host** — e.g. `192.168.1.50`, `bus-tracker.university.edu`, or a public IP.
2. **Port** — `80` (default) or whatever you chose (must match the firmware).
3. **API path** — keep `/api/bus-location` (it is already in the firmware).
4. **The `api_key`** — if you enabled `REQUIRE_API_KEY`.
5. **Your JSON field names** — keep `bus_id`, `latitude`, `longitude`,
   `speed_kmh`, `timestamp`, or tell EEE to change them.

---

## 11. Suggested roadmap

| Milestone | Task |
| :--- | :--- |
| 1 | Run the reference server, pass the test plan (section 7) |
| 2 | Replace `simulate.js` with the real ESP32 (EEE) — one bus |
| 3 | Live road test with 1 bus on campus; tune polling / map view |
| 4 | Add fleet table, route names, driver/vehicle metadata |
| 5 | Add arrival-time estimation (compare bus position to fixed route stops) |
| 6 | Alerts: bus approaching a stop, overspeed, "no data for X min" |
| 7 | Move to PostgreSQL + proper hosting if scalability needs it |

---

## 12. Troubleshooting

| Symptom | Likely cause | Fix |
| :--- | :--- | :--- |
| No rows appearing | Device still getting GPS fix, or wrong host/port in firmware | Check the ESP32 Serial Monitor; confirm the `SERVER`/`PORT` match yours |
| `curl` works but no map marker | Map centered far from the data point | Set `CAMPUS` in `public/index.html` to your coordinates |
| Map page blank | Opening `index.html` from a file, not the server | Browse to `http://<server>/`, never `file://` |
| `npm install` fails on `better-sqlite3` | Native compile issue on your machine | Use Node >= 18; or swap to `sqlite3` / a real DB |
| `EADDRINUSE` | Port already taken | `PORT=3000 npm start` (and tell EEE the port) |
| Latency on the map | 5 s polling too aggressive for a slow server | Raise the `setInterval` in `index.html` to 10000 |

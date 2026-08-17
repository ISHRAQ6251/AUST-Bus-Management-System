// =====================================================================
//  University Bus Tracker
//  ESP32 + NEO-7M GPS + SIM800L GSM (GPRS)
//
//  Reads the GPS every second, and every 10 seconds sends one report
//  to the CSE team's web database (a REST API) as JSON:
//
//      { "bus_id": "BUS-001",
//        "latitude": 12.345678,
//        "longitude": 76.123456,
//        "speed_kmh": 34.2,
//        "timestamp": "2026-08-17T10:00:00Z" }      <- UTC time
//
//  For the most accurate position possible, a report is ONLY sent when
//  the fix is good (>= MIN_SATELLITES satellites AND HDOP <= MAX_HDOP),
//  and the position/speed are averaged over all the valid fixes that
//  arrived during the 10 s window.
//
//  TEST MODE: set USE_BLYNK_TEST to 1 to push the location to the Blynk
//  app instead (Virtual Pin V0 = map, V1 = speed). See BLYNK_TUTORIAL.md.
// =====================================================================

#define TINY_GSM_MODEM_SIM800

#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <TinyGPSPlus.h>

// ---------------------------------------------------------------------
//  TEST / PRODUCTION SWITCH
//    0  = send to the real CSE web database (REST API)     <-- final
//    1  = send to Blynk Virtual Pin V0 for testing on your phone
// ---------------------------------------------------------------------
#define USE_BLYNK_TEST 0

#if USE_BLYNK_TEST
  #define BLYNK_TEMPLATE_ID  "TMPLxxxxxxx"         // from Blynk console
  #define BLYNK_DEVICE_NAME  "Bus Tracker"
  #define BLYNK_AUTH_TOKEN   "your-blynk-auth-token"
  #define BLYNK_PRINT Serial
  #include <Blynk.h>
#endif

// ---------------------------------------------------------------------
//  PLACEHOLDERS - replace these before going live
// ---------------------------------------------------------------------
const char APN[]       = "your-network.apn";   // e.g. "internet", "airtelgprs.com"
const char GPRS_USER[] = "";                   // usually empty
const char GPRS_PASS[] = "";                   // usually empty

const char SERVER[]    = "your-api-server.com"; // CSE team's database host
const uint16_t PORT    = 80;                    // 443 only if the API is HTTPS
const char ENDPOINT[]  = "/api/bus-location";   // REST path that accepts a POST

const char BUS_ID[]    = "BUS-001";             // unique bus identifier

// ---------------------------------------------------------------------
//  TIMING & ACCURACY SETTINGS
// ---------------------------------------------------------------------
const unsigned long SEND_INTERVAL_MS = 10000UL; // one report every 10 s
const int     MIN_SATELLITES = 4;               // ignore fixes below this count
const float   MAX_HDOP       = 3.0;             // ignore fixes worse than this (lower = better)

// ---------------------------------------------------------------------
//  HARDWARE UARTs (common ground on all modules!)
//  GPS : UART2  RX=GPIO16, TX=GPIO17  - NEO-7M
//  GSM : UART1  RX=GPIO26, TX=GPIO27  - SIM800L
// ---------------------------------------------------------------------
HardwareSerial SerialGPS(2);
HardwareSerial SerialAT(1);

const uint32_t GPS_BAUD = 9600;
const uint32_t GSM_BAUD = 9600;

TinyGPSPlus   gps;
TinyGsm       modem(SerialAT);
TinyGsmClient netClient(modem);
HttpClient    http(netClient, SERVER, PORT);

// ---------------------------------------------------------------------
//  Smoothing accumulators (averaged over one send window)
// ---------------------------------------------------------------------
double  sumLat    = 0;
double  sumLon    = 0;
float   sumSpeed  = 0;
uint8_t sampleCount = 0;

unsigned long lastSendMs = 0;

// =====================================================================
void setup() {
  Serial.begin(115200);
  SerialGPS.begin(GPS_BAUD, SERIAL_8N1, 16, 17);
  SerialAT.begin(GSM_BAUD, SERIAL_8N1, 26, 27);

  delay(2000);                          // let the GPS and GSM power up

  Serial.println(F("University Bus Tracker boot"));
  Serial.println(F("GPS: waiting for a fix (clear sky, first fix can take minutes)..."));

  #if USE_BLYNK_TEST
    Blynk.begin(BLYNK_AUTH_TOKEN, modem, APN, GPRS_USER, GPRS_PASS);
    Serial.println(F("MODE: testing -> sending to Blynk V0/V1"));
  #else
    connectToNetwork();
    Serial.println(F("MODE: production -> sending to REST API"));
  #endif
}

// =====================================================================
void loop() {
  while (SerialGPS.available()) {
    gps.encode(SerialGPS.read());
  }

  #if USE_BLYNK_TEST
    Blynk.run();
  #endif

  // Accumulate every new, accurate fix that arrives during the window
  if (gps.location.isUpdated() && hasAccurateFix()) {
    sumLat   += gps.location.lat();
    sumLon   += gps.location.lng();
    sumSpeed += gps.speed.kmph();
    sampleCount++;
  }

  // One report per window
  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    if (sampleCount > 0) {
      sendReport();
    } else {
      Serial.println(F("No accurate fix this window - nothing sent"));
    }
  }
}

// =====================================================================
//  True only when the GPS position is trustworthy
// =====================================================================
bool hasAccurateFix() {
  if (!gps.location.isValid())                       return false;
  if (gps.satellites.isValid() &&
      gps.satellites.value() < MIN_SATELLITES)       return false;
  if (gps.hdop.isValid() &&
      gps.hdop.hdop() > MAX_HDOP)                    return false;
  return true;
}

// =====================================================================
//  Average the window's samples and upload the report
// =====================================================================
void sendReport() {
  double lat = sumLat / sampleCount;
  double lon = sumLon / sampleCount;
  float  kmh = sumSpeed / sampleCount;

  String payload = String("{\"bus_id\":\"") + BUS_ID +
                   "\",\"latitude\":"    + String(lat, 6) +
                   ",\"longitude\":"     + String(lon, 6) +
                   ",\"speed_kmh\":"     + String(kmh, 1) +
                   ",\"timestamp\":\""   + gpsTimestamp() + "\"}";

  Serial.println(F("--- report ---"));
  Serial.println(payload);

  #if USE_BLYNK_TEST
    Blynk.virtualWrite(V0, lat, lon);   // Map widget
    Blynk.virtualWrite(V1, kmh);        // Speed gauge
    Serial.println(F("Sent to Blynk: V0 (map), V1 (speed)"));
  #else
    Serial.print(F("POSTing to server... "));
    int code = http.post(ENDPOINT, "application/json", payload);
    if (code > 0) {
      Serial.print(F("HTTP "));
      Serial.println(code);
    } else {
      Serial.println(F("failed - check SIM signal, APN, server address"));
    }
    http.stop();
  #endif

  sumLat = 0; sumLon = 0; sumSpeed = 0; sampleCount = 0;
}

// =====================================================================
//  Format the GPS UTC time as YYYY-MM-DDTHH:MM:SSZ
// =====================================================================
String gpsTimestamp() {
  char buf[24];
  if (gps.date.isValid() && gps.time.isValid()) {
    snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             (unsigned)gps.date.year(), (unsigned)gps.date.month(),
             (unsigned)gps.date.day(),
             (unsigned)gps.time.hour(), (unsigned)gps.time.minute(),
             (unsigned)gps.time.second());
  } else {
    snprintf(buf, sizeof(buf), "0000-00-00T00:00:00Z");
  }
  return String(buf);
}

// =====================================================================
//  Boot the SIM800L and open the GPRS data connection
// =====================================================================
void connectToNetwork() {
  Serial.print(F("GSM: restarting module... "));
  modem.restart();
  Serial.println(F("done"));
  Serial.println(modem.getModemInfo());

  Serial.print(F("GSM: waiting for network... "));
  if (!modem.waitForNetwork()) {
    Serial.println(F("FAILED (check SIM card / signal)"));
    return;
  }
  Serial.println(F("OK"));

  Serial.print(F("GPRS: connecting with APN '"));
  Serial.print(APN);
  Serial.print(F("'... "));
  if (!modem.gprsConnect(APN, GPRS_USER, GPRS_PASS)) {
    Serial.println(F("FAILED (check APN from your telecom operator)"));
    return;
  }
  Serial.println(F("OK"));
}

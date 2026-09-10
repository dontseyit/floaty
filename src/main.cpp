/*
 * FLOATY flap servos - ESP32-C3 Super Mini + HW-566 expansion board
 * 2x GH-S37D micro servos
 *
 * Wiring (HW-566 3-pin servo headers: S / V / G)
 *   Servo 1 signal -> GPIO4
 *   Servo 2 signal -> GPIO5
 *   V  -> 5V rail on the HW-566 (NOT the C3's 3V3 pin)
 *   G  -> GND, shared with the ESP32-C3
 *
 * Two control surfaces, both live at once:
 *   - Serial monitor @115200, type "h" for the command list
 *   - Web console: join the board's own WiFi "FLOATY-SERVO"
 *     (pass "servotest123"), open http://192.168.4.1 or http://floaty.local
 *
 * Each servo has its own sweep range (from -> to). Sweep drives both on one
 * shared phase, so they hit their ends at the same instant however wide each
 * range is. Ranges and stroke time are saved to flash and survive a reboot.
 */

#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>
#include "index_html.h"


// ---------------------------------------------------------------- config
static const int SERVO1_PIN = 4;
static const int SERVO2_PIN = 5;

// Onboard LED on the Super Mini. Active LOW. Blinks as a heartbeat so you can
// tell the firmware is running even when the serial monitor shows nothing.
static const int LED_PIN = 8;

// GH-S37D is a 9g-class analog servo: 50 Hz frame, ~500-2500 us pulse.
// If a servo buzzes or slams at an end stop, narrow this to 600/2400.
static const int PULSE_MIN_US = 500;
static const int PULSE_MAX_US = 2500;
static const int SERVO_HZ     = 50;

static const int ANGLE_MIN = 0;
static const int ANGLE_MAX = 180;

// Sweep timing. Stroke = one pass from -> to; a full flap is two strokes.
// If the stroke is shorter than the servo can physically travel its range,
// it turns back before reaching the ends - the flap just gets shallower.
static const int STROKE_MS_MIN     = 150;
static const int STROKE_MS_MAX     = 3000;
static const int STROKE_MS_DEFAULT = 1000;
static const unsigned long SWEEP_TICK_MS = 10;   // servo frame is 20 ms; stay ahead of it

// Ranges are written to flash this long after the last change, so dragging a
// slider costs one write instead of hundreds.
static const unsigned long SAVE_DELAY_MS = 1500;

// WiFi: access-point only. The board makes its own network, so there is no
// router to depend on and no credentials to keep in sync.
static const char* AP_SSID  = "FLOATY-SERVO";
static const char* AP_PASS  = "servotest123";   // >= 8 chars, or the AP stays open
static const char* HOSTNAME = "floaty";

static const int  AP_CHANNEL     = 1;   // 1, 6 or 11 - the non-overlapping ones
static const int  AP_MAX_CLIENTS = 4;

// The C3 Super Mini's onboard antenna is poorly matched and sits on an
// undersized ground plane. At full power (19.5 dBm) the supply droop distorts
// the output enough that the beacon can be unreadable even a metre away, so
// the AP looks absent. Backing off to 8.5 dBm makes it appear reliably.
// If it shows up but the range is short, try WIFI_POWER_11dBm.
static const wifi_power_t AP_TX_POWER = WIFI_POWER_8_5dBm;
static const IPAddress AP_IP     (192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET (255, 255, 255, 0);

// ---------------------------------------------------------------- state
Servo servo1;
Servo servo2;
WebServer server(80);
Preferences prefs;

bool attached = false;

int angle1 = 90;
int angle2 = 90;

// from > to is allowed and simply reverses the direction of travel. Servo 2
// defaults to mirrored because wing servos on opposite sides of a body face
// opposite ways, so the same flap needs opposite rotation.
struct Range { int from; int to; };
Range range1 = {   0, 180 };
Range range2 = { 180,   0 };

int strokeMs = STROKE_MS_DEFAULT;

enum Mode { MODE_IDLE, MODE_SWEEP, MODE_CENTER, MODE_ENDPOINTS };
Mode mode = MODE_IDLE;

// sweep state
float sweepPhase = 0.0f;          // 0..1 over one full flap: from -> to -> from
unsigned long lastSweepMs = 0;

// endpoints state
bool endpointAtTo = false;
unsigned long lastEndpointMs = 0;

bool settingsDirty = false;
unsigned long dirtySinceMs = 0;

// ---------------------------------------------------------------- helpers
static void printWifiStatus();   // defined in the web section below

static int clampAngle(int a) {
  if (a < ANGLE_MIN) return ANGLE_MIN;
  if (a > ANGLE_MAX) return ANGLE_MAX;
  return a;
}

static int clampStroke(int ms) {
  if (ms < STROKE_MS_MIN) return STROKE_MS_MIN;
  if (ms > STROKE_MS_MAX) return STROKE_MS_MAX;
  return ms;
}

// Writes in microseconds rather than whole degrees, so slow strokes glide
// instead of stepping one degree at a time.
static void setServo(Servo& s, int& angleOut, float a) {
  if (a < ANGLE_MIN) a = ANGLE_MIN;
  if (a > ANGLE_MAX) a = ANGLE_MAX;
  angleOut = (int)lroundf(a);
  float span = (float)(PULSE_MAX_US - PULSE_MIN_US) / (ANGLE_MAX - ANGLE_MIN);
  s.writeMicroseconds(PULSE_MIN_US + (int)lroundf((a - ANGLE_MIN) * span));
}

static void write1(float a) { setServo(servo1, angle1, a); }
static void write2(float a) { setServo(servo2, angle2, a); }

static void writeServo(int i, float a) {
  if (i == 1) write1(a); else write2(a);
}

static void writeBoth(float a) {
  write1(a);
  write2(a);
}

static Range& rangeFor(int i) {
  return i == 1 ? range1 : range2;
}

// The middle of a range is the one pose guaranteed to be inside what the
// linkage was calibrated for, so it's the safe place to park.
static void parkAtMid() {
  write1((range1.from + range1.to) / 2.0f);
  write2((range2.from + range2.to) / 2.0f);
}

static void markDirty() {
  settingsDirty = true;
  dirtySinceMs = millis();
}

static void loadSettings() {
  // Opened read-write so the namespace is created on first boot; a read-only
  // open of a missing namespace logs an error.
  prefs.begin("floaty", false);
  range1.from = clampAngle(prefs.getInt("r1f", range1.from));
  range1.to   = clampAngle(prefs.getInt("r1t", range1.to));
  range2.from = clampAngle(prefs.getInt("r2f", range2.from));
  range2.to   = clampAngle(prefs.getInt("r2t", range2.to));
  strokeMs    = clampStroke(prefs.getInt("stroke", strokeMs));
  prefs.end();
}

static void saveSettingsIfDue() {
  if (!settingsDirty || millis() - dirtySinceMs < SAVE_DELAY_MS) return;
  settingsDirty = false;
  prefs.begin("floaty", false);
  prefs.putInt("r1f", range1.from);
  prefs.putInt("r1t", range1.to);
  prefs.putInt("r2f", range2.from);
  prefs.putInt("r2t", range2.to);
  prefs.putInt("stroke", strokeMs);
  prefs.end();
  Serial.println(F("settings saved"));
}

static void printHelp() {
  Serial.println();
  Serial.println(F("=== FLOATY flap servos ==="));
  Serial.println(F("  s            sweep both through their ranges, in sync"));
  Serial.println(F("  e            step both between range ends, 1 s each"));
  Serial.println(F("  x            stop motion, hold current position"));
  Serial.println(F("  c            center both (90 deg)"));
  Serial.println(F("  d            detach both (servos go limp, no PWM)"));
  Serial.println(F("  a            re-attach both, park at range middle"));
  Serial.println(F("  1<ang>       set servo 1 angle, e.g. 145"));
  Serial.println(F("  2<ang>       set servo 2 angle, e.g. 20"));
  Serial.println(F("  b<ang>       set both,          e.g. b90"));
  Serial.println(F("  r<i> <f> <t> set sweep range,   e.g. r1 30 150"));
  Serial.println(F("               (from > to reverses direction)"));
  Serial.println(F("  +/-          shorter / longer stroke (50 ms)"));
  Serial.println(F("  p            print current state"));
  Serial.println(F("  w            print wifi / access point status"));
  Serial.println(F("  h            this help"));
  Serial.println();
}

static const char* modeName(Mode m) {
  switch (m) {
    case MODE_SWEEP:     return "sweep";
    case MODE_CENTER:    return "center";
    case MODE_ENDPOINTS: return "endpoints";
    case MODE_IDLE:
    default:             return "idle";
  }
}

static void printState() {
  Serial.printf("servo1: GPIO%d  %3d deg (%d us)  range %d -> %d\n",
                SERVO1_PIN, angle1, servo1.readMicroseconds(),
                range1.from, range1.to);
  Serial.printf("servo2: GPIO%d  %3d deg (%d us)  range %d -> %d\n",
                SERVO2_PIN, angle2, servo2.readMicroseconds(),
                range2.from, range2.to);
  Serial.printf("mode: %s   stroke: %d ms (%.2f flaps/s)\n",
                modeName(mode), strokeMs, 1000.0f / (2.0f * strokeMs));
}

static void attachServos() {
  servo1.setPeriodHertz(SERVO_HZ);
  servo2.setPeriodHertz(SERVO_HZ);
  servo1.attach(SERVO1_PIN, PULSE_MIN_US, PULSE_MAX_US);
  servo2.attach(SERVO2_PIN, PULSE_MIN_US, PULSE_MAX_US);
  attached = true;
  Serial.println(F("servos attached"));
}

static void detachServos() {
  servo1.detach();
  servo2.detach();
  attached = false;
  Serial.println(F("servos detached (limp)"));
}

static void startSweep() {
  // Re-selecting sweep while already sweeping must not restart the phase,
  // or every tap of the button would jerk the wings back to "from".
  if (mode != MODE_SWEEP) {
    sweepPhase = 0.0f;
    lastSweepMs = millis();
  }
  mode = MODE_SWEEP;
}

static void startEndpoints() {
  mode = MODE_ENDPOINTS;
  endpointAtTo = true;   // first tick flips this, so motion starts at "from"
  lastEndpointMs = 0;
}

static void setRange(int i, int from, int to) {
  Range& r = rangeFor(i);
  r.from = clampAngle(from);
  r.to   = clampAngle(to);
  markDirty();
}

static void setStroke(int ms) {
  strokeMs = clampStroke(ms);
  markDirty();
}

// ---------------------------------------------------------------- commands
static void handleCommand(const String& raw) {
  String cmd = raw;
  cmd.trim();
  if (cmd.length() == 0) return;

  char c = cmd.charAt(0);
  String arg = cmd.substring(1);
  arg.trim();

  switch (c) {
    case 's':
      startSweep();
      Serial.println(F("mode: sweep"));
      break;

    case 'c':
      mode = MODE_CENTER;
      writeBoth(90);
      Serial.println(F("mode: center (90 deg)"));
      break;

    case 'e':
      startEndpoints();
      Serial.println(F("mode: endpoints (range from <-> to)"));
      break;

    case 'x':
      mode = MODE_IDLE;
      Serial.println(F("mode: idle (holding)"));
      break;

    case 'd':
      mode = MODE_IDLE;
      detachServos();
      break;

    case 'a':
      attachServos();
      parkAtMid();
      break;

    case '1':
      mode = MODE_IDLE;
      write1(arg.toInt());
      Serial.printf("servo1 -> %d deg\n", angle1);
      break;

    case '2':
      mode = MODE_IDLE;
      write2(arg.toInt());
      Serial.printf("servo2 -> %d deg\n", angle2);
      break;

    case 'b':
      mode = MODE_IDLE;
      writeBoth(arg.toInt());
      Serial.printf("both -> %d deg\n", angle1);
      break;

    case 'r': {
      int i, from, to;
      if (sscanf(arg.c_str(), "%d %d %d", &i, &from, &to) == 3 && (i == 1 || i == 2)) {
        setRange(i, from, to);
        Range& r = rangeFor(i);
        Serial.printf("servo%d range -> %d -> %d\n", i, r.from, r.to);
      } else {
        Serial.println(F("usage: r<servo> <from> <to>   e.g. r1 30 150"));
      }
      break;
    }

    case '+':
      setStroke(strokeMs - 50);
      Serial.printf("stroke: %d ms\n", strokeMs);
      break;

    case '-':
      setStroke(strokeMs + 50);
      Serial.printf("stroke: %d ms\n", strokeMs);
      break;

    case 'p':
      printState();
      break;

    case 'w':
      printWifiStatus();
      break;

    case 'h':
    case '?':
      printHelp();
      break;

    default:
      Serial.printf("unknown command: '%s'  (h for help)\n", cmd.c_str());
      break;
  }
}

static void pollSerial() {
  static String buf;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (buf.length()) {
        handleCommand(buf);
        buf = "";
      }
    } else if (buf.length() < 32) {
      buf += ch;
    }
  }
}


// ---------------------------------------------------------------- web
// Every endpoint answers with the full state, so the browser stays in sync from
// a single round trip instead of a command plus a follow-up poll.
static void sendState() {
  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"a1\":%d,\"a2\":%d,\"p1\":%d,\"p2\":%d,"
           "\"r1f\":%d,\"r1t\":%d,\"r2f\":%d,\"r2t\":%d,"
           "\"mode\":\"%s\",\"stroke\":%d,\"smin\":%d,\"smax\":%d,"
           "\"attached\":%s}",
           angle1, angle2, SERVO1_PIN, SERVO2_PIN,
           range1.from, range1.to, range2.from, range2.to,
           modeName(mode), strokeMs, STROKE_MS_MIN, STROKE_MS_MAX,
           attached ? "true" : "false");
  server.send(200, "application/json", buf);
}

// Returns fallback when the arg is absent so a malformed request cannot move a
// servo to an unintended angle.
static int argInt(const char* name, int fallback) {
  if (!server.hasArg(name)) return fallback;
  return server.arg(name).toInt();
}

static void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleServo() {
  int i = argInt("i", 0);
  int a = argInt("a", 90);
  if (i == 1 || i == 2) {
    mode = MODE_IDLE;
    if (!attached) attachServos();
    writeServo(i, a);
  }
  sendState();
}

static void handleBoth() {
  mode = MODE_IDLE;
  if (!attached) attachServos();
  writeBoth(argInt("a", 90));
  sendState();
}

static void handleRange() {
  int i = argInt("i", 0);
  if (i == 1 || i == 2) {
    Range& r = rangeFor(i);
    setRange(i, argInt("from", r.from), argInt("to", r.to));

    // While holding, move the servo to the end being edited so the range can
    // be set by eye against the real linkage. Sweep and endpoints pick the new
    // values up on their own next tick, so they are left alone.
    String pv = server.arg("preview");
    if ((mode == MODE_IDLE || mode == MODE_CENTER) && (pv == "from" || pv == "to")) {
      if (!attached) attachServos();
      writeServo(i, pv == "from" ? r.from : r.to);
    }
  }
  sendState();
}

static void handleMode() {
  String m = server.arg("m");
  if (!attached) attachServos();
  if      (m == "sweep")     { startSweep(); }
  else if (m == "endpoints") { startEndpoints(); }
  else if (m == "center")    { mode = MODE_CENTER; writeBoth(90); }
  else                       { mode = MODE_IDLE; }
  Serial.printf("[web] mode -> %s\n", modeName(mode));
  sendState();
}

static void handleSpeed() {
  setStroke(argInt("ms", strokeMs));
  sendState();
}

static void handleAttach() {
  if (argInt("on", 1)) {
    attachServos();
  } else {
    mode = MODE_IDLE;
    detachServos();
  }
  sendState();
}

static void printWifiStatus() {
  Serial.println();
  Serial.println(F("--- wifi ---"));
  Serial.printf("mode     : %s\n", WiFi.getMode() == WIFI_AP ? "AP" : "NOT AP (!)");
  Serial.printf("ssid     : %s\n", AP_SSID);
  Serial.printf("pass     : %s\n", AP_PASS);
  Serial.printf("channel  : %d\n", AP_CHANNEL);
  Serial.printf("ap mac   : %s\n", WiFi.softAPmacAddress().c_str());
  Serial.printf("ap ip    : %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("tx power : %d (x0.25 dBm)\n", (int)WiFi.getTxPower());
  Serial.printf("clients  : %d\n", WiFi.softAPgetStationNum());
  Serial.printf("open     : http://%s\n", WiFi.softAPIP().toString().c_str());
  Serial.println(F("------------"));
}

static void startWifi() {
  // Don't write credentials to NVS on every boot - they are compiled in, and
  // the flash writes are a needless failure point.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);

  // Fix the subnet before bringing the AP up, so the URL never moves.
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);

  if (!WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, false, AP_MAX_CLIENTS)) {
    // Servos still work over serial, so keep running rather than rebooting.
    Serial.println(F("softAP failed - web console unavailable, serial still works"));
    return;
  }

  // Modem sleep adds up to ~100 ms of latency per request and is the usual
  // cause of an AP that feels laggy or drops mid-drag. The board is USB
  // powered, so there is nothing to gain from it.
  WiFi.setSleep(false);
  WiFi.setTxPower(AP_TX_POWER);

  printWifiStatus();

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("also: http://%s.local\n", HOSTNAME);
  }
}

static void startServer() {
  server.on("/",            handleRoot);
  server.on("/api/state",   sendState);
  server.on("/api/servo",   handleServo);
  server.on("/api/both",    handleBoth);
  server.on("/api/range",   handleRange);
  server.on("/api/mode",    handleMode);
  server.on("/api/speed",   handleSpeed);
  server.on("/api/attach",  handleAttach);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  Serial.println(F("http server up on port 80"));
}

// ---------------------------------------------------------------- motion
// Cosine profile: 0 at "from", 1 at "to". The wing eases into each end and
// reverses smoothly instead of slamming the linkage at full speed, which reads
// as a natural flap and is far kinder to plastic gears than a triangle wave.
static float strokePos(float phase) {
  return 0.5f - 0.5f * cosf(2.0f * PI * phase);
}

static void runSweep() {
  unsigned long now = millis();
  unsigned long dt = now - lastSweepMs;
  if (dt < SWEEP_TICK_MS) return;
  lastSweepMs = now;

  // A page load can stall the loop for tens of ms. Resume from where the wings
  // are rather than skipping ahead and lurching to catch up.
  if (dt > 100) dt = SWEEP_TICK_MS;

  sweepPhase += dt / (2.0f * strokeMs);
  sweepPhase -= floorf(sweepPhase);

  // One shared phase drives both servos, so they reach their ends together
  // even when one range is much wider than the other.
  float p = strokePos(sweepPhase);
  write1(range1.from + (range1.to - range1.from) * p);
  write2(range2.from + (range2.to - range2.from) * p);
}

static void runEndpoints() {
  unsigned long now = millis();
  if (lastEndpointMs != 0 && now - lastEndpointMs < 1000) return;
  lastEndpointMs = now;

  endpointAtTo = !endpointAtTo;
  write1(endpointAtTo ? range1.to : range1.from);
  write2(endpointAtTo ? range2.to : range2.from);
  Serial.printf("endpoint -> %s  (servo1 %d, servo2 %d)\n",
                endpointAtTo ? "to" : "from", angle1, angle2);
}

// ---------------------------------------------------------------- arduino
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);   // active low -> off

  Serial.begin(115200);

  // Serial here is HWCDC (native USB-Serial-JTAG). It drops anything written
  // while no host is attached, and the USB device re-enumerates on reset, so
  // wait for the host to actually come back before printing the banner.
  // Bail out after 3 s so the servos still run with no monitor attached.
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {
    digitalWrite(LED_PIN, LOW);
    delay(50);
    digitalWrite(LED_PIN, HIGH);
    delay(50);
  }
  // Never block on a write when nothing is listening.
  Serial.setTxTimeoutMs(0);
  delay(200);

  Serial.println();
  Serial.println(F("ESP32-C3 Super Mini + HW-566 - FLOATY flap servos boot"));

  loadSettings();

  // ESP32Servo needs timers allocated before attach()
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // Boot parked and holding, not sweeping: with a linkage attached, a
  // mechanism that starts flapping the moment power is applied is a hazard.
  attachServos();
  parkAtMid();
  mode = MODE_IDLE;
  delay(500);

  printHelp();
  printState();

  startWifi();
  startServer();
}

static void heartbeat() {
  static unsigned long lastBlinkMs = 0;
  static bool on = false;
  unsigned long now = millis();
  if (now - lastBlinkMs < 500) return;
  lastBlinkMs = now;
  on = !on;
  digitalWrite(LED_PIN, on ? LOW : HIGH);   // active low
}

static void reportClients() {
  static int lastCount = -1;
  static unsigned long lastCheckMs = 0;
  unsigned long now = millis();
  if (now - lastCheckMs < 1000) return;
  lastCheckMs = now;

  int n = WiFi.softAPgetStationNum();
  if (n != lastCount) {
    lastCount = n;
    Serial.printf("[wifi] clients connected: %d\n", n);
  }
}

void loop() {
  heartbeat();
  pollSerial();
  reportClients();
  server.handleClient();
  saveSettingsIfDue();

  switch (mode) {
    case MODE_SWEEP:     runSweep();     break;
    case MODE_ENDPOINTS: runEndpoints(); break;
    case MODE_CENTER:
    case MODE_IDLE:
    default:                             break;
  }
}

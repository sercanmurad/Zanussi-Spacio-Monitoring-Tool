
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ─── user configuration
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const String BOT_TOKEN    = "123456789:ABCdefGHIjklMNOpqrsTUVwxyz";  // from @BotFather telegram tool
const String CHAT_ID      = "987654321";                               // your chat ID
const long   UTC_OFFSET   = 7200;   // second UTC
// ─────────────────────────────────────────────────────────────

// ─── PIN DEFINITIONS ─────────────────────────────────────────
#define UART2_RX      16    // ← YP-05 TXD
#define UART2_TX      17    // → YP-05 RXD
#define PIN_LED_5V    32    // HW-221 A1 (LED1 — 5V power OK)
#define PIN_LED_24V   33    // HW-221 A2 (LED2 — 24V power OK)
#define PIN_LED_CPU    4    // HW-221 A3 (LED3 — CPU running, blinks)
#define PIN_LED_MOTOR  5    // HW-221 A4 (LED5 — motor active)
#define PIN_LED_ERR    6    // HW-221 A5 (LED6 — motor overcurrent ALARM)
#define PIN_STATUS_LED 2    // onboard LED (shows WiFi/system status)
// ─────────────────────────────────────────────────────────────

// ─── INTERVALS (milliseconds) ────────────────────────────────
#define INTERVAL_LED_CHECK      2000UL    // how often to poll LED pins
#define INTERVAL_DAILY_REPORT  86400000UL // 24 h — daily sales report
#define INTERVAL_STATUS_REPORT 21600000UL // 6 h  — system health report
#define INTERVAL_WIFI_CHECK     30000UL   // WiFi reconnect attempt
#define DEBOUNCE_ERROR_MS        5000UL   // min ms between same error alerts
// ─────────────────────────────────────────────────────────────

// ─── RS232 RAW KEYWORDS (Italian/English printer output) ─────
const char* KEYWORDS_SALE[]    = { "VEND", "SALE", "SELE", "EROG", "DISP", "SEL.", nullptr };
const char* KEYWORDS_TEMP[]    = { "TEMP", "°C", "GRADI", "BOIL", "BOLL", nullptr };
const char* KEYWORDS_ERROR[]   = { "ERROR", "ERR", "FAULT", "ALARM", "ALLAR",
                                    "FAIL", "GUAST", "BLOCCO", "BLOCK", nullptr };
const char* KEYWORDS_EMPTY[]   = { "EMPTY", "VUOTO", "ESAUR", "NO CUP", "NIENTE",
                                    "GROUNDS", "SCARICO", nullptr };
const char* KEYWORDS_WATER[]   = { "WATER", "ACQUA", "NO WATER", "MANCA ACQUA",
                                    "AIR BREAK", "AIRBREAK", nullptr };
const char* KEYWORDS_STATS[]   = { "STATIST", "TOTAL", "TOTALE", "COUNTER",
                                    "CONTATORE", "PRINT", "STAMPA", nullptr };
// ─────────────────────────────────────────────────────────────

// ─── GLOBAL STATE ────────────────────────────────────────────
String  rxBuffer = "";

// Sales counters
int     salesDay   = 0;
int     salesMonth = 0;
int     salesTotal = 0;

// Boiler temperatures (updated when temp lines are parsed)
float   tempCoffeeBoiler   = -1.0;
float   tempInstantBoiler  = -1.0;

// Timestamps
unsigned long lastLedCheck      = 0;
unsigned long lastDailyReport   = 0;
unsigned long lastStatusReport  = 0;
unsigned long lastWifiCheck     = 0;
unsigned long lastErrorTime[5]  = {0};

// LED states (for change detection)
bool    prevLed5v    = true;
bool    prevLed24v   = true;
bool    prevLedCpu   = true;
bool    prevLedMotor = false;
bool    prevLedErr   = false;

// CPU blink detection
int     cpuBlinkCount  = 0;
bool    cpuLastState   = false;
unsigned long cpuBlinkTime = 0;

// Daily reset time tracking
int     lastResetDay   = -1;
int     lastResetMonth = -1;
// ─────────────────────────────────────────────────────────────

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n[BOOT] Zanussi Spacio C6 Monitor starting..."));

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    pinMode(PIN_LED_5V,    INPUT);
    pinMode(PIN_LED_24V,   INPUT);
    pinMode(PIN_LED_CPU,   INPUT);
    pinMode(PIN_LED_MOTOR, INPUT);
    pinMode(PIN_LED_ERR,   INPUT);

    Serial2.begin(9600, SERIAL_8N1, UART2_RX, UART2_TX);
    Serial.println(F("[UART2] RS232 @ 9600 8N1 ready"));

    connectWiFi();

    // NTP time sync
    configTime(UTC_OFFSET, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print(F("[NTP] Syncing time"));
    struct tm ti;
    int attempts = 0;
    while (!getLocalTime(&ti) && attempts < 20) {
        Serial.print(".");
        delay(500);
        attempts++;
    }
    Serial.println(getLocalTime(&ti) ? F(" OK") : F(" FAILED (no NTP)"));

    // Startup Telegram message
    String msg = "☕ *Zanussi Spacio C6 Monitor v2.0*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "✅ System Started \n";
    msg += "📶 WiFi: `" + WiFi.localIP().toString() + "`\n";
    msg += "🕐 " + getTimeString() + "\n\n";
    msg += "📌 *Looking for signal:*\n";
    msg += "• RS232 Port (sells, errors, temperature)\n";
    msg += "• LED1 — 5V \n";
    msg += "• LED2 — 24V \n";
    msg += "• LED3 — CPU status\n";
    msg += "• LED5 — engine\n";
    msg += "• LED6 — high voltage (alarm)\n\n";
    msg += " /help for commands_";
    sendTelegram(msg);

    Serial.println(F("[BOOT] Ready. Listening..."));
}

// ════════════════════════════════════════════════════════════
//  MAIN LOOP
// ════════════════════════════════════════════════════════════
void loop() {
    unsigned long now = millis();

    // 1. Read RS232 data from machine
    readRS232();

    // 2. Poll LED status pins
    if (now - lastLedCheck >= INTERVAL_LED_CHECK) {
        checkLEDs();
        lastLedCheck = now;
    }

    // 3. Daily report + sales counter reset at midnight
    checkDailyReset();

    // 4. Periodic status report every 6 hours
    if (now - lastStatusReport >= INTERVAL_STATUS_REPORT) {
        sendStatusReport();
        lastStatusReport = now;
    }

    // 5. WiFi watchdog
    if (now - lastWifiCheck >= INTERVAL_WIFI_CHECK) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[WiFi] Connection lost — reconnecting..."));
            connectWiFi();
        }
        lastWifiCheck = now;
    }
}

// ════════════════════════════════════════════════════════════
//  RS232 READING & PARSING
// ════════════════════════════════════════════════════════════
void readRS232() {
    while (Serial2.available()) {
        char c = Serial2.read();
        if (c == '\n' || c == '\r') {
            if (rxBuffer.length() > 1) {
                processLine(rxBuffer);
                rxBuffer = "";
            }
        } else {
            rxBuffer += c;
            if (rxBuffer.length() > 200) rxBuffer = "";
        }
    }
}

// Called for each complete line received from the machine
void processLine(String line) {
    line.trim();
    if (line.length() < 2) return;

    // Mirror to Serial monitor for debugging
    Serial.print(F("[RS232] "));
    Serial.println(line);

    String lineUp = line;
    lineUp.toUpperCase();

    // ── SALE detected ─────────────────────────────────────
    if (containsAny(lineUp, KEYWORDS_SALE)) {
        salesDay++;
        salesMonth++;
        salesTotal++;

        // Typical format: "SELECTION 01 ESPRESSO"  or  "SELE 3 CAFFE"
        String product = extractProduct(lineUp);

        String msg = "☕ *Sales #" + String(salesDay) + " today*\n";
        if (product.length() > 0) msg += "📦 Product: `" + product + "`\n";
        msg += "📊 Mounth: " + String(salesMonth) + "  |  Total: " + String(salesTotal) + "\n";
        msg += "🕐 " + getTimeString();
        sendTelegram(msg);
        return;
    }

    // ── TEMPERATURE data ───────────────────────────────────
    if (containsAny(lineUp, KEYWORDS_TEMP)) {
        parseTemperature(lineUp, line);
        return;
    }

    // ── ERROR / ALARM ──────────────────────────────────────
    if (containsAny(lineUp, KEYWORDS_ERROR)) {
        String msg = "🔴 *Error from machine!*\n";
        msg += "```\n" + line + "\n```\n";
        msg += "🕐 " + getTimeString();
        sendTelegram(msg);
        return;
    }

    // ── EMPTY / OUT OF STOCK ───────────────────────────────
    if (containsAny(lineUp, KEYWORDS_EMPTY)) {
        String msg = "⚠️ *Warning:*\n";
        msg += "```\n" + line + "\n```\n";
        msg += "🕐 " + getTimeString();
        sendTelegram(msg);
        return;
    }

    // ── WATER problem ──────────────────────────────────────
    if (containsAny(lineUp, KEYWORDS_WATER)) {
        String msg = "💧 *Problem with water!*\n";
        msg += "```\n" + line + "\n```\n";
        msg += "🕐 " + getTimeString();
        sendTelegram(msg);
        return;
    }

    // ── STATISTICS block ───────────────────────────────────
    if (containsAny(lineUp, KEYWORDS_STATS)) {
        // Accumulate statistics lines and send as one block
        // (statistics print takes several seconds — collect for 3s)
        static String statsBuffer = "";
        static unsigned long statsStart = 0;

        if (statsBuffer.length() == 0) {
            statsStart = millis();
            statsBuffer = "📋 *Stastics from machine:*\n```\n";
        }
        statsBuffer += line + "\n";

        // Flush stats buffer after 5 seconds of no new stats lines
        if (millis() - statsStart > 5000 && statsBuffer.length() > 30) {
            statsBuffer += "```";
            sendTelegram(statsBuffer);
            statsBuffer = "";
        }
        return;
    }
}

// Extract product name from a sale line
String extractProduct(String lineUp) {
    int idx = lineUp.indexOf("ESPRESSO");
    if (idx >= 0) return "Espresso";
    idx = lineUp.indexOf("CAPPUCC");
    if (idx >= 0) return "Cappuccino";
    idx = lineUp.indexOf("CAFFE");
    if (idx >= 0) return "Caffe";
    idx = lineUp.indexOf("COFFEE");
    if (idx >= 0) return "Coffee";
    idx = lineUp.indexOf("CHOCOLATE");
    if (idx >= 0) return "Chocolate";
    idx = lineUp.indexOf("CIOCCO");
    if (idx >= 0) return "Cioccolata";
    idx = lineUp.indexOf("TEA");
    if (idx >= 0) return "Tea";
    idx = lineUp.indexOf("THE");
    if (idx >= 0) return "The";
    idx = lineUp.indexOf("HOT WATER");
    if (idx >= 0) return "Hot Water";
    idx = lineUp.indexOf("SOUP");
    if (idx >= 0) return "Soup";
    idx = lineUp.indexOf("BROTH");
    if (idx >= 0) return "Broth";
    return "";  // unknown product — no label
}

void parseTemperature(String lineUp, String lineOrig) {
    int temp = extractNumber(lineUp);
    if (temp < 0 || temp > 200) return; 

    bool isCoffee  = lineUp.indexOf("CAFFE") >= 0 || lineUp.indexOf("COFFEE") >= 0
                  || lineUp.indexOf("ESPRESSO") >= 0 || lineUp.indexOf("BOIL") >= 0;
    bool isInstant = lineUp.indexOf("INSTANT") >= 0 || lineUp.indexOf("INSTAN") >= 0
                  || lineUp.indexOf("BRODO") >= 0;

    String boilerName;
    if (isCoffee) {
        tempCoffeeBoiler = (float)temp;
        boilerName = "Кафе бойлер";
    } else if (isInstant) {
        tempInstantBoiler = (float)temp;
        boilerName = "Инстант бойлер";
    } else {
        tempCoffeeBoiler = (float)temp;
        boilerName = "Бойлер";
    }

    // Only notify if temperature is suspicious (too high or too low)
    if (temp > 98) {
        String msg = "🌡️ *High Temperature!*\n";
        msg += boilerName + ": *" + String(temp) + "°C*\n";
        msg += "🕐 " + getTimeString();
        sendTelegram(msg);
    } else if (temp < 70) {
        String msg = "🌡️ *Low temperature!*\n";
        msg += boilerName + ": *" + String(temp) + "°C*\n";
        msg += "🕐 " + getTimeString();
        sendTelegram(msg);
    } else {
        // Normal — just log to serial, don't spam Telegram
        Serial.print(F("[TEMP] "));
        Serial.print(boilerName);
        Serial.print(F(": "));
        Serial.print(temp);
        Serial.println(F("°C (normal)"));
    }
}

// ════════════════════════════════════════════════════════════
//  LED PIN MONITORING
// ════════════════════════════════════════════════════════════
void checkLEDs() {
    bool led5v    = digitalRead(PIN_LED_5V);
    bool led24v   = digitalRead(PIN_LED_24V);
    bool ledCpu   = digitalRead(PIN_LED_CPU);
    bool ledMotor = digitalRead(PIN_LED_MOTOR);
    bool ledErr   = digitalRead(PIN_LED_ERR);
    unsigned long now = millis();

    // ── LED6: Motor overcurrent — HIGHEST PRIORITY ─────────
    if (ledErr && !prevLedErr) {
        if (now - lastErrorTime[0] > DEBOUNCE_ERROR_MS) {
            String msg = "🚨 *Alarm!*\n";
            msg += "━━━━━━━━━━━━━━━━━━━━\n";
            msg += "⚡ *High voltage on motor!*\n";
            msg += "The machine is blocking.\n";
            msg += "🔧 Need help!\n";
            msg += "🕐 " + getTimeString();
            sendTelegram(msg);
            lastErrorTime[0] = now;
        }
    }

    // ── LED1: 5V supply lost ────────────────────────────────
    if (!led5v && prevLed5v) {
        if (now - lastErrorTime[1] > DEBOUNCE_ERROR_MS) {
            String msg = "⚡ *Electrical problem!*\n";
            msg += "❌ No 5V\n";
            msg += "🕐 " + getTimeString();
            sendTelegram(msg);
            lastErrorTime[1] = now;
        }
    } else if (led5v && !prevLed5v) {
        sendTelegram("✅ 5V voltage is okey\n🕐 " + getTimeString());
    }

    // ── LED2: 24V supply lost ───────────────────────────────
    if (!led24v && prevLed24v) {
        if (now - lastErrorTime[2] > DEBOUNCE_ERROR_MS) {
            String msg = "⚡ *Problem!*\n";
            msg += "❌ No 24V\n";
            msg += "🕐 " + getTimeString();
            sendTelegram(msg);
            lastErrorTime[2] = now;
        }
    } else if (led24v && !prevLed24v) {
        sendTelegram("✅ 24V voltage is okey\n🕐 " + getTimeString());
    }

    if (ledCpu != cpuLastState) {
        cpuBlinkCount++;
        cpuLastState = ledCpu;
        cpuBlinkTime = now;
    }
    static bool cpuAlertSent = false;
    if (now - cpuBlinkTime > 10000 && cpuBlinkCount == 0 && !cpuAlertSent) {
        String msg = "⚠️ *CPU no blink!*\n";
        msg += "LED3 (CPU) not active >10s \n";
        msg += "Machine does not working CPU is blocked.\n";
        msg += "🕐 " + getTimeString();
        sendTelegram(msg);
        cpuAlertSent = true;
    }
    if (cpuBlinkCount > 0) {
        cpuAlertSent = false;
        cpuBlinkCount = 0;  // reset each 2-second window
    }

    // ── LED5: Motor state change ────────────────────────────
    // Only log — motor running briefly is normal (dispensing)
    if (ledMotor && !prevLedMotor) {
        Serial.println(F("[LED5] Motor started"));
    }

    // Update previous states
    prevLed5v    = led5v;
    prevLed24v   = led24v;
    prevLedCpu   = ledCpu;
    prevLedMotor = ledMotor;
    prevLedErr   = ledErr;
}

// ════════════════════════════════════════════════════════════
//  DAILY RESET & REPORT AT MIDNIGHT
// ════════════════════════════════════════════════════════════
void checkDailyReset() {
    struct tm ti;
    if (!getLocalTime(&ti)) return;

    int today = ti.tm_mday;
    int month = ti.tm_mon + 1;
    int hour  = ti.tm_hour;

    // Reset daily counter at midnight (00:00-00:02 window)
    if (hour == 0 && today != lastResetDay) {
        // Send yesterday's daily report first
        sendDailyReport();

        // Reset
        salesDay    = 0;
        lastResetDay = today;

        // Monthly reset on 1st of month
        if (today == 1 && month != lastResetMonth) {
            salesMonth    = 0;
            lastResetMonth = month;
        }
    }
}

void sendDailyReport() {
    struct tm ti;
    getLocalTime(&ti);
    char dateStr[20];
    strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &ti);

    String msg = "📊 *Daily Summary — " + String(dateStr) + "*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "☕ Sales today: *" + String(salesDay) + "* bevareges\n";
    msg += "📅 Mountly sales: *" + String(salesMonth) + "* bevareges\n";
    msg += "🔢 Total: *" + String(salesTotal) + "* bevareges\n";

    if (tempCoffeeBoiler > 0) {
        msg += "\n🌡️ Coffee temp: *" + String((int)tempCoffeeBoiler) + "°C*\n";
    }
    if (tempInstantBoiler > 0) {
        msg += "🌡️ Instant temp: *" + String((int)tempInstantBoiler) + "°C*\n";
    }

    msg += "\n⚡ 5V: " + String(digitalRead(PIN_LED_5V) ? "✅" : "❌");
    msg += "  24V: " + String(digitalRead(PIN_LED_24V) ? "✅" : "❌");
    msg += "  CPU: " + String(prevLedCpu ? "✅" : "❌");
    sendTelegram(msg);
}

// ════════════════════════════════════════════════════════════
//  PERIODIC STATUS REPORT (every 6 hours)
// ════════════════════════════════════════════════════════════
void sendStatusReport() {
    bool led5v  = digitalRead(PIN_LED_5V);
    bool led24v = digitalRead(PIN_LED_24V);
    bool ledCpu = digitalRead(PIN_LED_CPU);
    long rssi   = WiFi.RSSI();

    String msg = "📡 *Status report*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "🕐 " + getTimeString() + "\n\n";

    msg += "*Power:*\n";
    msg += "  🔌 5V : " + String(led5v  ? "✅ ОК" : "❌ error") + "\n";
    msg += "  ⚡ 24V: " + String(led24v ? "✅ ОК" : "❌ error") + "\n";
    msg += "  🖥️ CPU: " + String(ledCpu  ? "✅ Active" : "⚠️ Not active") + "\n\n";

    msg += "*Sales:*\n";
    msg += "  ☕ Today: *" + String(salesDay) + "*\n";
    msg += "  📅 Mounth: *" + String(salesMonth) + "*\n";
    msg += "  🔢 Total: *" + String(salesTotal) + "*\n\n";

    if (tempCoffeeBoiler > 0 || tempInstantBoiler > 0) {
        msg += "*Temp:*\n";
        if (tempCoffeeBoiler > 0)
            msg += "  🌡️ CoffeeBoiler: *" + String((int)tempCoffeeBoiler) + "°C*\n";
        if (tempInstantBoiler > 0)
            msg += "  🌡️ InstantBoiler: *" + String((int)tempInstantBoiler) + "°C*\n";
        msg += "\n";
    }

    msg += "*Network:*\n";
    msg += "  📶 WiFi: `" + WiFi.localIP().toString() + "`\n";
    msg += "  📡 Signal: " + String(rssi) + " dBm";
    if      (rssi > -60) msg += " (Perfect)";
    else if (rssi > -70) msg += " (Good)";
    else if (rssi > -80) msg += " (Bad)";
    else                  msg += " (No Connection)";

    sendTelegram(msg);
}

// ════════════════════════════════════════════════════════════
//  TELEGRAM SENDER
// ════════════════════════════════════════════════════════════
void sendTelegram(String message) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[TG] WiFi not connected — queuing skipped"));
        return;
    }

    Serial.print(F("[TG] Sending: "));
    Serial.println(message.substring(0, 60));

    HTTPClient http;
    String url = "https://api.telegram.org/bot" + BOT_TOKEN + "/sendMessage";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);

    // Build JSON payload
    DynamicJsonDocument doc(1024);
    doc["chat_id"]    = CHAT_ID;
    doc["text"]       = message;
    doc["parse_mode"] = "Markdown";

    String body;
    serializeJson(doc, body);

    int httpCode = http.POST(body);
    if (httpCode == 200) {
        Serial.println(F("[TG] Sent OK"));
    } else {
        Serial.print(F("[TG] Failed, HTTP code: "));
        Serial.println(httpCode);
    }
    http.end();
}

// ════════════════════════════════════════════════════════════
//  WIFI
// ════════════════════════════════════════════════════════════
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    digitalWrite(PIN_STATUS_LED, LOW);
    Serial.print(F("[WiFi] Connecting to "));
    Serial.print(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 40) {
        delay(500);
        Serial.print(".");
        tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print(F("[WiFi] Connected — IP: "));
        Serial.println(WiFi.localIP());
        digitalWrite(PIN_STATUS_LED, HIGH);
    } else {
        Serial.println(F("\n[WiFi] FAILED — will retry in 30s"));
    }
}

// ════════════════════════════════════════════════════════════
//  UTILITY HELPERS
// ════════════════════════════════════════════════════════════

// Check if 'haystack' contains any keyword from a null-terminated array
bool containsAny(const String& haystack, const char* keywords[]) {
    for (int i = 0; keywords[i] != nullptr; i++) {
        if (haystack.indexOf(keywords[i]) >= 0) return true;
    }
    return false;
}

// Extract first integer number found in a string
int extractNumber(const String& s) {
    String num = "";
    for (int i = 0; i < (int)s.length(); i++) {
        char c = s.charAt(i);
        if (isDigit(c)) {
            num += c;
        } else if (num.length() > 0) {
            // Stop at first non-digit after finding digits
            break;
        }
    }
    if (num.length() == 0) return -1;
    return num.toInt();
}

// Return formatted time string: "14:35  10.04.2026"
String getTimeString() {
    struct tm ti;
    if (!getLocalTime(&ti)) return "??:?? ??.??.????";
    char buf[24];
    strftime(buf, sizeof(buf), "%H:%M  %d.%m.%Y", &ti);
    return String(buf);
}

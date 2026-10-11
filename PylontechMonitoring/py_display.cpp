#include <Arduino.h>
#include <Preferences.h>
#include "py_display.h"
#include "config.h"
#include "py_cellstats.h"
#include "py_cellstore.h"
#include "py_log.h"
#include "py_scheduler.h"
#include "py_mqtt.h"
#include "py_sdcard.h"
#include "py_systemmanager.h"
#include "py_uart.h"
#include "py_parser_pwr.h"
#include "py_wifimanager.h"

extern HealthStatus health;
extern AppConfig config;
extern PyMqtt py_mqtt;
extern PyUart py_uart;

TFT_eSPI tft = TFT_eSPI();
PyDisplay display;

// ---- Backlight PWM (TFT_BL pin) ----
// LEDC API differs between Arduino ESP32 core 2.x (channels) and 3.x (pins).
// Polarity on the CYD boards varies by revision (some are active-LOW despite
// TFT_BACKLIGHT_ON=HIGH). Set BACKLIGHT_INVERT after the on-device calibration.
static const bool   BACKLIGHT_INVERT = false;   // true => higher duty = less light
static const uint8_t BL_CH = 0;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void backlightInit() {
    if (!ledcAttach(TFT_BL, 5000, 8))
        Serial.println("[BACKLIGHT] WARNING: ledcAttach failed; backlight stays at TFT_eSPI level");
    ledcWrite(TFT_BL, 200);
}
static void backlightWrite(uint8_t value) {
    ledcWrite(TFT_BL, value);
}
#else
static void backlightInit() {
    ledcSetup(BL_CH, 5000, 8);
    ledcAttachPin(TFT_BL, BL_CH);
    ledcWrite(BL_CH, 200);
}
static void backlightWrite(uint8_t value) {
    ledcWrite(BL_CH, value);
}
#endif

// Map a 0-100% brightness to a PWM duty for the "screen on" state. A floor of 1
// keeps the screen perceptible when on (0% still glows) and honours the
// "always ON at boot" rule even if the saved brightness is 0.
static uint8_t dutyForBrightness(uint8_t pct) {
    uint8_t d = (uint16_t)pct * 255 / 100;
    if (d < 1) d = 1;
    return BACKLIGHT_INVERT ? (255 - d) : d;
}

// ---- Layout constants (320x240 landscape) ----
static const int16_t L_X = 5,  L_Y = 5,  L_W = 125, L_H = 200;
static const int16_t R_X = 135;
static const int16_t BATT_CX = 67;
static const int16_t BADGE_W = 56, BADGE_H = 24;
static const int16_t BADGE_Y = 183;  // SOC badge below the battery icon (body 36..162, panel to 205)
static const int16_t DISCONNECT_AFTER_MS = 10000;

// Right panel layout (label + value on same line)
static const int16_t STATUS_Y = 18;  // centered status value, no label
static const int16_t VAL_X = 310;  // right-aligned value text edge
static const int MAIN_ROW_COUNT = 4;   // always-drawn rows (EVENTS is conditional)
static const int ROW_EVENTS = 4;       // index of the conditional Events row
static const int16_t ROW_Y[5] = { 58, 88, 118, 148, 178 };
static const char *const ROW_LABELS_EN[5] = { "VOLTAGE", "CURRENT", "TEMP", "POWER", "EVENTS" };
static const char *const ROW_LABELS_ES[5] = { "VOLTAJE", "CORR.", "TEMP", "POTEN.", "EVENTOS" };

// ---- Main screen footer: STATUS (left) and MODULES (right) buttons.
// The clock/WiFi footer moved to the STATUS screen (see drawStatusHeader).
static const int16_t BTN_STATUS_X = 4, BTN_MOD_X = 204, BTN_MOD_Y = 208, BTN_MOD_W = 112, BTN_MOD_H = 30;

// ---- Modules screen geometry ----
static const int16_t HDR_BACK_X = 4, HDR_BACK_Y = 3, HDR_BACK_W = 68, HDR_BACK_H = 22;
static const int16_t MOD_FRAME_X = 2, MOD_FRAME_Y = 30, MOD_FRAME_W = 316, MOD_FRAME_H = 178;
static const int16_t MOD_ROW_X = 8;
static const int16_t MOD_ROW_W = 304;
static const int16_t MOD_ROW_H = 33;
static const int16_t MOD_ROW_GAP = 1;
static const int16_t MOD_VISIBLE = 5;
static const int MOD_EMPTY_ROWS = 10;   // placeholder rows when no modules
static const int16_t SCROLL_Y = 210, SCROLL_H = 28;
static const int16_t SCROLL_UP_X = 4, SCROLL_UP_W = 100;
static const int16_t SCROLL_DN_X = 216, SCROLL_DN_W = 100;

// ---- Cell detail screen geometry ----
static const int16_t CL_GRID_Y = 32;        // top of the cell grid
static const int16_t CL_COL1_X = 8;         // left column (cells 1..8)
static const int16_t CL_COL2_X = 162;       // right column (cells 9..16)
static const int16_t CL_ROW_H = 18;         // row height in grid (size-2 font)
static const int16_t CL_ROWS_PER_COL = 8;   // cells per column
static const int16_t CL_SEP_Y = 178;        // line under the grid
// Summary: three rows x two columns. The left column holds the current-frame
// value, the right column the matching today's extreme (same row of each pair).
static const int16_t CL_SUM_X1 = 8;         // left summary column (current)
static const int16_t CL_SUM_X2 = 140;       // right summary column (today)
static const int16_t CL_SUM_Y1 = 187;       // Max V | Max V today
static const int16_t CL_SUM_Y2 = 203;       // Min V | Min V today
static const int16_t CL_SUM_Y3 = 219;       // Current Delta | Max Delta today
static const int CL_EMPTY_CELLS = 15;       // placeholder cells when no data

// ---- Module status colors ----
static const uint16_t COLOR_OK   = 0x05E0;  // green
static const uint16_t COLOR_WARN = 0xFDE0;  // yellow
static const uint16_t COLOR_ERR  = 0xF800;  // red

// ---- Placeholder (no modules) colors ----
static const uint16_t COLOR_PH_BG  = 0x8410;  // grey box
static const uint16_t COLOR_PH_TAG = 0x39E7;  // darker grey tag
static const uint16_t COLOR_PH_TXT = 0x4208;  // dark grey text

// ---- Theme palette (dark = default). Light mode inverts the screen
// background (black <-> white) and the primary text colour; accent colors
// (green/yellow/red/blue/amber/slate/navy) stay identical in both themes.
static bool g_darkMode = true;
static uint16_t themeBg() { return g_darkMode ? TFT_BLACK : TFT_WHITE; }
static uint16_t themeFg() { return g_darkMode ? TFT_WHITE : TFT_BLACK; }
static void applyTheme(bool dark) { g_darkMode = dark; }

// ---- Touch calibration fallback (XPT2046, CYD ESP32-2432S028R, rotation 1) ----
// Only used if the guided calibration cannot read useful raw ranges.
static const uint16_t TOUCH_X0 = 264;      // raw Y min over the 320px axis
static const uint16_t TOUCH_X1 = 3268;     // raw Y range over the 320px axis
static const uint16_t TOUCH_Y0 = 275;      // raw X min over the 240px axis
static const uint16_t TOUCH_Y1 = 3345;     // raw X range over the 240px axis
static const uint16_t TOUCH_FLAGS = 5;     // bit0=rotate, bit1=invert_x, bit2=invert_y

// Battery icon colors (web dashboard palette, 16-bit mapped)
static const uint16_t BAT_OUTLINE = 0x7C93;  // slate grey (web #78909c)
static const uint16_t BAT_AMBER   = 0xF460;  // flash bolt (web #ff8f00)

static uint16_t moduleColor(uint8_t status) {
    if (status == 1) return COLOR_WARN;
    if (status == 2) return COLOR_ERR;
    return COLOR_OK;
}

// Decode a UTF-8 literal and emit raw CP437 bytes for the TFT_eSPI GLCD 5x7
// font. Only the Spanish glyphs actually present in that font are mapped;
// everything else non-ASCII becomes '?'. ASCII passes through untouched.
// CP437 positions: é=0x82, ü=0x81, á=0xA0, í=0xA1, ó=0xA2, ú=0xA3, ñ=0xA4,
// Ñ=0xA5, ª=0xA6, º=0xA7, ¿=0xA8, ¡=0xAD. Uppercase accented vowels
// are NOT in the font, so they are never emitted.
static String cp437(const char *utf8) {
    String out;
    if (!utf8) return out;
    const uint8_t *p = (const uint8_t *)utf8;
    while (*p) {
        uint8_t c = *p;
        if (c < 0x80) {                 // plain ASCII (0x00-0x7F) passes through
            out += (char)c;
            p++;
            continue;
        }
        // Decode one UTF-8 code point (2- and 3-byte forms cover the glyphs).
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            p++;                        // invalid lead byte
            out += '?';
            continue;
        }
        char b;
        switch (cp) {
            case 0x00E9: b = (char)0x82; break;   // é
            case 0x00FC: b = (char)0x81; break;   // ü
            case 0x00E1: b = (char)0xA0; break;   // á
            case 0x00ED: b = (char)0xA1; break;   // í
            case 0x00F3: b = (char)0xA2; break;   // ó
            case 0x00FA: b = (char)0xA3; break;   // ú
            case 0x00F1: b = (char)0xA4; break;   // ñ
            case 0x00D1: b = (char)0xA5; break;   // Ñ
            case 0x00AA: b = (char)0xA6; break;   // ª
            case 0x00BA: b = (char)0xA7; break;   // º
            case 0x00BF: b = (char)0xA8; break;   // ¿
            case 0x00A1: b = (char)0xAD; break;   // ¡
            default:     b = '?';        break;
        }
        out += b;
    }
    return out;
}

String PyDisplay::tr(const char *en, const char *es) const {
    return cp437(spanish ? es : en);
}

// Battery stack status mapping shown on the main screen. The raw protocol
// token is never changed here — drawValues() tests the raw value for the red
// alarm colour so it works in both languages. The tokens produced by
// computeStackStatus() are "Alarm!", "Charging", "Discharging", "Idle",
// "Balance" and "Disconnected"; "Charge"/"Discharge" are kept as defensive
// aliases in case the protocol wording changes.
static String displayStatus(const String &in, bool es) {
    if (!es) {
        if (in == "Alarm!")  return "Alert!";
        if (in == "Balance") return "Balancing";
        return in;   // Charging / Discharging / Idle / Disconnected
    }
    if (in == "Alarm!")       return "Alerta!";
    if (in == "Balance")      return "Equilibrando";
    if (in == "Charging")     return "Cargando";
    if (in == "Charge")       return "Cargando";      // defensive alias
    if (in == "Discharging")  return "Descargando";
    if (in == "Discharge")    return "Descargando";   // defensive alias
    if (in == "Idle")         return "Reposo";
    if (in == "Disconnected") return "Desconectado";
    return in;
}

bool PyDisplay::batteryConnected(unsigned long now) {
    if (!lastHasData) return false;
    // PWR polls every config.battery.intervalPwr (default 60 s), which is much
    // longer than DISCONNECT_AFTER_MS. The main screen is normally only repainted
    // right after a fresh frame, but returning from the MODULES screen (and the
    // theme change in applyScreenConfig) force an immediate redraw at an
    // arbitrary point in the poll cycle. A fixed 10 s cutoff then blanked the
    // cached values ("NO DATA" / "--") even though the link was fine. Treat the
    // link as down only after two full polling intervals without a frame, with a
    // floor of DISCONNECT_AFTER_MS for very short intervals.
    unsigned long timeout = config.battery.intervalPwr * 2;
    if (timeout < DISCONNECT_AFTER_MS) timeout = DISCONNECT_AFTER_MS;
    return (now - lastDataTime) < timeout;
}

bool PyDisplay::inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) const {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void PyDisplay::begin() {
    Serial.println("========================================");
    Serial.println("Initializing display...");
    Serial.println("Pines: MOSI=13, SCLK=14, CS=15, DC=2, RST=-1, BL=21");
    Serial.println("========================================");
    delay(1000);

    // Theme from config (loaded earlier in setup); avoids a dark->light flash.
    applyTheme(config.screen.darkMode);

    // UI language from config (loaded earlier in setup); avoids an English flash.
    spanish = (config.language == "es");

    tft.init();
    Serial.println("Display initialized");
    delay(500);

    tft.setRotation(1); // 320x240 landscape
    tft.setTextWrap(false, false);
    tft.fillScreen(themeBg());
    Serial.println("Display cleared");
    delay(500);

    // Backlight PWM (pin 21, active-high). tft.init() drives the pin HIGH with
    // digitalWrite; re-arming it as a LEDC channel lets us dim the backlight.
    backlightInit();

    initTouch();
    // Ignore touch for the first 1.5s after boot: the XPT2046 bus emits
    // transient IRQ edges during init that would otherwise register phantom
    // taps (raw=(4095,0)) before the IRQ settles high.
    touchReadyAt = millis() + 1500;

    drawStaticUI();
    Serial.println("Display UI drawn");
    delay(500);
    Serial.println("========================================");
}

// ---- Bit-banged XPT2046 touch driver ----
// On this board (ESP32-2432S028R) the XPT2046 has its OWN SPI bus, independent
// of the display: CLK=25, MOSI=32, MISO=39, CS=33, and an IRQ line on GPIO36
// that goes LOW while the panel is pressed (per the manufacturer's demos in
// Demo_LVGL/5_2_XPT2046_Touch_Test and 5_35 LVGL_Full_Test-S024).
static const uint8_t XPT_CLK = 25, XPT_MOSI = 32, XPT_MISO = 39, XPT_CS = 33, XPT_IRQ = 36;
static const uint8_t TOUCH_CFG_VER = 3;   // NVS key to invalidate older touch state

static void xptDirectGpio() {
    pinMode(XPT_CLK, OUTPUT);
    pinMode(XPT_MOSI, OUTPUT);
    pinMode(XPT_MISO, INPUT);            // GPIO39 input-only, no pullup (manufacturer)
    pinMode(XPT_CS, OUTPUT);
    digitalWrite(XPT_CLK, LOW);
    digitalWrite(XPT_MOSI, LOW);
    digitalWrite(XPT_CS, HIGH);
}

static void xptWriteBit(uint8_t b) {
    digitalWrite(XPT_CLK, LOW);
    digitalWrite(XPT_MOSI, b ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(XPT_CLK, HIGH);
    delayMicroseconds(1);
}

static void xptSend8(uint8_t d) {
    for (int i = 7; i >= 0; i--) xptWriteBit((d >> i) & 1);
}

// Manufacturer-style read: 8-bit command, 6us conversion settle, clear BUSY,
// then 16 bits read (only the high 12 are valid).
static uint16_t xptReadReg(uint8_t reg) {
    xptDirectGpio();
    digitalWrite(XPT_CLK, LOW);
    digitalWrite(XPT_MOSI, LOW);
    digitalWrite(XPT_CS, LOW);
    xptSend8(reg);
    ets_delay_us(6);
    digitalWrite(XPT_CLK, LOW);
    ets_delay_us(1);
    digitalWrite(XPT_CLK, HIGH);         // clear BUSY
    digitalWrite(XPT_CLK, LOW);
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) {
        digitalWrite(XPT_CLK, LOW);
        digitalWrite(XPT_CLK, HIGH);
        v <<= 1;
        if (digitalRead(XPT_MISO)) v |= 1;
    }
    digitalWrite(XPT_CS, HIGH);
    return v >> 4;
}

static bool xptPressed() {
    return digitalRead(XPT_IRQ) == LOW;  // IRQ line active-low on press
}

// Manufacturer-style stable read (mirrors TP_Read_XOY): take N raw samples,
// discard the lowest and highest outliers, and average the rest so a single
// noisy sample can't knock the coordinate off-target.
static uint16_t xptReadMedian(uint8_t reg) {
    const int N = 16;          // number of raw samples
    const int DROP = 2;        // extreme samples discarded on each end
    uint16_t buf[N];
    for (int i = 0; i < N; i++) buf[i] = xptReadReg(reg);

    // Bubble sort ascending (same as the reference driver)
    for (int i = 0; i < N - 1; i++) {
        for (int j = i + 1; j < N; j++) {
            if (buf[i] > buf[j]) {
                uint16_t t = buf[i];
                buf[i] = buf[j];
                buf[j] = t;
            }
        }
    }

    uint32_t sum = 0;
    for (int i = DROP; i < N - DROP; i++) sum += buf[i];
    return (uint16_t)(sum / (N - 2 * DROP));
}

bool PyDisplay::readTouchMedian(uint16_t &rx, uint16_t &ry) {
    rx = xptReadMedian(0x90);   // manufacturer CMD_X_READ
    ry = xptReadMedian(0xD0);   // manufacturer CMD_Y_READ
    return true;
}

void PyDisplay::convertTouch(uint16_t &cx, uint16_t &cy) {
    int32_t x = cx, y = cy;
    int32_t x0 = touchCal[0], x1 = touchCal[1], y0 = touchCal[2], y1 = touchCal[3];
    uint8_t flg = touchCal[4];
    int32_t dx, dy;
    if (!(flg & 0x01)) {
        dx = x - x0; dy = y - y0;
    } else {
        dx = y - x0; dy = x - y0;
    }
    cx = (uint16_t)constrain((int)(dx * tft.width() / (x1 ? x1 : 1)), 0, (int)tft.width() - 1);
    cy = (uint16_t)constrain((int)(dy * tft.height() / (y1 ? y1 : 1)), 0, (int)tft.height() - 1);
    if (flg & 0x02) cx = tft.width() - 1 - cx;
    if (flg & 0x04) cy = tft.height() - 1 - cy;
}

bool PyDisplay::loadCalibration(uint16_t cal[5]) const {
    Preferences prefs;
    if (!prefs.begin("touch", true)) return false;
    bool ok = prefs.getUChar("ok", 0) == 1;
    if (ok) {
        cal[0] = prefs.getUShort("tx0", 0);
        cal[1] = prefs.getUShort("tx1", 0);
        cal[2] = prefs.getUShort("ty0", 0);
        cal[3] = prefs.getUShort("ty1", 0);
        cal[4] = prefs.getUChar("flg", 0);
        ok = (cal[1] > 1 && cal[3] > 1);
    }
    prefs.end();
    return ok;
}

void PyDisplay::saveCalibration(const uint16_t cal[5]) {
    Preferences prefs;
    if (!prefs.begin("touch", false)) return;
    prefs.putUChar("ok", 1);
    prefs.putUShort("tx0", cal[0]);
    prefs.putUShort("tx1", cal[1]);
    prefs.putUShort("ty0", cal[2]);
    prefs.putUShort("ty1", cal[3]);
    prefs.putUChar("flg", cal[4]);
    prefs.end();
    Serial.println("Touch calibration saved to NVS");
}

void PyDisplay::initTouch() {
    // GPIO36 (SENSOR_VP) is an input-only RTC pin on the ESP32: digitalRead()
    // returns a spurious LOW until the pin is explicitly configured as digital
    // input. This MUST run on every boot — including when a calibration is
    // already stored (the early return below used to skip it, latching the
    // touch as permanently pressed and killing navigation).
    pinMode(XPT_IRQ, INPUT);

    uint16_t cal[5];

    // NVS migration: invalidate stale state from earlier builds that read the
    // touch controller over the shared display bus instead of the real XPT2046
    // bus (CLK=25/MOSI=32/MISO=39/CS=33/IRQ=36).
    {
        Preferences prefs;
        if (prefs.begin("touch", false)) {
            if (prefs.getUChar("ver", 0) != TOUCH_CFG_VER) {
                prefs.putUChar("ver", TOUCH_CFG_VER);
                prefs.remove("ok");
                prefs.remove("tpin");
                Serial.println("[TOUCH] NVS touch config reset (new wiring)");
            }
            prefs.end();
        }
    }

    if (loadCalibration(cal)) {
        for (int i = 0; i < 5; i++) touchCal[i] = cal[i];
        Serial.println("Touch calibrated (NVS)");
        return;
    }

    // Initialize the fixed XPT2046 bus pins (GPIO36 handled at function top)
    Serial.printf("[TOUCH] XPT2046 bus: CS=%u CLK=%u MOSI=%u MISO=%u IRQ=%u\n",
                  XPT_CS, XPT_CLK, XPT_MOSI, XPT_MISO, XPT_IRQ);

    // Timed 4-corner calibration (never blocks boot forever). The wizard first
    // waits for the user to press the screen so a boot-time blinking run never
    // fails because nobody was tapping yet.
    Serial.println("Touch: starting guided calibration...");
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tr("TOUCH CALIBRATION", "Calibración táctil"), 160, 40);
    tft.setTextSize(1);
    tft.drawString(tr("Press the screen to begin", "Toque la pantalla para empezar"), 160, 62);
    tft.setTextDatum(TL_DATUM);

    const unsigned long BEGIN_WAIT = 10000;
    bool began = false;
    unsigned long beginStart = millis();
    while (millis() - beginStart < BEGIN_WAIT) {
        if (xptPressed()) { began = true; break; }
        delay(20);
    }
    if (!began) {
        for (int i = 0; i < 5; i++) touchCal[i] = (uint16_t[]){TOUCH_X0, TOUCH_X1, TOUCH_Y0, TOUCH_Y1, TOUCH_FLAGS}[i];
        Serial.println("Touch: no press to begin within timeout, using defaults (calibration will be offered on next boot)");
        tft.fillScreen(TFT_BLACK);
        Serial.println("Touch ready");
        return;
    }
    // Let the user release before the corner sequence starts
    unsigned long relEnd = millis() + 2000;
    while (xptPressed() && millis() < relEnd) delay(20);
    delay(300);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tr("Press and hold each marker", "Mantenga pulsado cada punto"), 160, 62);
    tft.setTextDatum(TL_DATUM);
    delay(400);

    const int16_t s = 14;
    const int16_t w = tft.width(), h = tft.height();
    int16_t values[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool cornerFail = false;
    const unsigned long CORNER_WAIT = 4000;
    const int MAX_SAMPLES = 8;

    // Press detection: IRQ active-low (primary), Z pressure as fallback
    auto rawValid = [&](uint16_t *rx, uint16_t *ry) -> bool {
        if (!xptPressed()) return false;
        delay(5);
        if (!xptPressed()) return false;
        return readTouchMedian(*rx, *ry);
    };

    for (int i = 0; i < 4; i++) {
        tft.fillRect(0, 0, s + 1, s + 1, TFT_BLUE);
        tft.fillRect(0, h - s - 1, s + 1, s + 1, TFT_BLUE);
        tft.fillRect(w - s - 1, 0, s + 1, s + 1, TFT_BLUE);
        tft.fillRect(w - s - 1, h - s - 1, s + 1, s + 1, TFT_BLUE);

        if (i == 0) { // up left
            tft.drawLine(0, 0, 0, s, TFT_WHITE);
            tft.drawLine(0, 0, s, 0, TFT_WHITE);
            tft.drawLine(0, 0, s, s, TFT_WHITE);
        } else if (i == 1) { // bot left
            tft.drawLine(0, h - s - 1, 0, h - 1, TFT_WHITE);
            tft.drawLine(0, h - 1, s, h - 1, TFT_WHITE);
            tft.drawLine(s, h - s - 1, 0, h - 1, TFT_WHITE);
        } else if (i == 2) { // up right
            tft.drawLine(w - s - 1, 0, w - 1, 0, TFT_WHITE);
            tft.drawLine(w - s - 1, s, w - 1, 0, TFT_WHITE);
            tft.drawLine(w - 1, s, w - 1, 0, TFT_WHITE);
        } else { // bot right
            tft.drawLine(w - s - 1, h - s - 1, w - 1, h - 1, TFT_WHITE);
            tft.drawLine(w - 1, h - 1 - s, w - 1, h - 1, TFT_WHITE);
            tft.drawLine(w - 1 - s, h - 1, w - 1, h - 1, TFT_WHITE);
        }

        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(tr("Dot ", "Punto ") + String(i + 1) + "/4", 160, 200);
        tft.setTextDatum(TL_DATUM);

        if (i > 0) delay(400);

        unsigned long start = millis();
        uint32_t sx = 0, sy = 0;
        int n = 0;
        while (millis() - start < CORNER_WAIT && n < MAX_SAMPLES) {
            uint16_t rx = 0, ry = 0;
            if (rawValid(&rx, &ry)) {
                sx += rx; sy += ry; n++;
            }
            delay(5);
        }
        if (n == 0) {
            cornerFail = true;
            Serial.printf("[TOUCH] Corner %d: NO PRESS DETECTED (IRQ never went low)\n", i + 1);
            break;
        }
        values[i * 2] = sx / n;
        values[i * 2 + 1] = sy / n;
        Serial.printf("[TOUCH] Corner %d raw=(%d,%d) samples=%d\n", i + 1, values[i * 2], values[i * 2 + 1], n);
    }

    if (cornerFail) {
        cal[0] = TOUCH_X0; cal[1] = TOUCH_X1; cal[2] = TOUCH_Y0; cal[3] = TOUCH_Y1; cal[4] = TOUCH_FLAGS;
        for (int i = 0; i < 5; i++) touchCal[i] = cal[i];
        Serial.println("Touch calibration incomplete, using defaults (offered again on next boot)");
        tft.fillScreen(TFT_BLACK);
        Serial.println("Touch ready");
        return;
    }

    // Replicate the TFT_eSPI calibrateTouch math from the sampled corners
    bool rotate = (abs(values[0] - values[2]) > abs(values[1] - values[3]));
    if (rotate) {
        cal[0] = (uint16_t)((values[1] + values[3]) / 2);
        cal[1] = (uint16_t)((values[5] + values[7]) / 2);
        cal[2] = (uint16_t)((values[0] + values[4]) / 2);
        cal[3] = (uint16_t)((values[2] + values[6]) / 2);
    } else {
        cal[0] = (uint16_t)((values[0] + values[2]) / 2);
        cal[1] = (uint16_t)((values[4] + values[6]) / 2);
        cal[2] = (uint16_t)((values[1] + values[5]) / 2);
        cal[3] = (uint16_t)((values[3] + values[7]) / 2);
    }
    bool invX = false, invY = false;
    if (cal[0] > cal[1]) { uint16_t t = cal[0]; cal[0] = cal[1]; cal[1] = t; invX = true; }
    if (cal[2] > cal[3]) { uint16_t t = cal[2]; cal[2] = cal[3]; cal[3] = t; invY = true; }
    cal[1] -= cal[0];
    cal[3] -= cal[2];
    if (cal[0] == 0) cal[0] = 1;
    if (cal[1] == 0) cal[1] = 1;
    if (cal[2] == 0) cal[2] = 1;
    if (cal[3] == 0) cal[3] = 1;
    cal[4] = (uint8_t)rotate | ((uint8_t)invX << 1) | ((uint8_t)invY << 2);

    for (int i = 0; i < 5; i++) touchCal[i] = cal[i];
    saveCalibration(cal);
    tft.fillScreen(TFT_BLACK);
    Serial.printf("[TOUCH] Calibration ok: cal={%u,%u,%u,%u,%u}\n", cal[0], cal[1], cal[2], cal[3], cal[4]);
    Serial.println("Touch ready");
}

// Single source of truth for the backlight level. screenOn decides dark vs the
// brightness-derived duty; polarity is handled inside dutyForBrightness().
void PyDisplay::writeBacklight() {
    if (screenOn) {
        backlightWrite(dutyForBrightness(brightnessPct));
    } else {
        backlightWrite(BACKLIGHT_INVERT ? 255 : 0);
    }
}

void PyDisplay::setBrightness(uint8_t percent) {
    brightnessPct = percent;
    if (screenOn) writeBacklight();
}

// YYYYMMDD day key of the local calendar day, matching the DST-aware clock
// string. Returns -1 while the system time is not valid (no sync → no day
// tracking, stats are never wiped against the 1970 epoch).
static int currentDayKey() {
    return config.getCurrentDayKey();
}

// Delegates to the shared clock helper so the screen power schedule uses
// exactly the same time-of-day as the scheduled reboot. Deriving it here as
// well led to a second, inconsistent DST calculation.
static bool currentHourMinute(int &h, int &m) {
    int mins = config.getCurrentMinutesOfDay();
    if (mins < 0) return false;
    h = mins / 60;
    m = mins % 60;
    return true;
}

void PyDisplay::setScreenOn(bool on) {
    screenOn = on;
    // Any turn-on restarts the auto-off inactivity window so a freshly woken
    // (or just-scheduled) screen does not time out immediately.
    if (on) lastTouchTime = millis();
    writeBacklight();
}

void PyDisplay::setTouchEnabled(bool enabled) {
    touchEnabled = enabled;
}

void PyDisplay::applyScreenConfig(const ScreenConfig &cfg) {
    screenEnabled = cfg.enabled;
    touchEnabled = cfg.touch;
    brightnessPct = cfg.brightness;
    scheduled = cfg.scheduled;
    offHour   = cfg.offHour;
    offMin    = cfg.offMin;
    onHour    = cfg.onHour;
    onMin     = cfg.onMin;
    refreshMs = (uint32_t)cfg.refreshSec * 1000;
    lastMinuteKey = -1;
    autoOff    = cfg.autoOff;
    autoOffSec = cfg.autoOffSec;

    // Theme switch: repaint the visible screen immediately with new colors so
    // no stale black/white region survives the change.
    bool themeChanged = (cfg.darkMode != g_darkMode);
    applyTheme(cfg.darkMode);
    if (themeChanged) {
        lastTimeStr = "";
        if (screen == DisplayScreen::MAIN) {
            drawStaticUI();
            drawBatteryPanel();
            drawValues();
        } else if (screen == DisplayScreen::MODULES) {
            drawModulesHeader();
            drawModuleList();
        } else if (screen == DisplayScreen::CELLS) {
            drawCellScreen();
        } else if (screen == DisplayScreen::STATUS) {
            drawStatusHeader();
            drawStatusRows();
        }
    }

    // screenOn is NOT set from config here — enabling/disabling is driven by
    // screenEnabled (drawn/touch master switch). A plain save must neither wake
    // a screen a power-timer turned off nor darken a live one.
    if (screenOn) writeBacklight();
}

bool PyDisplay::isScreenOn() const {
    return screenOn;
}

void PyDisplay::setLanguage(const String &lang) {
    bool es = (lang == "es");
    if (es == spanish) return;   // no change -> no repaint
    spanish = es;

    // Full repaint of the currently visible screen, following the same pattern
    // as the theme change in applyScreenConfig(). Backlight/touch are untouched.
    lastTimeStr = "";
    if (screen == DisplayScreen::MAIN) {
        drawStaticUI();
        drawBatteryPanel();
        drawValues();
    } else if (screen == DisplayScreen::MODULES) {
        drawModulesHeader();
        drawModuleList();
    } else if (screen == DisplayScreen::CELLS) {
        drawCellScreen();
    } else if (screen == DisplayScreen::STATUS) {
        drawStatusHeader();
        drawStatusRows();
    }
}

void PyDisplay::setEventCount(int n) {
    if (n == eventCount) return;
    eventCount = n;
    // The Events row is conditional, so redraw the right panel on the next
    // loop() pass (throttled by refreshMs like any other data update).
    needsRedraw = true;
}

void PyDisplay::drawStaticUI() {
    const char *const *ROW_LABELS = spanish ? ROW_LABELS_ES : ROW_LABELS_EN;

    tft.fillScreen(themeBg());

    // Left battery panel border + title
    tft.drawRect(L_X, L_Y, L_W, L_H, TFT_DARKGREY);
    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("PYLONTECH", BATT_CX, 12);
    tft.setTextDatum(TL_DATUM);

    // Right panel labels (size 2, values drawn on same line in drawValues)
    tft.setTextSize(2);
    for (int i = 0; i < MAIN_ROW_COUNT; i++) {
        tft.setCursor(R_X + 5, ROW_Y[i]);
        tft.print(ROW_LABELS[i]);
    }
    // Events: drawn only when the history has entries; label in red.
    if (eventCount > 0) {
        tft.setTextColor(TFT_RED, themeBg());
        tft.setCursor(R_X + 5, ROW_Y[ROW_EVENTS]);
        tft.print(ROW_LABELS[ROW_EVENTS]);
        tft.setTextColor(TFT_DARKGREY, themeBg());
    }
    tft.setTextSize(1);

    // Bottom separator
    tft.drawLine(5, 205, 315, 205, TFT_DARKGREY);

    drawMainButton();
    
    
}

void PyDisplay::drawMainButton() {
    // Same style for both footer buttons: NAVY fill, SKYBLUE border, white label.
    auto button = [](int16_t x, const char *label) {
        tft.fillRoundRect(x, BTN_MOD_Y, BTN_MOD_W, BTN_MOD_H, 8, TFT_NAVY);
        tft.drawRoundRect(x, BTN_MOD_Y, BTN_MOD_W, BTN_MOD_H, 8, TFT_SKYBLUE);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, TFT_NAVY);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(label, x + BTN_MOD_W / 2, BTN_MOD_Y + BTN_MOD_H / 2);
        tft.setTextDatum(TL_DATUM);
    };

    button(BTN_STATUS_X, tr("STATUS", "ESTADO").c_str());
    button(BTN_MOD_X, tr("MODULES", "MODULOS").c_str());
}

// Battery icon drawn with TFT primitives (same look as the web dashboard):
// level-scaled fill, slate outline, amber bolt while charging, "!" when
// disconnected/alarm. No smooth MDI font (its charging glyph overflowed the panel).
static void drawBatteryIcon(int soc, bool conn, const String &stackStatus) {
    const int16_t bx = 28, bw = 78, r = 12;      // body box (x 28..106)
    const int16_t top = 36, bot = 162;          // body y 36..162
    const int16_t h  = bot - top;

    bool alarm = (stackStatus == "Alarm!");

    if (!conn) {
        uint16_t c = alarm ? COLOR_ERR : TFT_DARKGREY;
        tft.drawRoundRect(bx, top, bw, h, r, c);
        tft.fillRoundRect(bx + (bw - 36) / 2, 24, 36, 12, 3, c);
        tft.setTextSize(2);
        tft.setTextColor(c, themeBg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("!", BATT_CX, (top + bot) / 2);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        return;
    }

    // Alarm: whole battery in red with "!" (precedence over level fill and bolt)
    if (alarm) {
        uint16_t c = COLOR_ERR;
        tft.drawRoundRect(bx, top, bw, h, r, c);
        tft.fillRoundRect(bx + (bw - 36) / 2, 24, 36, 12, 3, c);
        tft.setTextSize(2);
        tft.setTextColor(c, themeBg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("!", BATT_CX, (top + bot) / 2);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        return;
    }

    // Level fill (green, red below 10%)
    uint16_t fillCol = (soc < 10) ? COLOR_ERR : COLOR_OK;
    int32_t fillH = (int32_t)h * soc / 100;
    int16_t fillY = bot - fillH;
    if (fillH > 0)
        tft.fillRoundRect(bx, fillY, bw, fillH, r, fillCol);

    // Slate outline + terminal (drawn over the fill to trim the rounded caps)
    tft.drawRoundRect(bx, top, bw, h, r, BAT_OUTLINE);
    tft.fillRoundRect(bx + (bw - 36) / 2, 24, 36, 12, 3, BAT_OUTLINE);

    // Charging: amber bolt inside the body (scaled MDI_BATT_FLASH, correct path
    // M7,2 V13 H10 V22 L17,10 H13 L17,2 H7 Z -> polygon:
    // A(48,50) B(48,96) C(59,96) D(59,134) E(86,84) F(71,84) G(86,50)),
    // filled per scanline so the top-right keeps its slope and the bottom a true point.
    if (stackStatus == "Charging") {
        for (int y = 50; y <= 134; y++) {
            int xl = (y < 96) ? 48 : 59;
            float xrF = (y < 84) ? (86.0f - (y - 49.5f) * (15.0f / 34.0f))   // top-right slope G-F
                                 : (86.0f - (y - 83.5f) * (27.0f / 50.0f));   // main diagonal D-E
            int xr = (int)(xrF - 0.5f);
            if (xr < xl) xr = xl;
            tft.fillRect(xl, y, xr - xl + 1, 1, BAT_AMBER);
        }
    }
}

void PyDisplay::drawBatteryPanel() {
    
    
    bool conn = batteryConnected(millis());
    lastDataRedraw = millis();

    // Clear icon + badge area (inside the panel, below the title)
    tft.fillRect(L_X + 2, 20, L_W - 4, 178, themeBg());

    // Battery icon (drawn primitives, web dashboard palette)
    drawBatteryIcon(soc, conn, stackStatus);

    // Restore title, panel border and right gutter over glyph bleed
    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("PYLONTECH", BATT_CX, 12);
    tft.setTextDatum(TL_DATUM);
    tft.drawRect(L_X, L_Y, L_W, L_H, TFT_DARKGREY);
    tft.fillRect(L_X + L_W + 1, L_Y + 1, R_X - (L_X + L_W + 1), L_H - 2, themeBg());

    // SOC readout badge (GLCD font)
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    if (conn) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", soc);
        tft.fillRoundRect(BATT_CX - BADGE_W / 2, BADGE_Y - BADGE_H / 2, BADGE_W, BADGE_H, 4, TFT_NAVY);
        tft.drawRoundRect(BATT_CX - BADGE_W / 2, BADGE_Y - BADGE_H / 2, BADGE_W, BADGE_H, 4, TFT_SKYBLUE);
        tft.setTextColor(TFT_WHITE, TFT_NAVY);
        tft.drawString(buf, BATT_CX, BADGE_Y);
    } else {
        tft.setTextColor(TFT_DARKGREY, themeBg());
        tft.drawString(tr("NO DATA", "SIN DATOS"), BATT_CX, BADGE_Y);
    }
    tft.setTextDatum(TL_DATUM);
    
    
}

void PyDisplay::drawValues() {
    const char *const *ROW_LABELS = spanish ? ROW_LABELS_ES : ROW_LABELS_EN;

    bool conn = batteryConnected(millis());

    // Clear right value area
    tft.fillRect(R_X, 5, 315 - R_X, 195, themeBg());

    // STATUS: centered value only, no label
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    if (conn) {
        String st = displayStatus(stackStatus, spanish);
        // Test the RAW protocol token so the red alarm colour works in both
        // languages (the displayed string is translated).
        tft.setTextColor((stackStatus == "Alarm!") ? TFT_RED : themeFg(), themeBg());
        tft.drawString(st, R_X + 90, STATUS_Y);
    } else {
        tft.setTextColor(TFT_DARKGREY, themeBg());
        tft.drawString(tr("No data", "Sin datos"), R_X + 90, STATUS_Y);
    }
    tft.setTextDatum(TL_DATUM);

    // Data rows: label size 2 + value on the same line
    tft.setTextSize(2);
    uint16_t vColor = conn ? themeFg() : TFT_DARKGREY;

    for (int i = 0; i < MAIN_ROW_COUNT; i++) {
        tft.setTextColor(TFT_DARKGREY, themeBg());
        tft.setCursor(R_X + 5, ROW_Y[i]);
        tft.print(ROW_LABELS[i]);
        tft.setTextColor(vColor, themeBg());
        String v;
        switch (i) {
            case 0: // Voltage
                if (conn) v = String(volt / 1000.0f, 1) + " V";
                else v = "--";
                break;
            case 1: // Current
                if (conn) v = String(curr / 1000.0f, 1) + " A";
                else v = "--";
                break;
            case 2: // Temperature
                // The GLCD degree glyph (0xF8) renders wrong on this panel; use a
                // plain space instead: "26 C".
                if (conn) { v = String(temp / 1000) + " C"; }
                else v = "--";
                break;
            case 3: // Power
                if (conn) v = String(powerW) + " W";
                else v = "--";
                break;
        }
        tft.setTextDatum(MR_DATUM);
        tft.drawString(v, VAL_X, ROW_Y[i] + 8);
        tft.setTextDatum(TL_DATUM);
    }

    // Events: only when the history has entries; red label, normal value colour.
    if (eventCount > 0) {
        tft.setTextColor(TFT_RED, themeBg());
        tft.setCursor(R_X + 5, ROW_Y[ROW_EVENTS]);
        tft.print(ROW_LABELS[ROW_EVENTS]);
        tft.setTextColor(vColor, themeBg());
        tft.setTextDatum(MR_DATUM);
        tft.drawString(String(eventCount), VAL_X, ROW_Y[ROW_EVENTS] + 8);
        tft.setTextDatum(TL_DATUM);
    }
    tft.setTextSize(1);
    
    
}

// -----------------------------
//  STATUS screen
// -----------------------------
// 16 device-health rows, scrolled 5 at a time. Every value is read from live
// runtime state in Task 2 (no cross-task sharing). Labels are dark grey; the
// value colour follows the table rule (themeFg() when neutral).

// Row labels, indexed by slot (see drawStatusRow). Declared once so the label
// list and the 16-row contract cannot drift apart.
static const char *const STATUS_LABELS_EN[17] = {
    "WiFi", "IP", "AP IP", "RSSI", "MQTT", "Broker", "Server", "Home Assistant disc", "Battery hub/stack",
    "UART", "Modules", "SD card", "SD free", "Export",
    "Heap", "CPU", "Version"
};
static const char *const STATUS_LABELS_ES[17] = {
    "WiFi", "IP", "AP IP", "RSSI", "MQTT", "Broker", "Servidor", "Descubr. HA", "Batería hub/stack",
    "UART", "Módulos", "Tarjeta SD", "SD libre", "Exportar",
    "Heap", "CPU", "Versión"
};
static const int STATUS_TOTAL = 17;
static const int STATUS_VISIBLE = 5;

void PyDisplay::drawStatusHeader() {
    tft.fillRect(0, 0, 320, MOD_FRAME_Y, themeBg());

    // Title
    tft.setTextSize(2);
    tft.setTextColor(themeFg(), themeBg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tr("STATUS", "ESTADO"), 160, 14);
    tft.setTextDatum(TL_DATUM);

    // Live clock (right-aligned), repaints only when the minute changes.
    drawStatusClock();

    // Back button (same geometry/style as the MODULES/CELLS headers)
    tft.fillRoundRect(HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H, 6, TFT_NAVY);
    tft.drawRoundRect(HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H, 6, TFT_SKYBLUE);
    tft.fillTriangle(HDR_BACK_X + 9, 14, HDR_BACK_X + 17, 9, HDR_BACK_X + 17, 19, TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(HDR_BACK_X + 22, HDR_BACK_Y + 8);
    tft.print(tr("BACK", "ATRAS"));
}

void PyDisplay::drawStatusClock() {
    // HH:MM (local, DST-aware like the rest of the UI) while the system time
    // is valid, otherwise a placeholder. Only repaints when the string changed,
    // so it repaints at most once per minute.
    time_t now = time(nullptr);
    char buf[16];
    if (now > 1700000000) {
        time_t localNow = now;
        if (config.timezone.startsWith("Europe/"))
            localNow = now + config.getDSTOffsetForTimezone();
        struct tm ti;
        localtime_r(&localNow, &ti);
        snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }

    String timeStr = String(buf);
    if (timeStr == lastTimeStr) return;
    lastTimeStr = timeStr;

    // HH:MM at size 2 is ~60 px wide, right-aligned at x=316 (left edge ~256).
    tft.fillRect(250, 3, tft.width() - 250, 22, themeBg());
    tft.setTextSize(2);
    tft.setTextColor(themeFg(), themeBg());
    tft.setTextDatum(MR_DATUM);
    tft.drawString(timeStr, 316, 14);
    tft.setTextDatum(TL_DATUM);
}

void PyDisplay::drawStatusRow(int16_t y, int slot) {
    const char *const *STATUS_LABELS = spanish ? STATUS_LABELS_ES : STATUS_LABELS_EN;
    const int16_t rowH = 30;
    if (slot < 0 || slot >= STATUS_TOTAL) return;

    tft.fillRect(4, y, tft.width() - 8, rowH, themeBg());

    String value;
    uint16_t color = themeFg();   // neutral default; rules override when needed

    switch (slot) {
        case 0: { // WiFi: joined SSID, or the AP SSID while in AP mode
            if (wifiAPMode) {
                value = "AP: " + config.apSSID;
                color = TFT_GREEN;
            } else if (wifiConnected) {
                value = wifiSSID.isEmpty() ? config.wifiSSID : wifiSSID;
                color = TFT_GREEN;
            } else {
                value = tr("offline", "sin conexión");
                color = TFT_RED;
            }
            break;
        }
        case 1: // IP (STA / current); avoids 0.0.0.0 when there is no STA
            value = (wifiConnected && !wifiIP.isEmpty()) ? wifiIP : String("-");
            break;
        case 2: // AP IP (only while the AP radio is up)
            if (wifiAPMode && !wifiAPIP.isEmpty()) {
                value = wifiAPIP;
                color = TFT_GREEN;
            } else {
                value = "-";
            }
            break;
        case 3: { // RSSI of the STA link (the AP itself has no RSSI)
            if (wifiConnected) {
                char b[20];
                snprintf(b, sizeof(b), "%d dBm STA", wifiRSSI);
                value = b;
                if (wifiRSSI > -67)      color = TFT_GREEN;
                else if (wifiRSSI > -80) color = TFT_YELLOW;
                else                     color = TFT_RED;
            } else {
                value = "-";
            }
            break;
        }
        case 4: // MQTT master switch
            value = config.mqtt.enabled ? tr("ON", "SI") : tr("OFF", "NO");
            color = config.mqtt.enabled ? TFT_GREEN : TFT_DARKGREY;
            break;
        case 5: // Broker connection state
            value = py_mqtt.isConnected() ? tr("connected", "conectado")
                                          : tr("disconnected", "desconectado");
            color = py_mqtt.isConnected() ? TFT_GREEN : TFT_RED;
            break;
        case 6: // Broker address
            value = config.mqtt.server + ":" + String(config.mqtt.port);
            break;
        case 7: // Home Assistant discovery
            value = py_mqtt.isDiscoveryActive() ? tr("on", "activado") : tr("off", "desactivado");
            color = py_mqtt.isDiscoveryActive() ? TFT_GREEN : TFT_DARKGREY;
            break;
        case 8: // Battery wiring mode
            value = (g_batteryMode == BatteryMode::STACK) ? "STACK" :
                    (g_batteryMode == BatteryMode::HUB)   ? "HUB" : tr("UNKNOWN", "DESCONOCIDO");
            break;
        case 9: // Battery UART link
            value = py_uart.isReady() ? tr("link OK", "enlace OK") : tr("no link", "sin enlace");
            color = py_uart.isReady() ? TFT_GREEN : TFT_RED;
            break;
        case 10: // Module count of the last PWR frame
            value = String(lastParsedStack.batteryCount);
            break;
        case 11: // SD card presence
            value = sdCard.isReady() ? tr("ready", "listo") : tr("absent", "ausente");
            color = sdCard.isReady() ? TFT_GREEN : TFT_RED;
            break;
        case 12: { // SD free space
            if (sdCard.isReady()) {
                int pct = (int)sdCard.getFreeSpacePercent();
                value = String(pct) + "%";
                if (pct < 10) color = TFT_YELLOW;
            } else {
                value = "--";
            }
            break;
        }
        case 13: // Export activity
            value = sdCard.isExporting() ? tr("in progress", "en curso") : tr("idle", "inactivo");
            color = sdCard.isExporting() ? TFT_YELLOW : TFT_DARKGREY;
            break;
        case 14: // Free heap (KB)
            value = String(SystemManager::getFreeHeap() / 1024) + " KB";
            break;
        case 15: { // CPU load per core
            uint8_t c0 = 0, c1 = 0;
            SystemManager::getCpuLoads(c0, c1);
            value = "c0 " + String(c0) + "% c1 " + String(c1) + "%";
            break;
        }
        case 16: // Firmware version
            value = config.firmwareVersion;
            break;
    }

    // The ES label array is UTF-8 (Batería/Módulos/Versión); encode it to CP437
    // for the GLCD font before measuring and drawing, exactly like tr() does.
    String label = cp437(STATUS_LABELS[slot]);

    tft.setTextSize(2);
    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setTextDatum(ML_DATUM);
    tft.drawString(label, 6, y + rowH / 2);

    // Truncate the value (drop trailing chars + "..") so it cannot collide with
    // the label. Leave a 10 px gutter between the two.
    int labelW = tft.textWidth(label);
    int maxValueW = 320 - 6 - labelW - 10;
    if (tft.textWidth(value) > maxValueW) {
        while (value.length() > 1 && tft.textWidth(value + "..") > maxValueW) {
            value.remove(value.length() - 1);
        }
        value += "..";
    }

    tft.setTextColor(color, themeBg());
    tft.setTextDatum(MR_DATUM);
    tft.drawString(value, 314, y + rowH / 2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
}

void PyDisplay::drawStatusRows() {
    lastDataRedraw = millis();

    // Rows area between the header (y=30) and the scroll buttons (y=210).
    const int16_t ROWS_Y = MOD_FRAME_Y + 2;   // 32
    const int16_t ROW_H = 30;
    int maxStart = STATUS_TOTAL - STATUS_VISIBLE;

    if (statusScroll > maxStart) statusScroll = maxStart;
    if (statusScroll < 0) statusScroll = 0;

    tft.fillRect(0, MOD_FRAME_Y, tft.width(), SCROLL_Y - MOD_FRAME_Y, themeBg());

    for (int i = 0; i < STATUS_VISIBLE; i++) {
        int slot = statusScroll + i;
        if (slot >= STATUS_TOTAL) break;
        drawStatusRow(ROWS_Y + i * ROW_H, slot);
    }

    // Clear the bottom strip left behind by the MAIN footer (STATUS + MODULES
    // buttons) before drawing the scroll controls, same as drawModuleList().
    tft.fillRect(0, SCROLL_Y - 2, tft.width(), tft.height() - (SCROLL_Y - 2), themeBg());

    // Scroll buttons + visible-window counter, matching the MODULES screen.
    bool upEnabled = statusScroll > 0;
    bool dnEnabled = statusScroll < maxStart;
    drawScrollButton(SCROLL_UP_X, SCROLL_Y, SCROLL_UP_W, upEnabled, true);
    drawScrollButton(SCROLL_DN_X, SCROLL_Y, SCROLL_DN_W, dnEnabled, false);

    char cnt[16];
    int first = statusScroll + 1;
    int last  = statusScroll + STATUS_VISIBLE;
    if (last > STATUS_TOTAL) last = STATUS_TOTAL;
    snprintf(cnt, sizeof(cnt), "%d-%d/%d", first, last, STATUS_TOTAL);
    tft.setTextSize(1);
    tft.setTextColor(themeFg(), themeBg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(cnt, 160, SCROLL_Y + SCROLL_H / 2);
    tft.setTextDatum(TL_DATUM);
}

// -----------------------------
//  Modules screen
// -----------------------------
void PyDisplay::drawModulesHeader() {
    
    
    tft.fillRect(0, 0, 320, MOD_FRAME_Y, themeBg());

    // Title
    tft.setTextSize(2);
    tft.setTextColor(themeFg(), themeBg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tr("MODULES", "MODULOS"), 160, 14);
    tft.setTextDatum(TL_DATUM);

    // Back button
    tft.fillRoundRect(HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H, 6, TFT_NAVY);
    tft.drawRoundRect(HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H, 6, TFT_SKYBLUE);
    tft.fillTriangle(HDR_BACK_X + 9, 14, HDR_BACK_X + 17, 9, HDR_BACK_X + 17, 19, TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(HDR_BACK_X + 22, HDR_BACK_Y + 8);
    tft.print(tr("BACK", "ATRAS"));
    
    
}

void PyDisplay::drawScrollButton(int16_t x, int16_t y, int16_t w, bool enabled, bool up) {
    uint16_t fill   = enabled ? TFT_NAVY   : 0x528A;
    uint16_t border = enabled ? TFT_SKYBLUE : TFT_DARKGREY;
    uint16_t tri    = enabled ? TFT_WHITE   : 0x4208;

    tft.fillRoundRect(x, y, w, SCROLL_H, 8, fill);
    tft.drawRoundRect(x, y, w, SCROLL_H, 8, border);

    int16_t cx = x + w / 2;
    int16_t cy = y + SCROLL_H / 2;
    if (up) {
        tft.fillTriangle(cx - 9, cy + 3, cx + 9, cy + 3, cx, cy - 7, tri);
    } else {
        tft.fillTriangle(cx - 9, cy - 3, cx + 9, cy - 3, cx, cy + 7, tri);
    }
}

void PyDisplay::drawModuleRow(int16_t y, const DisplayModule &m) {
    uint16_t bg = moduleColor(m.status);

    tft.fillRoundRect(MOD_ROW_X, y, MOD_ROW_W, MOD_ROW_H, 4, bg);
    tft.drawRoundRect(MOD_ROW_X, y, MOD_ROW_W, MOD_ROW_H, 4, 0x0000);

    // Module tag
    char tag[12];
    snprintf(tag, sizeof(tag), "M%d", m.index);
    tft.fillRoundRect(MOD_ROW_X + 3, y + 3, 42, MOD_ROW_H - 6, 3, TFT_NAVY);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tag, MOD_ROW_X + 24, y + MOD_ROW_H / 2);
    tft.setTextDatum(TL_DATUM);

    // Single line: voltage, current, SOC, temperature, cell delta
    char line[40];
    if (m.cellDiff_mV >= 0) {
        snprintf(line, sizeof(line), "%.1fV %.1fA %d%% %d.%dC d%dmV",
                 m.volt_mV / 1000.0f, m.curr_mA / 1000.0f, m.soc,
                 m.temp_mC / 1000, abs(m.temp_mC % 1000) / 100, m.cellDiff_mV);
    } else {
        snprintf(line, sizeof(line), "%.1fV %.1fA %d%% %d.%dC d--",
                 m.volt_mV / 1000.0f, m.curr_mA / 1000.0f, m.soc,
                 m.temp_mC / 1000, abs(m.temp_mC % 1000) / 100);
    }
    tft.setTextFont(2);   // 8x16: text twice as tall as the old 6x8
    tft.setTextSize(1);
    tft.setTextColor(TFT_BLACK, bg);
    tft.setCursor(MOD_ROW_X + 54, y + (MOD_ROW_H - 16) / 2);
    tft.print(line);
    tft.setTextFont(1);
}

void PyDisplay::drawPlaceholderRow(int16_t y, int index) {
    tft.fillRoundRect(MOD_ROW_X, y, MOD_ROW_W, MOD_ROW_H, 4, COLOR_PH_BG);
    tft.drawRoundRect(MOD_ROW_X, y, MOD_ROW_W, MOD_ROW_H, 4, 0x0000);

    // Module tag
    char tag[12];
    snprintf(tag, sizeof(tag), "M%d", index);
    tft.fillRoundRect(MOD_ROW_X + 3, y + 3, 42, MOD_ROW_H - 6, 3, COLOR_PH_TAG);
    tft.setTextSize(2);
    tft.setTextColor(0xC618, COLOR_PH_TAG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tag, MOD_ROW_X + 24, y + MOD_ROW_H / 2);
    tft.setTextDatum(TL_DATUM);

    // Placeholder values in one line
    tft.setTextFont(2);   // 8x16: match drawModuleRow
    tft.setTextSize(1);
    tft.setTextColor(COLOR_PH_TXT, COLOR_PH_BG);
    tft.setCursor(MOD_ROW_X + 54, y + (MOD_ROW_H - 16) / 2);
    tft.print("--.-V  --.-A  --%  --.-C  d--");
    tft.setTextFont(1);
}

int PyDisplay::visibleTotal() const {
    if (modules.empty()) return MOD_EMPTY_ROWS;
    return (int)modules.size();
}

void PyDisplay::drawModuleList() {
    
    
    int total = visibleTotal();
    lastDataRedraw = millis();
    int lastPageStart = ((total - 1) / MOD_VISIBLE) * MOD_VISIBLE;  // pages of 5: 10->0/5
    if (scrollOffset > lastPageStart) scrollOffset = lastPageStart;

    // List area clear + frame
    tft.fillRect(MOD_FRAME_X, MOD_FRAME_Y, MOD_FRAME_W, MOD_FRAME_H, themeBg());
    tft.drawRect(MOD_FRAME_X, MOD_FRAME_Y, MOD_FRAME_W, MOD_FRAME_H, TFT_DARKGREY);

    int realCount = (int)modules.size();
    int rowY = MOD_FRAME_Y + 2;
    for (int i = 0; i < MOD_VISIBLE; i++) {
        int slot = scrollOffset + i;
        if (slot < realCount) {
            drawModuleRow(rowY, modules[slot]);
        } else if (slot < total) {
            drawPlaceholderRow(rowY, slot + 1);
        }
        rowY += MOD_ROW_H + MOD_ROW_GAP;
    }

    // Clear the bottom strip left behind by the main screen footer
    // (clock, WiFi, MODULES button) before drawing the scroll controls.
    tft.fillRect(0, SCROLL_Y - 2, tft.width(), tft.height() - (SCROLL_Y - 2), themeBg());

    // Scroll buttons + counter
    bool upEnabled = scrollOffset > 0;
    bool dnEnabled = scrollOffset < lastPageStart;
    drawScrollButton(SCROLL_UP_X, SCROLL_Y, SCROLL_UP_W, upEnabled, true);
    drawScrollButton(SCROLL_DN_X, SCROLL_Y, SCROLL_DN_W, dnEnabled, false);

    char cnt[12];
    // X/Y counter shows the LAST module on the current page (clamped to the
    // real total, so the last partial page reads e.g. 10/10).
    int lastShown = scrollOffset + MOD_VISIBLE;
    if (lastShown > total) lastShown = total;
    snprintf(cnt, sizeof(cnt), "%d/%d", lastShown, total);
    tft.setTextSize(2);
    tft.setTextColor(themeFg(), themeBg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(cnt, 160, SCROLL_Y + SCROLL_H / 2);
    tft.setTextDatum(TL_DATUM);
    
    
}

void PyDisplay::drawCellScreen() {
    
    
    lastDataRedraw = millis();
    tft.fillRect(0, 0, 320, CL_GRID_Y, themeBg());

    // Title
    tft.setTextSize(2);
    tft.setTextColor(themeFg(), themeBg());
    tft.setTextDatum(MC_DATUM);
    // Nudged right of centre so the BACK button (x 4..72) never overlaps the
    // title's first letter, even with a two-digit module index.
    tft.drawString(tr("CELLS MODULE ", "CELDAS MODULO ") + String(cellsModuleIndex), 178, 14);
    tft.setTextDatum(TL_DATUM);

    // Back button
    tft.fillRoundRect(HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H, 6, TFT_NAVY);
    tft.drawRoundRect(HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H, 6, TFT_SKYBLUE);
    tft.fillTriangle(HDR_BACK_X + 9, 14, HDR_BACK_X + 17, 9, HDR_BACK_X + 17, 19, TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setCursor(HDR_BACK_X + 22, HDR_BACK_Y + 8);
    tft.print(tr("BACK", "ATRAS"));

    // Clear grid + summary area
    tft.fillRect(0, CL_GRID_Y, 320, tft.height() - CL_GRID_Y, themeBg());

    // Cells of the module currently on screen. Each module only ever shows its
    // OWN payload; a module without BAT data yet renders "--" placeholders.
    const std::vector<WebCell> *viewCells = nullptr;
    if (cellsModuleIndex >= 1 && cellsModuleIndex <= MAX_MODULES &&
        cellStore.size() > (size_t)cellsModuleIndex && cellStore[cellsModuleIndex].have) {
        viewCells = &cellStore[cellsModuleIndex].cells;
    }
    const size_t viewCount = viewCells ? viewCells->size() : 0;

    // Find min/max of the current frame
    int frameMax = -1, frameMin = -1;
    int maxCell = -1, minCell = -1;
    for (size_t i = 0; i < viewCount; i++) {
        int v = (*viewCells)[i].volt_mV;
        if (v <= 0) continue;
        if (frameMax == -1 || v > frameMax) { frameMax = v; maxCell = (int)i + 1; }
        if (frameMin == -1 || v < frameMin) { frameMin = v; minCell = (int)i + 1; }
    }
    bool allSame = (frameMax >= 0 && frameMax == frameMin);

    // Cell grid: two columns of up to CL_ROWS_PER_COL cells. A size-2 font is
    // used so the individual cell values are easier to read; the reused vertical
    // space comes from collapsing the summary below into three rows.
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);
    bool hasCells = false;
    for (size_t i = 0; i < viewCount; i++) {
        if ((*viewCells)[i].volt_mV > 0) { hasCells = true; break; }
    }
    int gridCount = hasCells ? (int)viewCount : CL_EMPTY_CELLS;
    int col = 0, row = 0;
    for (int i = 0; i < gridCount; i++) {
        if (col > 1) break;  // only supports two columns

        int x = (col == 0) ? CL_COL1_X : CL_COL2_X;
        int16_t y = CL_GRID_Y + 2 + row * CL_ROW_H;

        int cIndex = i + 1;
        int cVolt = hasCells ? (*viewCells)[i].volt_mV : 0;

        char tag[8];
        snprintf(tag, sizeof(tag), "C%02d", cIndex);
        tft.setTextColor(TFT_DARKGREY, themeBg());
        tft.setCursor(x, y);
        tft.print(tag);

        tft.setCursor(x + 42, y);
        if (cVolt > 0) {
            bool hl = false;
            uint16_t vColor = themeFg();
            if (!allSame && cIndex == maxCell) {
                vColor = TFT_RED;
                hl = true;
            } else if (!allSame && cIndex == minCell) {
                vColor = 0x001F;  // blue
                hl = true;
            }
            // Red/blue highlight cells are drawn twice 1px apart (fake bold,
            // the GLCD font has no bold variant) to stand out from the rest.
            char buf[12];
            snprintf(buf, sizeof(buf), "%d.%03dV", cVolt / 1000, cVolt % 1000);
            tft.setTextColor(vColor, themeBg());
            tft.drawString(buf, x + 42, y);
            if (hl) tft.drawString(buf, x + 43, y);
        } else {
            tft.setTextColor(COLOR_PH_TXT, themeBg());
            tft.print("--");
        }

        // advance: row-major column fill (8 per column)
        row++;
        if (row >= CL_ROWS_PER_COL) { row = 0; col++; }
    }

    // Separator under the grid
    tft.drawFastHLine(5, CL_SEP_Y, 310, TFT_DARKGREY);

    // Summary: three rows x two columns. Each row pairs the current-frame value
    // (left column) with the matching today's extreme (right column), so the
    // summary fits in half the vertical space the old stacked layout needed.
    // Extremes come from the shared day-store (fed by PWR frames when BAT polling
    // is disabled, by BAT frames otherwise). Cell numbers are only shown when the
    // value was actually captured from a BAT frame (last*Cell>0); PWR-sourced
    // values store cell 0 and render without them. Today's capture times are only
    // rendered while BAT parsing stays enabled: the store may be holding earlier
    // BAT-sourced times that must stay hidden once disabled.
    CellDayStats st;
    if (cellsModuleIndex >= 1 && cellsModuleIndex <= MAX_MODULES) {
        st = cellDayStats[cellsModuleIndex];
    }
    bool showTodayCells = config.battery.enableBat;
    int todayKey = currentDayKey();
    // Never present yesterday's values as "today": stats are only shown once a
    // new-frame of the CURRENT day has been captured (or reset by the midnight
    // rollover in loop()).
    bool hasToday = (todayKey >= 0 && st.dayKey == todayKey);

    tft.setTextSize(1);

    // Row 1: Max V (current) | Max V today
    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setCursor(CL_SUM_X1, CL_SUM_Y1);
    tft.print(tr("Max V: ", "V max: "));
    if (st.lastMax_mV >= 0) {
        tft.setTextColor(themeFg(), themeBg());
        tft.printf("%d.%03dV", st.lastMax_mV / 1000, st.lastMax_mV % 1000);
        if (st.lastMaxCell > 0)
            tft.printf(" (c%d)", st.lastMaxCell);
    } else {
        tft.setTextColor(COLOR_PH_TXT, themeBg());
        tft.print("--");
    }

    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setCursor(CL_SUM_X2, CL_SUM_Y1);
    tft.print(tr("Max V today: ", "V max hoy: "));
    if (hasToday && st.todayMax_mV >= 0) {
        tft.setTextColor(themeFg(), themeBg());
        tft.printf("%d.%03dV", st.todayMax_mV / 1000, st.todayMax_mV % 1000);
        if (showTodayCells && st.todayMaxMin >= 0)
            tft.printf(" (%02d:%02d)", st.todayMaxMin / 60, st.todayMaxMin % 60);
    } else {
        tft.setTextColor(COLOR_PH_TXT, themeBg());
        tft.print("--");
    }

    // Row 2: Min V (current) | Min V today
    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setCursor(CL_SUM_X1, CL_SUM_Y2);
    tft.print(tr("Min V: ", "V min: "));
    if (st.lastMin_mV >= 0) {
        tft.setTextColor(themeFg(), themeBg());
        tft.printf("%d.%03dV", st.lastMin_mV / 1000, st.lastMin_mV % 1000);
        if (st.lastMinCell > 0)
            tft.printf(" (c%d)", st.lastMinCell);
    } else {
        tft.setTextColor(COLOR_PH_TXT, themeBg());
        tft.print("--");
    }

    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setCursor(CL_SUM_X2, CL_SUM_Y2);
    tft.print(tr("Min V today: ", "V min hoy: "));
    if (hasToday && st.todayMin_mV >= 0) {
        tft.setTextColor(themeFg(), themeBg());
        tft.printf("%d.%03dV", st.todayMin_mV / 1000, st.todayMin_mV % 1000);
        if (showTodayCells && st.todayMinMin >= 0)
            tft.printf(" (%02d:%02d)", st.todayMinMin / 60, st.todayMinMin % 60);
    } else {
        tft.setTextColor(COLOR_PH_TXT, themeBg());
        tft.print("--");
    }

    // Row 3: Current Delta (two cells -> no single cell shown)
    //        | Max Delta today (two cells -> time only)
    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setCursor(CL_SUM_X1, CL_SUM_Y3);
    tft.print(tr("Current Delta: ", "Delta actual: "));
    if (st.lastDelta_mV >= 0) {
        tft.setTextColor(themeFg(), themeBg());
        tft.print(st.lastDelta_mV);
        tft.setTextColor(TFT_DARKGREY, themeBg());
        tft.print("mV");
    } else {
        tft.setTextColor(COLOR_PH_TXT, themeBg());
        tft.print("--");
    }

    tft.setTextColor(TFT_DARKGREY, themeBg());
    tft.setCursor(CL_SUM_X2, CL_SUM_Y3);
    tft.print(tr("Max Delta today: ", "Delta max hoy: "));
    if (hasToday && st.todayDelta_mV >= 0) {
        tft.setTextColor(themeFg(), themeBg());
        tft.print(st.todayDelta_mV);
        tft.setTextColor(TFT_DARKGREY, themeBg());
        tft.print("mV");
        if (showTodayCells && st.todayDeltaMin >= 0) {
            tft.setTextColor(themeFg(), themeBg());
            tft.printf(" %02d:%02d", st.todayDeltaMin / 60, st.todayDeltaMin % 60);
        }
    } else {
        tft.setTextColor(COLOR_PH_TXT, themeBg());
        tft.print("--");
    }
    
    
}

void PyDisplay::handleTap(int16_t x, int16_t y) {
    if (screen == DisplayScreen::MAIN) {
        if (inRect(x, y, BTN_STATUS_X, BTN_MOD_Y, BTN_MOD_W, BTN_MOD_H)) {
            statusScroll = 0;
            screen = DisplayScreen::STATUS;
            lastTimeStr = "";
            drawStatusHeader();
            drawStatusRows();
        } else if (inRect(x, y, BTN_MOD_X, BTN_MOD_Y, BTN_MOD_W, BTN_MOD_H)) {
            scrollOffset = 0;
            modulesDirty = true;
            screen = DisplayScreen::MODULES;
            drawModulesHeader();
            drawModuleList();
        }
        return;
    }

    // STATUS screen: BACK returns to MAIN; UP/DOWN scroll and redraw immediately.
    if (screen == DisplayScreen::STATUS) {
        int maxStart = STATUS_TOTAL - STATUS_VISIBLE;
        if (inRect(x, y, HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H)) {
            screen = DisplayScreen::MAIN;
            needsRedraw = true;
            lastTimeStr = "";
            drawStaticUI();
            drawBatteryPanel();
            drawValues();
        } else if (inRect(x, y, SCROLL_UP_X, SCROLL_Y, SCROLL_UP_W, SCROLL_H) && statusScroll > 0) {
            statusScroll -= STATUS_VISIBLE;
            if (statusScroll < 0) statusScroll = 0;
            drawStatusRows();
        } else if (inRect(x, y, SCROLL_DN_X, SCROLL_Y, SCROLL_DN_W, SCROLL_H) && statusScroll < maxStart) {
            statusScroll += STATUS_VISIBLE;
            if (statusScroll > maxStart) statusScroll = maxStart;
            drawStatusRows();
        }
        return;
    }

    // CELLS detail screen → BACK returns to MODULES. There is no manual refresh
    // button: the shown module is fetched on entry and re-polled by the
    // scheduler while this screen is active (config.battery.cellsScreenActive).
    if (screen == DisplayScreen::CELLS) {
        if (inRect(x, y, HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H)) {
            screen = DisplayScreen::MODULES;
            modulesDirty = true;
            drawModulesHeader();
            drawModuleList();
        }
        return;
    }

    // MODULES screen
    if (inRect(x, y, HDR_BACK_X, HDR_BACK_Y, HDR_BACK_W, HDR_BACK_H)) {
        screen = DisplayScreen::MAIN;
        needsRedraw = true;
        lastTimeStr = "";
        drawStaticUI();
        drawBatteryPanel();
        drawValues();
        return;
    }

    // Tap on a module row → open cell detail screen. Works for real rows
    // and placeholder rows (when no module data has arrived yet).
    int realCount = (int)modules.size();
    int total = visibleTotal();
    for (int i = 0; i < MOD_VISIBLE; i++) {
        int slot = scrollOffset + i;
        if (slot >= total) break;
        int16_t ry = MOD_FRAME_Y + 2 + i * (MOD_ROW_H + MOD_ROW_GAP);
        if (inRect(x, y, MOD_ROW_X, ry, MOD_ROW_W, MOD_ROW_H)) {
            cellsModuleIndex = (slot < realCount) ? modules[slot].index : (slot + 1);
            cellsDirty = true;
            screen = DisplayScreen::CELLS;
            drawCellScreen();
            // BAT polling disabled → fetch this module's cell frame on demand
            // so the CELLS view shows real values instead of placeholders.
            // lastDataRedraw = 0 (set AFTER drawCellScreen, which overwrites it
            // with millis()) makes loop() repaint as soon as the frame arrives.
            if (slot < realCount && !config.battery.enableBat &&
                g_batteryMode == BatteryMode::STACK) {
                py_scheduler.enqueue("bat " + String(cellsModuleIndex));
                lastDataRedraw = 0;
            }
            return;
        }
    }

    int lastPageStart = ((total - 1) / MOD_VISIBLE) * MOD_VISIBLE;

    if (inRect(x, y, SCROLL_UP_X, SCROLL_Y, SCROLL_UP_W, SCROLL_H) && scrollOffset > 0) {
        scrollOffset -= MOD_VISIBLE;
        if (scrollOffset < 0) scrollOffset = 0;
        drawModuleList();
    } else if (inRect(x, y, SCROLL_DN_X, SCROLL_Y, SCROLL_DN_W, SCROLL_H) && scrollOffset < lastPageStart) {
        scrollOffset += MOD_VISIBLE;
        if (scrollOffset > lastPageStart) scrollOffset = lastPageStart;
        drawModuleList();
    }
}

void PyDisplay::pollTouch() {
    // Master switch: when the screen is disabled the touch is completely dead —
    // not even a tap can wake it. This runs BEFORE the wake-from-off logic.
    if (!screenEnabled) return;
    // Touch always stays live while the screen is OFF so a tap can wake it up,
    // regardless of the "Touch" setting. The setting only applies while ON.
    if (!touchEnabled && screenOn) return;
    unsigned long now = millis();
    if (now - lastTouchPoll < 30) return;
    lastTouchPoll = now;
    if (now < touchReadyAt) return;

    // Press detection: IRQ active-low, matching the manufacturer driver exactly.
    // Z-pressure fallback removed — spurious Z values above the threshold
    // created phantom edges that consumed real taps before they could dispatch.
    bool pressed = xptPressed();
    if (pressed && !touchPressed) {
        // Brief settle lets the XPT2046 ADC stabilize after the contact edge,
        // then xptReadMedian (mirrors TP_Read_XOY) takes 16 raw samples, sorts
        // them, drops the 2 extremes on each side, and averages the 12 central
        // values. The resulting coordinate is stable enough to hit small buttons
        // (68 px wide BACK) in a single press, unlike the noisy single-read or
        // 4-sample average that was causing repeated-press behaviour.
        delay(5);
        if (xptPressed()) {
            lastTouchTime = millis();
            // Screen OFF: a tap only wakes it (backlight on). The tap is NOT
            // dispatched as navigation — the user cannot know what they pressed
            // while the screen was dark, and the current view is kept intact.
            if (!screenOn) {
                setScreenOn(true);
                touchPressed = pressed;
                return;
            }
            uint16_t rxAve = 0, ryAve = 0;
            readTouchMedian(rxAve, ryAve);
            uint16_t cx = rxAve, cy = ryAve;
            convertTouch(cx, cy);
            Log(LOG_INFO, "TOUCH raw=(" + String(rxAve) + "," + String(ryAve) +
                ") cal=(" + String(cx) + "," + String(cy) + ")");
            if (cx < tft.width() && cy < tft.height()) {
                handleTap(cx, cy);
            }
        }
    }
    touchPressed = pressed;
    // A held touch also counts as activity so the screen stays alive while the
    // user keeps pressing (poll runs every 30 ms here).
    if (pressed) lastTouchTime = millis();
}

void PyDisplay::loop() {
    // Scope the scheduler's CELLS-screen BAT polling to the view actually on
    // screen. Evaluated before the master-switch early return so turning the
    // backlight off (auto power-off, schedule) or disabling the display also
    // clears it; otherwise a single visit to CELLS would keep enqueueing
    // `bat N` for every present module at intervalBat forever, even with
    // enableBat=false. Single writer for config.battery.cellsScreenActive.
    config.battery.cellsScreenActive =
        screenEnabled && screenOn && screen == DisplayScreen::CELLS;

    // Master switch: the whole screen functionality (drawing, power timers,
    // auto power-off) is inert while the screen is disabled.
    if (!screenEnabled) return;
    unsigned long now = millis();

    

    // Power timer: turn the screen off/on at the configured times. Requires a
    // valid system time (NTP or manual); evaluated as the minute rolls over.
    {
        static unsigned long lastTimerCheck = 0;
        if (now - lastTimerCheck >= 1000) {
            lastTimerCheck = now;
            int h = 0, m = 0;
            if (currentHourMinute(h, m)) {
                int key = h * 60 + m;
                if (key != lastMinuteKey) {
                    lastMinuteKey = key;
                    // Scheduling is a single toggle: both hours are active together.
                    if (scheduled && screenOn && key == (int)offHour * 60 + offMin) {
                        setScreenOn(false);
                    }
                    if (scheduled && !screenOn && key == (int)onHour * 60 + onMin) {
                        setScreenOn(true);
                    }
                }
            } else if (lastMinuteKey != -1) {
                // Time became invalid (e.g. NTP lost) — keep the current state,
                // just forget the last minute so we re-evaluate on re-sync.
                lastMinuteKey = -1;
            }

            // Auto power-off: after autoOffSec without any touch, switch the
            // screen off. A later touch wakes it back up (see pollTouch).
            if (screenOn && autoOff &&
                now - lastTouchTime >= (unsigned long)autoOffSec * 1000UL) {
                setScreenOn(false);
            }

            // Day rollover: at 00:00 (or the first moment time becomes valid)
            // clear every module's "today" cell stats. Runs once per day even
            // if no BAT frame for a given module arrives after midnight.
            static int lastStatsDayKey = -1;
            int dk = currentDayKey();
            if (dk != lastStatsDayKey) {
                lastStatsDayKey = dk;
                if (dk > 0) {
                    for (int mi = 1; mi <= MAX_MODULES; mi++) {
                        CellDayStats &s = cellDayStats[mi];
                        if (s.dayKey != dk) {
                            s = CellDayStats();
                            s.dayKey = dk;
                        }
                    }
                    if (screen == DisplayScreen::CELLS) cellsDirty = true;
                }
            }
        }
    }

    if (screen == DisplayScreen::MAIN) {
        if (needsRedraw && (refreshMs == 0 || now - lastDataRedraw >= refreshMs)) {
            needsRedraw = false;
            lastDataRedraw = now;
            drawBatteryPanel();
            drawValues();
        }
    } else if (screen == DisplayScreen::MODULES) {
        if (modulesDirty && (refreshMs == 0 || now - lastDataRedraw >= refreshMs)) {
            modulesDirty = false;
            lastDataRedraw = now;
            drawModuleList();
        }
    } else if (screen == DisplayScreen::CELLS) {
        if (cellsDirty && (refreshMs == 0 || now - lastDataRedraw >= refreshMs)) {
            cellsDirty = false;
            lastDataRedraw = now;
            drawCellScreen();
        }
    } else if (screen == DisplayScreen::STATUS) {
        // Rows repaint on the data-refresh throttle only; drawStatusClock()
        // early-returns while HH:MM is unchanged, so it repaints at most once
        // per minute and the screen never flickers per second.
        uint32_t period = (refreshMs > 0) ? refreshMs : 1000;
        if (now - lastDataRedraw >= period) {
            drawStatusRows();   // sets lastDataRedraw
        }
        drawStatusClock();      // repaints only when HH:MM changes
    }
}

void PyDisplay::updatePwr(int volt_mV, int curr_mA, int temp_mC, int socVal, const String &status) {
    if (volt == volt_mV &&
        curr == curr_mA &&
        temp == temp_mC &&
        soc  == socVal &&
        stackStatus == status &&
        lastHasData) {
        lastDataTime = millis(); // keep-alive even if identical
        return;
    }
    volt = volt_mV;
    curr = curr_mA;
    temp = temp_mC;
    soc  = socVal;
    stackStatus = status;
    // 64-bit product: mV × mA exceeds 2^31 above ~42 A at 50 V, which wrapped
    // to a negative wattage when computed in a 32-bit long.
    powerW = (long)((long long)volt_mV * curr_mA / 1000000LL);
    lastHasData = true;
    lastDataTime = millis();
    needsRedraw = true;
}

void PyDisplay::updateModules(const std::vector<DisplayModule> &mods) {
    bool same = (modules.size() == mods.size());
    if (same) {
        for (size_t i = 0; i < mods.size(); i++) {
            if (modules[i].index      != mods[i].index ||
                modules[i].soc        != mods[i].soc   ||
                modules[i].volt_mV    != mods[i].volt_mV ||
                modules[i].curr_mA    != mods[i].curr_mA ||
                modules[i].temp_mC    != mods[i].temp_mC ||
                modules[i].cellDiff_mV != mods[i].cellDiff_mV ||
                modules[i].status     != mods[i].status) {
                same = false;
                break;
            }
        }
    }
    modules = mods;
    if (!same) modulesDirty = true;
}

void PyDisplay::updateCells(int moduleIndex) {
    if (moduleIndex < 1 || moduleIndex > MAX_MODULES) return;
    if (cellStore.size() <= (size_t)moduleIndex) return;

    const std::vector<WebCell> &cells = cellStore[moduleIndex].cells;

    int frameMax = -1, frameMin = -1;
    int maxCell = -1, minCell = -1;
    for (size_t i = 0; i < cells.size(); i++) {
        int v = cells[i].volt_mV;
        if (v <= 0) continue;
        if (frameMax == -1 || v > frameMax) { frameMax = v; maxCell = (int)i + 1; }
        if (frameMin == -1 || v < frameMin) { frameMin = v; minCell = (int)i + 1; }
    }

    // Feed the shared day-store. A batch of identical payloads must not keep
    // recapturing the same values, but the first frame after the midnight
    // rollover (even if byte-identical to the last one of the previous day)
    // must still be captured so today's stats are not left empty.
    //
    // Only min/max and their cell numbers drive updateCellStats, so "changed"
    // compares those four fields against the previous frame, which the day-store
    // keeps in its last* values (the display no longer holds a cell copy).
    CellDayStats &st = cellDayStats[moduleIndex];
    int dayKey = currentDayKey();
    bool freshToday = (dayKey >= 0 && st.dayKey == dayKey);
    bool changed = (frameMin != st.lastMin_mV || frameMax != st.lastMax_mV ||
                    minCell != st.lastMinCell || maxCell != st.lastMaxCell);

    if (frameMax > 0 && frameMin > 0 && (changed || !freshToday)) {
        if (config.battery.enableBat)
            updateCellStats(moduleIndex, frameMin, frameMax, minCell, maxCell, true);
    }

    // Only repaint when the module the CELLS screen shows is the one updated.
    // The view selector (cellsModuleIndex) is owned by the user's taps.
    if (cellsModuleIndex == moduleIndex) {
        cellsDirty = true;
        if (screen == DisplayScreen::CELLS) lastDataRedraw = 0;
    }
}

void PyDisplay::updateWifi(bool connected, const String &ip, int rssi, bool apMode, const String &apIp) {
    // Cache the joined SSID for the STATUS screen. WiFiManagerModule::getStatus()
    // builds several Strings, so do NOT call it every loop: only re-query on a
    // real STA connect transition (or when nothing is cached yet). Clear it while
    // offline/in AP so a reconnect re-reads it and a changed SSID is picked up.
    String prevSSID = wifiSSID;
    if (!connected || apMode) {
        wifiSSID = "";
    } else if (wifiSSID.isEmpty() || !wifiConnected) {
        wifiSSID = WiFiManagerModule::getStatus().ssid;
    }

    if (wifiConnected == connected &&
        wifiIP == ip &&
        wifiRSSI == rssi &&
        wifiAPMode == apMode &&
        wifiAPIP == apIp &&
        prevSSID == wifiSSID) {
        return;
    }
    wifiConnected = connected;
    wifiIP = ip;
    wifiRSSI = rssi;
    wifiAPMode = apMode;
    wifiAPIP = apIp;
}
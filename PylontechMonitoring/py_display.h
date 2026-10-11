#pragma once
#include <Arduino.h>
#include <vector>
#include <TFT_eSPI.h>
#include "config.h"

struct ScreenConfig;   // forward decl (defined in config.h)

enum class DisplayScreen { MAIN, MODULES, CELLS, STATUS };

struct DisplayModule {
    int index = 0;
    int soc = 0;
    int volt_mV = 0;
    int curr_mA = 0;
    int temp_mC = 0;
    int cellDiff_mV = -1;   // -1 = no data available
    uint8_t status = 0;     // 0 = OK, 1 = Warning, 2 = Error
};

class PyDisplay {
public:
    void begin();
    void loop();

    void updatePwr(int volt_mV, int curr_mA, int temp_mC, int soc, const String &status);
    void updateModules(const std::vector<DisplayModule> &mods);
    void updateCells(int moduleIndex);
    void setEventCount(int n);

    void updateWifi(bool connected, const String &ip, int rssi, bool apMode, const String &apIp);

    void pollTouch();
    void setBrightness(uint8_t percent);
    void setScreenOn(bool on);
    void setTouchEnabled(bool enabled);
    void applyScreenConfig(const ScreenConfig &cfg);
    void setLanguage(const String &lang);
    bool isScreenOn() const;

    bool readTouchMedian(uint16_t &rx, uint16_t &ry);
    void convertTouch(uint16_t &cx, uint16_t &cy);

private:
    void drawStaticUI();
    void drawBatteryPanel();
    void drawValues();
    void drawMainButton();
    bool batteryConnected(unsigned long now);
    void writeBacklight();

    // Translate a user-facing literal: returns the Spanish string (encoded to
    // CP437 for the GLCD font) when Spanish is active, otherwise English.
    String tr(const char *en, const char *es) const;

    void initTouch();
    bool loadCalibration(uint16_t cal[5]) const;
    void saveCalibration(const uint16_t cal[5]);

    void drawModulesHeader();
    void drawModuleList();
    void drawModuleRow(int16_t y, const DisplayModule &m);
    void drawPlaceholderRow(int16_t y, int index);
    void drawScrollButton(int16_t x, int16_t y, int16_t w, bool enabled, bool up);
    void drawCellScreen();
    void drawStatusHeader();
    void drawStatusRows();
    void drawStatusClock();
    void drawStatusRow(int16_t y, int slot);
    void handleTap(int16_t x, int16_t y);
    bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) const;
    int visibleTotal() const;

    int volt = 0;
    int curr = 0;
    int temp = 0;
    int soc  = 0;
    long powerW = 0;
    String stackStatus = "";

    bool lastHasData = false;
    unsigned long lastDataTime = 0;

    // Health event-history count shown on the main screen (0 = row hidden).
    int eventCount = 0;

    bool wifiConnected = false;
    String wifiIP = "";
    int wifiRSSI = 0;
    bool wifiAPMode = false;
    String wifiAPIP = "";   // AP IP while the AP radio is up (empty otherwise)
    String wifiSSID = "";   // currently joined SSID (empty when AP/offline)

    String lastTimeStr = "";
    unsigned long lastTouchPoll = 0;
    unsigned long touchReadyAt = 0;

    // Screen state (runtime). screenOn = backlight on/off (auto-off / power timer);
    // screenEnabled = master switch from config: when false the display neither
    // draws nor accepts touch (not even wake), and the screen starts OFF at boot.
    bool screenEnabled = true;
    bool screenOn = true;
    bool touchEnabled = true;
    uint8_t brightnessPct = 60;
    bool spanish = false;   // true = draw user-facing text in Spanish
    bool scheduled = false;
    uint8_t offHour = 23, offMin = 0;
    uint8_t onHour = 7, onMin = 0;
    int lastMinuteKey = -1;

    uint16_t touchCal[5] = {1, 1, 1, 1, 0};

    bool needsRedraw = true;

    DisplayScreen screen = DisplayScreen::MAIN;
    std::vector<DisplayModule> modules;
    bool modulesDirty = true;
    int scrollOffset = 0;
    bool touchPressed = false;

    // STATUS screen scroll position (row index of the first visible row).
    int statusScroll = 0;

    // Min interval (ms) between data-driven redraws; 0 = redraw on every update.
    uint32_t refreshMs = 10000;
    unsigned long lastDataRedraw = 0;

    // Auto power-off: screen turns off after autoOffSec of touch inactivity;
    // any touch (with touch enabled, or always while the screen is OFF)
    // resets lastTouchTime and wakes the screen when it is off.
    bool autoOff = false;
    uint16_t autoOffSec = 30;
    unsigned long lastTouchTime = 0;

    // Cell detail screen. The cell payload lives in the shared per-module
    // cellStore (py_cellstore.h); only the currently shown module is tracked here.
    int cellsModuleIndex = -1;   // module currently shown (set only by user tap)
    bool cellsDirty = false;

    };

extern PyDisplay display;
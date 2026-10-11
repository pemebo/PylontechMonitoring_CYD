#include "py_uart.h"
#include "py_log.h"
#include "py_parser_pwr.h"
#include "py_parser_bat.h"
#include "py_parser_stat.h"
#include "py_sdcard.h"

#include "config.h"   // contains PwrBuffer, BatBuffer, StatBuffer + Flags
#include <new>
#include <exception>
#include "esp_heap_caps.h"


static char g_szRecvBuff[7000];
static int g_invalidCount = 0;

// Consecutive frame-level voltage rejections per flow (Task-1-local, like
// g_invalidCount). A structural failure (g_invalidCount) must not reset these,
// and a good frame of the same flow resets its own counter. A single LOG_ERROR
// is emitted when a streak reaches exactly VOLTAGE_STREAK_ALERT.
static int g_pwrVoltStreak = 0;
static int g_batVoltStreak[MAX_MODULES + 1] = {0};

// ---------------------------------------------------------
static bool isValidFrame(const String& f) {
    if (f.indexOf("@") < 0) return false;
    if (f.indexOf("$$") < 0) return false;
    if (f.indexOf("pylon>") < 0) return false;
    if (f.length() < 40) return false;

    int lines = 0;
    for (int i = 0; i < f.length(); i++)
        if (f[i] == '\n') lines++;
    if (lines < 3) return false;

    if (f.indexOf("Press [Enter]") >= 0 &&
        f.indexOf("Remote command:") < 0)
        return false;

    return true;
}

bool detectHubMode() {
    unsigned long start = millis();
    const unsigned long timeout = 10000; // listen for 2.5 seconds

    String buffer = "";

    while (millis() - start < timeout) {
        while (Serial2.available()) {
            char c = Serial2.read();
            buffer += c;

            // Heuristic: hub PWR frame always contains '@' and '$$'
            if (buffer.indexOf("@") >= 0 &&
                buffer.indexOf("$$") >= 0 &&
                buffer.length() > 200) {

                Log(LOG_INFO, "UART: Hub frame detected → HUB mode");
                return true;
            }
        }
        delay(5);
    }

    Log(LOG_INFO, "UART: No hub data received → STACK mode");
    return false;
}


// ---------------------------------------------------------
void PyUart::begin(int rx, int tx) {
    rxPin = rx;
    txPin = tx;

    Serial2.begin(115200, SERIAL_8N1, rxPin, txPin);
    delay(50);

    Log(LOG_INFO, "UART: begin() RX=" + String(rxPin) + " TX=" + String(txPin));

    // ---------------------------------------------------------
    // AUTO-DETECTION: HUB or STACK
    // ---------------------------------------------------------
    if (g_batteryMode == BatteryMode::UNKNOWN) {
        Log(LOG_INFO, "UART: Auto-detection started...");

        bool hub = detectHubMode();

        if (hub) {
            g_batteryMode = BatteryMode::HUB;
        } else {
            g_batteryMode = BatteryMode::STACK;
        }
    }

    // ---------------------------------------------------------
    // Continue with the matching mode
    // ---------------------------------------------------------
    if (g_batteryMode == BatteryMode::HUB) {
        Log(LOG_INFO, "UART: Starting in HUB mode (listen only)");
        commReady = true;      // wir brauchen kein wakeUpConsole()
        busy = false;
        frameReady = false;
        frameValid = false;
        return;                // IMPORTANT: skip stack initialization
    }

    // ---------------------------------------------------------
    // STACK-MODUS → normale Initialisierung
    // ---------------------------------------------------------
    wakeUpConsole();


    commReady     = false;
    busy          = false;
    frameReady    = false;
    frameValid    = false;
    lastCommand   = "";
    lastRawFrame  = "";
    g_invalidCount = 0;

    wakeUpConsole();
}

// ---------------------------------------------------------
void PyUart::switchBaud(int newRate) {
    Log(LOG_DEBUG, "UART: switchBaud(" + String(newRate) + ")");
    Serial2.flush();
    delay(20);
    Serial2.end();
    delay(20);
    Serial2.begin(newRate, SERIAL_8N1, rxPin, txPin);
    delay(20);
}

// ---------------------------------------------------------
void PyUart::wakeUpConsole() {
    Log(LOG_INFO, "UART: wakeUpConsole()");

    commReady = false;

    switchBaud(1200);
    Serial2.write("~20014682C0048520FCC3\r");
    delay(1000);

    byte nl[] = {0x0E, 0x0A};
    switchBaud(115200);

    for (int i = 0; i < 10; i++) {
        Serial2.write(nl, 2);
        delay(1000);

        if (Serial2.available()) {
            while (Serial2.available()) Serial2.read();
            break;
        }
    }

    commReady = true;
    g_invalidCount = 0;

    Log(LOG_INFO, "UART: wakeUpConsole complete → commReady=true");
}

// ---------------------------------------------------------
int PyUart::readFromSerial() {
    memset(g_szRecvBuff, 0, sizeof(g_szRecvBuff));
    int recvLen = 0;

    // Wait for first byte
    for (int i = 0; i < 150 && !Serial2.available(); ++i)
        delay(10);

    if (!Serial2.available()) {
        Log(LOG_WARN, "UART: timeout waiting for response");
        return 0;
    }

    while (Serial2.available()) {
        char buf[256] = "";
        int r = Serial2.readBytesUntil('>', buf, sizeof(buf) - 1);

        if (r > 0) {
            if (r + recvLen + 1 >= (int)sizeof(g_szRecvBuff)) {
                Log(LOG_WARN, "UART: read overflow");
                break;
            }

            strcat(g_szRecvBuff, buf);
            recvLen += r;

            if (strstr(g_szRecvBuff, "$$\r\n\rpylon")) {
                strcat(g_szRecvBuff, ">");
                break;
            }

            if (strstr(g_szRecvBuff, "Press [Enter] to be continued"))
                Serial2.write("\r");

            for (int j = 0; j < 20 && !Serial2.available(); ++j)
                delay(10);
        } else break;
    }

    Log(LOG_DEBUG, "UART RX len=" + String(recvLen));
    return recvLen;
}

// ---------------------------------------------------------
int PyUart::readDatalistStream() {
    // Wait for the first byte (same 1.5 s budget as readFromSerial).
    for (int i = 0; i < 150 && !Serial2.available(); ++i) delay(10);
    if (!Serial2.available()) {
        Log(LOG_WARN, "UART: datalist timeout waiting for response");
        return 0;
    }

    int total = 0;
    bool terminated = false;
    char tail[48] = "";   // rolling window so the terminator/paging marker can straddle reads

    while (true) {
        char buf[256] = "";
        int r = Serial2.readBytesUntil('>', buf, sizeof(buf) - 1);
        if (r <= 0) break;                 // no more data: end of stream
        total += r;

        char window[320];
        snprintf(window, sizeof(window), "%s%s", tail, buf);

        bool isLast     = strstr(window, "$$\r\n\rpylon") != nullptr;
        bool hasContinue = strstr(window, "Press [Enter] to be continued") != nullptr;

        sdCard.onDatalistChunk(buf, r, false);
        if (isLast) {
            // readBytesUntil() strips the '>' delimiter; the exporter's verbatim
            // payload needs it back, flagged as the last chunk.
            sdCard.onDatalistChunk(">", 1, true);
            terminated = true;
            break;
        }

        // Keep the last ~40 chars for cross-read detection.
        size_t wl = strlen(window);
        size_t keep = wl < 40 ? wl : 40;
        memcpy(tail, window + wl - keep, keep);
        tail[keep] = '\0';

        if (hasContinue) {
            Serial2.write("\r");           // continue to the next page
            for (int j = 0; j < 300 && !Serial2.available(); ++j) delay(10);
        }
    }

    if (total > 0 && !terminated) {
        // Stream ended without the '$$' terminator: complete the export instead
        // of leaving it running until the timeout.
        Log(LOG_WARN, "UART: datalist stream ended without terminator");
        sdCard.onDatalistStreamEnd();
    }
    return total;
}

// ---------------------------------------------------------
bool PyUart::sendCommandAndReadSerialResponse(const char* cmd) {
    if (cmd && cmd[0]) {
        Log(LOG_DEBUG, "UART TX: '" + String(cmd) + "'");
        Serial2.write(cmd);
    }

    Serial2.write("\n");
    Serial2.flush();

    int len = lastCommand.startsWith("datalist") ? readDatalistStream() : readFromSerial();
    return (len > 0);
}

// ---------------------------------------------------------
// Breaks a latched `busy` after an allocation failure. sendCommandImpl() sets
// busy=true before it starts allocating, so any throw between there and its
// return would otherwise leave Task 1 spinning forever on isBusy() while the
// scheduler stalls. Call this from every catch path around sendCommandImpl().
void PyUart::abortFrame() {
    frameReady = false;
    frameValid = false;
    busy = false;
}

// ---------------------------------------------------------
// Unguarded command body. All allocation happens here; sendCommand() is the
// exception boundary that keeps a bad_alloc from escaping into Task 1.
bool PyUart::sendCommandImpl(const char* cmd) {

    if (!commReady) {
        Log(LOG_WARN, "UART: commReady=false → wakeUpConsole()");
        wakeUpConsole();
        if (!commReady) {
            Log(LOG_ERROR, "UART: wakeUpConsole failed");
            return false;
        }
    }

    while (Serial2.available()) Serial2.read();
    delay(10);

    lastCommand = String(cmd);

    busy       = true;
    frameReady = false;
    frameValid = false;

    if (!sendCommandAndReadSerialResponse(cmd)) {
        busy = false;
        g_invalidCount++;

        Log(LOG_WARN, "UART: no response, invalidCount=" + String(g_invalidCount));

        if (g_invalidCount > 3) {
            commReady = false;
            Log(LOG_ERROR, "UART: too many failures → commReady=false");
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return false;
    }

    // A streamed datalist dump was forwarded to the SD exporter chunk by chunk;
    // it is not a PWR/BAT frame, so do not buffer/validate it.
    if (lastCommand.startsWith("datalist")) {
        g_invalidCount = 0;
        busy = false;
        frameReady = false;
        frameValid = false;
        return true;
    }

    lastRawFrame = String(g_szRecvBuff);
    frameReady   = true;
    frameValid   = isValidFrame(lastRawFrame);

    if (!frameValid) {
        g_invalidCount++;
        Log(LOG_WARN, "UART: invalid frame received");

        if (g_invalidCount > 3) {
            commReady = false;
            Log(LOG_ERROR, "UART: too many invalid frames → commReady=false");
        }

        busy = false;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return false;
    }

    g_invalidCount = 0;

    Log(LOG_INFO, "UART: valid frame received (" + String(lastRawFrame.length()) + " bytes)");

    // No per-type raw-frame copies here. lastPwrFrame/lastBatFrame/lastStatFrame
    // were written on every valid frame and never read by anything (the Frame
    // Dump API only uses getLastRawFrame()), so they cost a full ~2 KB String
    // allocation per frame plus ~6 KB retained for the lifetime of the process.
    // lastRawFrame already holds the frame and is what the parsers consume.

    // ---------------------------------------------------------
    // PARSER DIRECT CALL (NEW ARCHITECTURE)
    // ---------------------------------------------------------
    // Intentionally not wrapped in its own try/catch: sendCommand() is the single
    // exception boundary for this whole body, so one diagnostic path covers every
    // allocation in the command path.
    if (frameValid) {

        // Bound by reference, not copied: every parser takes `const String&`, and
        // a by-value String here duplicated the whole ~2 KB frame on a heap whose
        // largest 8-bit block was measured at ~2.1 KB — i.e. right at the edge
        // where a single extra copy decides success or bad_alloc.
        const String &raw = lastRawFrame;

        // -----------------------------
        // PWR PARSER
        // -----------------------------
        if (lastCommand == "pwr") {
            BatteryStack stack;
            std::vector<BatteryModule> mods;

            ParseResult r = parsePwrFrame(raw, stack, mods);

            if (r == PARSE_RANGE_INVALID) {
                // Whole frame dropped for an out-of-range voltage. Log once per
                // streak (== threshold, not >=) so a long run does not spam.
                if (++g_pwrVoltStreak == VOLTAGE_STREAK_ALERT) {
                    Log(LOG_ERROR, "PWR: " + String(VOLTAGE_STREAK_ALERT) +
                                   " consecutive frames rejected (voltage out of range)");
                }
            }
            else if (r == PARSE_OK) {
                g_pwrVoltStreak = 0;
                // parsePwrFrame() already published this frame into the double
                // buffer and bumped pwrFrameSeq. Publishing again here toggled
                // pwrUseA a second time per frame, which pinned the "front"
                // buffer to the one Task 1 keeps overwriting. Only raise the
                // one-shot flag here (MQTT owns clearing it).
                parserHasData = true;
            }
        }

        // -----------------------------
        // BAT PARSER
        // -----------------------------
        else if (lastCommand.startsWith("bat")) {
            int moduleIndex = lastCommand.substring(3).toInt();

            ParseResult r = parseBatFrame(moduleIndex, raw);

            if (r == PARSE_RANGE_INVALID) {
                // Per-module streak; guard the array index before use.
                if (moduleIndex >= 0 && moduleIndex <= MAX_MODULES) {
                    if (++g_batVoltStreak[moduleIndex] == VOLTAGE_STREAK_ALERT) {
                        Log(LOG_ERROR, "BAT: module " + String(moduleIndex) + ": " +
                                       String(VOLTAGE_STREAK_ALERT) +
                                       " consecutive frames rejected (voltage out of range)");
                    }
                }
            }
            else if (r == PARSE_OK) {
                // Published by parseBatFrame(); only raise the flag here.
                if (moduleIndex >= 0 && moduleIndex <= MAX_MODULES) {
                    g_batVoltStreak[moduleIndex] = 0;
                }
                batParserHasData = true;
            }
        }

        // -----------------------------
        // STAT PARSER
        // -----------------------------
        else if (lastCommand.startsWith("stat")) {
            StatData out;
            int moduleIndex = lastCommand.substring(4).toInt();

            ParseResult r = parseStatFrame(moduleIndex, raw, out);

            if (r == PARSE_OK) {
                // Published by parseStatFrame(); only raise the flag here.
                statParserHasData = true;
                statParserModuleIndex = moduleIndex;
            }
        }

        // Frame already processed → do not parse again
        frameReady = false;
    }


    busy = false;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return true;
}

// ---------------------------------------------------------
// Exception boundary for the whole command path. A std::bad_alloc raised by any
// String copy in sendCommandImpl() would otherwise unwind past this point into
// realtimeTask() and hit abort(). abortFrame() is mandatory, not cosmetic:
// sendCommandImpl() latches busy=true early, so without it Task 1 would spin
// forever on isBusy() and the scheduler would stall permanently.
//
// Diagnostics use Serial.printf, never Log(): Log() concatenates Strings and
// would throw again inside the handler. g_invalidCount is deliberately left
// alone — this is a memory fault, not a comms fault, and counting it would
// cascade into commReady=false and a costly console re-wake loop.
bool PyUart::sendCommand(const char* cmd) {
    try {
        return sendCommandImpl(cmd);
    } catch (const std::bad_alloc &) {
        Serial.printf("[DIAG] bad_alloc sendCommand: cmd='%s' free=%u max=%u largest8=%u intact=%d\n",
                      cmd ? cmd : "", (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMaxAllocHeap(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                      (int)heap_caps_check_integrity_all(true));
        abortFrame();
        return false;
    } catch (const std::exception &e) {
        Serial.printf("[DIAG] exception sendCommand: cmd='%s' what=%s free=%u max=%u\n",
                      cmd ? cmd : "", e.what(),
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        abortFrame();
        return false;
    }
}

// ---------------------------------------------------------
String PyUart::getFrame() {
    frameReady = false;
    return lastRawFrame;
}

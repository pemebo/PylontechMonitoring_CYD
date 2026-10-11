#pragma once
#include <Arduino.h>

// Battery UART pins (CYD, CN1 connector): TX=GPIO27, RX=GPIO22
// Loopback-validated 2026-09-20 (GPIO22<->GPIO27 echoes 36/36 bytes at 115200,
// both directions). Replaces the old P3 wiring TX=22/RX=35.
#define BAT_RX_PIN 22
#define BAT_TX_PIN 27

class PyUart {
public:
    void begin(int rx, int tx);

    // Blocking command → fills lastRawFrame.
    // Thin exception boundary around sendCommandImpl().
    bool sendCommand(const char* cmd);

    // Frame access for realtimeTask()
    bool hasFrame() const { return frameReady; }
    bool isFrameValid() const { return frameValid; }
    String getFrame();   // returns lastRawFrame and clears frameReady

    // Status
    bool isReady() const { return commReady; }
    bool isBusy() const { return busy; }
    String getLastCommand() const { return lastCommand; }

    // Clears the in-flight UART state after an allocation failure so Task 1 can
    // never stall on a latched `busy` flag.
    void abortFrame();

    // Last frame for web UI (the Frame Dump API). Kept as a single buffer: a
    // per-type copy per frame cost a ~2 KB allocation each and was never read.
    String getLastRawFrame() const { return lastRawFrame; }

private:
    void switchBaud(int newRate);
    void wakeUpConsole();
    int  readFromSerial();
    int  readDatalistStream();
    bool sendCommandAndReadSerialResponse(const char* cmd);

    // Unguarded body of sendCommand(). Wrapped by sendCommand().
    bool sendCommandImpl(const char* cmd);

    bool commReady = false;
    bool busy = false;
    bool frameReady = false;
    bool frameValid = false;

    int rxPin = -1;
    int txPin = -1;

    String lastCommand;
    String lastRawFrame;
};

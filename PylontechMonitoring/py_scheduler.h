#pragma once
#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "py_uart.h"
#include "config.h"
#include "py_parser_pwr.h"   // for lastParsedStack + currentPwrModules

class PyScheduler {
public:
    void begin(PyUart* u);
    void loop();

    void enqueue(const String& cmd);
    bool tryEnqueueBatBatch(const std::vector<String>& commands);

    // Datalist command for the history export (high priority)
    void enqueueDatalistHistory(int module);

    bool   hasQueuedCommand() const;
    bool   isBatLoading() const;
    String popNextCommand();
    void   markCommandFinished();

    unsigned long lastCommandFinished = 0;

private:
    struct QueuedCommand {
        String command;
        bool isBat = false;
    };

    void lockQueue() const;
    void unlockQueue() const;

    PyUart* uart = nullptr;
    SemaphoreHandle_t queueMutex = nullptr;

    unsigned long bootTime = 0;

    unsigned long lastPwr  = 0;
    unsigned long lastBat  = 0;
    unsigned long lastStat = 0;

    bool initialPwrDone  = false;
    bool initialBatDone  = false;
    bool initialStatDone = false;
    bool initialDiscoveryDone = false;

    std::vector<QueuedCommand> queue;
    bool commandInFlight = false;
    bool commandInFlightIsBat = false;
    size_t batCommandsPending = 0;
};

extern PyScheduler py_scheduler;

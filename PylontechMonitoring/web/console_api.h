#pragma once
#include "api_core.h"
#include "auth.h"
#include "../py_uart.h"
#include "../py_scheduler.h"

extern PyUart py_uart;
extern PyScheduler py_scheduler;

inline void registerConsoleAPI() {

    apiGetAuth("/req", []() {
        String command = server.arg("code");
        String validationCommand = command;
        validationCommand.trim();
        if (validationCommand.length() == 0) {
            return apiError(400, F("Command cannot be empty"));
        }

        py_scheduler.enqueue(command);
        apiText("OK");
    });

    apiGetAuth("/api/lastframe", []() {
        unsigned long start = millis();
        while (!py_uart.hasFrame()) {
            if (millis() - start > 2000)
                return apiText("TIMEOUT");
            vTaskDelay(1);
        }
        apiText(py_uart.getFrame());
    });
}

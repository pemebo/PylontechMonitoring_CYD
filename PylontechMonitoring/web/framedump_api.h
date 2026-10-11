#pragma once
#include "api_core.h"
#include "auth.h"
#include "../py_uart.h"

extern PyUart py_uart;

inline void registerFramedumpApi() {
    apiGetAuth("/api/framedump", []() {
        apiText(py_uart.getLastRawFrame());
    });
}

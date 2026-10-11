#pragma once
#include <Arduino.h>

enum LogLevel {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_DEBUG
};

void Log(LogLevel lvl, const String& msg);

void WebLogInit();
void WebLog(const char* msg);
const char* WebLogCStr();
void WebLogWriteLock();
void WebLogWriteUnlock();

#pragma once
#include "api_core.h"
#include "../config.h"

#define BASIC_AUTH_USER   "admin"
#define BASIC_AUTH_REALM  "Pylontech"

extern AppConfig config;

inline bool authOk() {
    return server.authenticate(BASIC_AUTH_USER, config.configPassword.c_str());
}

inline void authFail() {
    server.requestAuthentication(BASIC_AUTH, BASIC_AUTH_REALM);
}

template<typename FN>
inline void apiGetAuth(const char *path, FN fn) {
    server.on(path, HTTP_GET, [fn]() {
        if (!authOk()) return authFail();
        fn();
    });
}

template<typename FN>
inline void apiPostAuth(const char *path, FN fn) {
    server.on(path, HTTP_POST, [fn]() {
        if (!authOk()) return authFail();
        fn();
    });
}
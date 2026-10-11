#include "wp_webserver.h"
#include "wp_routes.h"


WebServer server(80);

void WebServerModule_begin() {
    registerRoutes();
    server.begin();
}

void WebServerModule_handle() {
    server.handleClient();
}
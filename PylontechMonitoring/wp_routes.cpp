#include "wp_routes.h"
#include "wp_webserver.h"

#include "web/api_core.h"
#include "web/auth.h"
#include "web/api_combined.h"
#include "web/console_api.h"
#include "web/wp_connect_api.h"
#include "web/pwr_api.h"
#include "web/bat_api.h"
#include "web/stat_api.h"
#include "web/wp_health_api.h"
#include "web/modules_api.h"
#include "web/statistics_api.h"
#include "web/cells_api.h"
#include "web/screen_api.h"
#include "web/service_api.h"
#include "web/framedump_api.h"
#include "web/dataexport_api.h"

void registerRoutes() {

    registerServiceAPI(server);
    registerCombinedAPI();
    registerConsoleAPI();
    registerPwrAPI();
    registerBatAPI();
    registerStatAPI();
    registerHealthAPI();
    registerModulesAPI();
    registerStatisticsAPI();
    registerCellsAPI();
    registerFramedumpApi();
    registerScreenAPI();
    registerDataExportAPI();

    apiGetAuth("/api/wifi", apiWifiGet);
    apiPostAuth("/api/wifi", apiWifiPost);
    apiPostAuth("/api/wifi/enabled", apiWifiSetEnabled);
    apiGetAuth("/api/wifi/scan", apiWifiScan);

    apiGetAuth("/api/mqtt", apiMqttGet);
    apiPostAuth("/api/mqtt", apiMqttPost);

    apiGetAuth("/api/time", apiTimeGet);
    apiPostAuth("/api/time", apiTimePost);

    apiGetAuth("/api/network", apiNetworkGet);
    apiPostAuth("/api/network", apiNetworkPost);

    server.on("/api/password", HTTP_POST, []() {
        if (!authOk()) return authFail();
        apiPasswordPost();
    });

    apiGetAuth("/api/autoreboot", apiAutorebootGet);
    apiPostAuth("/api/autoreboot", apiAutorebootPost);

    // UI language. GET is public (the SPA shell at "/" is public and needs the
    // language to render before the user authenticates); POST requires auth.
    server.on("/api/language", HTTP_GET, apiLanguageGet);
    apiPostAuth("/api/language", apiLanguagePost);

    // Root: serve the SPA shell directly (registered before serveStatic for
    // priority). It must NOT depend on a client-side redirect: over the AP
    // (no internet) the browser may not run index.html's JS redirect, which
    // left "/" blank until /layout.html was entered manually.
    server.on("/", HTTP_GET, []() {
        File f = SPIFFS.open("/layout.html");
        if (f) {
            // Read the shell into memory so the server-injected language
            // placeholder can be replaced before the browser parses it.
            String html = f.readString();
            f.close();
            html.replace("__LANG__", config.language);
            server.send(200, "text/html", html);
            return;
        }
        server.send(404, "text/plain", "Not found: /layout.html");
    });

    // Password-protected pages (registered BEFORE serveStatic)
    auto protectedPage = [](const char* path) {
        server.on(path, HTTP_GET, [path]() {
            if (!authOk()) return authFail();
            File f = SPIFFS.open(path);
            if (f) {
                server.streamFile(f, "text/html", 200);
                f.close();
                return;
            }
            server.send(404, "text/plain", "Not found");
        });
    };

    protectedPage("/pages_basevalue.html");
    protectedPage("/pages_celldata.html");
    protectedPage("/pages_statistic.html");
    protectedPage("/pages_console.html");
    protectedPage("/pages_connection.html");
    protectedPage("/pages_screen.html");
    protectedPage("/pages_dataexport.html");
    protectedPage("/pages_sdcard.html");

    // Serve static files from SPIFFS (AFTER the specific routes)
    server.serveStatic("/", SPIFFS, "/");

    // Direct route for the configuration page
    server.on("/config", HTTP_GET, []() {
        server.sendHeader("Content-Type", "text/html");
        server.send(200, "text/html", String("Configuración WiFi (use /layout.html)"));
    });
}

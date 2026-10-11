#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "py_parser_pwr.h"


// Forward declarations
struct BatteryModule;
struct BatteryStack;
struct BatData;
struct StatField;
struct StatData;

struct MqttMessage {
    char topic[128];
    char payload[64];
};

class PyMqtt {
public:
    // Core lifecycle
    void begin();
    void loop();

    // Raw publish (Task1 → Task2)
    bool publishRaw(const String& topic, const String& payload);

    // Publish parsed data
    void publishStack(const BatteryStack& stack);
    void publishBat(int index, const BatteryModule& mod);
    void publishDiscoveryBatCell(int moduleIndex, int cellIndex);
    void publishBatCells(int moduleIndex, const std::vector<BatData>& batCells, const std::vector<String>& header);
    void publishStat(int moduleIndex, const StatData& stat);

    bool isDiscoveryActive() const { return discoveryActive; }

    bool isConnected() { return mqttClient.connected(); }
    void publishDiscoveryStatModule(int moduleIndex);
    bool publishDirect(const String& topic, const String& payload);



private:
    WiFiClient   wifiClient;
    PubSubClient mqttClient = PubSubClient(wifiClient);

    bool enabled = false;
    bool discoveryActive = false;

    int precisionForUnit(const String& unit);
    bool precisionDiffersFromDefault(const String& unit);

    // MQTT connection
    bool connect();

    // Value conversion (numeric, text, date)
    String computeValue(const String& raw, const FieldConfig& fc);

    // Name normalization (CamelCase)
    String normalizeName(const String& in);

    // Decimal precision based on unit
    int decimalsForUnit(const String& unit);

    // Device class based on unit
    String deviceClassForUnit(const String& unit);

    // Add HA metadata (unit, device_class, precision)
    void addDiscoveryMeta(JsonDocument& doc, const FieldConfig& fc);

    // Discovery publishers
    void publishDiscoveryStack();
    void publishDiscoveryPwrModule(int moduleIndex);

    // Discovery state machine
    void handleDiscoveryStep(
        const PwrBuffer& pwr,
        const BatBuffer& bat,
        const StatBuffer& stat
    );

    // Logging helper
    void logPublishFailure(const String& topic);
};
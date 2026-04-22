#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

namespace {
constexpr uint32_t kBridgeBaud = 115200;
constexpr size_t kMaxBridgeLine = 1024;
constexpr const char* kPrefsNamespace = "bambu-bridge";
constexpr const char* kApiBase = "https://api.bambulab.com";
constexpr const char* kPythonUserAgent = "python-requests/2.31.0";
constexpr uint16_t kMqttPort = 8883;
constexpr uint32_t kProbeConnectTimeoutMs = 250;
constexpr uint32_t kMqttTimeoutMs = 8000;
constexpr uint32_t kMqttPushallIntervalMs = 250;
constexpr uint32_t kMqttCollectWindowMs = 1800;
constexpr uint32_t kMqttQuietWindowMs = 500;
constexpr uint32_t kCacheRefreshIntervalMs = 4000;
constexpr size_t kMaxCachedPrinters = 8;
constexpr uint8_t kResolveAuthAttempts = 2;
constexpr uint8_t kStatusAttempts = 2;
constexpr size_t kMqttBufferSize = 16384;
constexpr size_t kPacketJsonCapacity = 16384;
constexpr size_t kMergedJsonCapacity = 24576;

struct CachedPrinterState {
  bool used = false;
  bool hasStatus = false;
  String serial;
  String ip;
  String accessCode;
  String name;
  String model;
  String cloudStatus;
  String state;
  String wifiSignal;
  String gcodeFile;
  bool hasProgress = false;
  bool hasLayer = false;
  bool hasTotalLayers = false;
  bool hasRemainingMinutes = false;
  bool hasSpeed = false;
  bool hasFan = false;
  bool hasFanAux1 = false;
  bool hasFanAux2 = false;
  bool hasNozzleTemp = false;
  bool hasBedTemp = false;
  bool online = false;
  uint8_t progress = 0;
  uint16_t layer = 0;
  uint16_t totalLayers = 0;
  uint16_t remainingMinutes = 0;
  uint16_t speed = 0;
  uint16_t fan = 0;
  uint16_t fanAux1 = 0;
  uint16_t fanAux2 = 0;
  float nozzleTemp = 0.0f;
  float bedTemp = 0.0f;
  uint32_t lastRefreshMs = 0;
  uint32_t lastAttemptMs = 0;
};

Preferences prefs;
String bridgeLine;
WiFiClientSecure mqttNetClient;
PubSubClient mqttClient(mqttNetClient);
String mqttExpectedTopic;
String mqttLastPayload;
bool mqttMessageReceived = false;
CachedPrinterState cachedPrinters[kMaxCachedPrinters];
size_t nextRefreshIndex = 0;

void bridgeReply(const String& line);
bool ensureWifiConnected();
void clearCachedPrinters();
bool probeMqttPort(const IPAddress& ip);
bool mqttAuthSucceeds(const String& serial, const IPAddress& ip, const String& accessCode);
String resolvePrinterIp(const String& serial, const String& accessCode);
void emitProgress(size_t current, size_t total, const String& label);
bool warmPrinterStatus(CachedPrinterState* entry, String* errorOut);
bool collectPrinterStatusWithRetry(
    const String& serial,
    const String& ip,
    const String& accessCode,
    CachedPrinterState* cacheEntry,
    String* errorOut);

String getSavedToken() {
  return prefs.getString("token", "");
}

String getSavedSsid() {
  return prefs.getString("ssid", "");
}

String getSavedPassword() {
  return prefs.getString("pass", "");
}

String sanitizeForBridge(String value, size_t maxLen = 180) {
  value.replace('\r', ' ');
  value.replace('\n', ' ');
  value.replace('|', '/');
  value.trim();
  if (value.length() > maxLen) {
    value = value.substring(0, maxLen);
  }
  return value;
}

bool jsonHasValue(const JsonObjectConst& object, const char* key) {
  return object.containsKey(key) && !object[key].isNull();
}

String bridgeValueOrUnknown(const String& value) {
  return value.length() > 0 ? value : String("?");
}

String bridgeNumberOrUnknown(bool known, unsigned long value) {
  return known ? String(value) : String("?");
}

String bridgeFloatOrUnknown(bool known, float value) {
  return known ? String(value) : String("?");
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String topicStr = topic ? String(topic) : String();
  if (mqttExpectedTopic.length() > 0 && topicStr != mqttExpectedTopic) {
    return;
  }

  mqttLastPayload = "";
  mqttLastPayload.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) {
    mqttLastPayload += static_cast<char>(payload[i]);
  }
  mqttMessageReceived = true;
}

void publishPushAll(const String& requestTopic) {
  mqttClient.publish(
      requestTopic.c_str(),
      "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}");
}

bool mergePrintPayload(DynamicJsonDocument& mergedDoc, const String& payload) {
  DynamicJsonDocument packetDoc(kPacketJsonCapacity);
  JsonObject mergedPrint;
  JsonObject packetPrint;
  DeserializationError jsonError = deserializeJson(packetDoc, payload);

  if (jsonError) {
    return false;
  }

  if (!packetDoc["print"].is<JsonObject>()) {
    return false;
  }

  if (!mergedDoc.is<JsonObject>()) {
    mergedDoc.to<JsonObject>();
  }

  mergedPrint = mergedDoc.as<JsonObject>();
  packetPrint = packetDoc["print"].as<JsonObject>();
  for (JsonPair kv : packetPrint) {
    mergedPrint[kv.key()] = kv.value();
  }

  return true;
}

bool hasUsefulPrintData(const JsonObjectConst& print) {
  return print.containsKey("gcode_state") ||
         print.containsKey("mc_percent") ||
         print.containsKey("nozzle_temper") ||
         print.containsKey("bed_temper") ||
         print.containsKey("wifi_signal") ||
         print.containsKey("gcode_file") ||
         print.containsKey("cooling_fan_speed") ||
         print.containsKey("big_fan1_speed") ||
         print.containsKey("big_fan2_speed");
}

CachedPrinterState* findCachedPrinter(const String& serial) {
  for (size_t i = 0; i < kMaxCachedPrinters; ++i) {
    if (cachedPrinters[i].used && cachedPrinters[i].serial == serial) {
      return &cachedPrinters[i];
    }
  }

  return nullptr;
}

CachedPrinterState* ensureCachedPrinter(
    const String& serial,
    const String& ip,
    const String& accessCode) {
  CachedPrinterState* entry = findCachedPrinter(serial);

  if (!entry) {
    for (size_t i = 0; i < kMaxCachedPrinters; ++i) {
      if (!cachedPrinters[i].used) {
        entry = &cachedPrinters[i];
        *entry = CachedPrinterState();
        entry->used = true;
        entry->serial = serial;
        break;
      }
    }
  }

  if (!entry) {
    entry = &cachedPrinters[nextRefreshIndex % kMaxCachedPrinters];
    *entry = CachedPrinterState();
    entry->used = true;
    entry->serial = serial;
    nextRefreshIndex = (nextRefreshIndex + 1U) % kMaxCachedPrinters;
  }

  if (!ip.isEmpty()) {
    entry->ip = ip;
  }
  if (!accessCode.isEmpty()) {
    entry->accessCode = accessCode;
  }

  return entry;
}

void updateCachedPrinterFromPrint(CachedPrinterState& entry, const JsonObjectConst& print) {
  if (jsonHasValue(print, "gcode_state")) {
    entry.state = sanitizeForBridge(String(print["gcode_state"] | ""), 40);
  }
  if (jsonHasValue(print, "wifi_signal")) {
    entry.wifiSignal = sanitizeForBridge(String(print["wifi_signal"] | ""), 24);
  }
  if (jsonHasValue(print, "gcode_file")) {
    entry.gcodeFile = sanitizeForBridge(String(print["gcode_file"] | ""), 80);
  }
  if (jsonHasValue(print, "mc_percent")) {
    entry.progress = static_cast<uint8_t>(print["mc_percent"] | 0);
    entry.hasProgress = true;
  }
  if (jsonHasValue(print, "layer_num")) {
    entry.layer = static_cast<uint16_t>(print["layer_num"] | 0);
    entry.hasLayer = true;
  }
  if (jsonHasValue(print, "total_layer_num")) {
    entry.totalLayers = static_cast<uint16_t>(print["total_layer_num"] | 0);
    entry.hasTotalLayers = true;
  }
  if (jsonHasValue(print, "mc_remaining_time")) {
    entry.remainingMinutes = static_cast<uint16_t>(print["mc_remaining_time"] | 0);
    entry.hasRemainingMinutes = true;
  }
  if (jsonHasValue(print, "spd_lvl")) {
    entry.speed = static_cast<uint16_t>(print["spd_lvl"] | 0);
    entry.hasSpeed = true;
  }
  if (jsonHasValue(print, "cooling_fan_speed")) {
    entry.fan = static_cast<uint16_t>(print["cooling_fan_speed"] | 0);
    entry.hasFan = true;
  }
  if (jsonHasValue(print, "big_fan1_speed")) {
    entry.fanAux1 = static_cast<uint16_t>(print["big_fan1_speed"] | 0);
    entry.hasFanAux1 = true;
  }
  if (jsonHasValue(print, "big_fan2_speed")) {
    entry.fanAux2 = static_cast<uint16_t>(print["big_fan2_speed"] | 0);
    entry.hasFanAux2 = true;
  }
  if (jsonHasValue(print, "nozzle_temper")) {
    entry.nozzleTemp = static_cast<float>(print["nozzle_temper"] | 0);
    entry.hasNozzleTemp = true;
  }
  if (jsonHasValue(print, "bed_temper")) {
    entry.bedTemp = static_cast<float>(print["bed_temper"] | 0);
    entry.hasBedTemp = true;
  }
  entry.hasStatus = true;
  entry.lastRefreshMs = millis();
}

void clearCachedPrinters() {
  for (size_t i = 0; i < kMaxCachedPrinters; ++i) {
    cachedPrinters[i] = CachedPrinterState();
  }
  nextRefreshIndex = 0;
}

void emitProgress(size_t current, size_t total, const String& label) {
  String line = "PROGRESS|";
  line += String(current);
  line += "|";
  line += String(total);
  line += "|";
  line += sanitizeForBridge(label, 63);
  bridgeReply(line);
}

void emitCachedStatus(const CachedPrinterState& entry) {
  String line = "STATUS|";
  line += entry.serial;
  line += "|";
  line += bridgeValueOrUnknown(sanitizeForBridge(entry.state, 40));
  line += "|";
  line += bridgeNumberOrUnknown(entry.hasProgress, entry.progress);
  line += "|";
  line += bridgeNumberOrUnknown(entry.hasLayer, entry.layer);
  line += "|";
  line += bridgeNumberOrUnknown(entry.hasTotalLayers, entry.totalLayers);
  line += "|";
  line += bridgeNumberOrUnknown(entry.hasRemainingMinutes, entry.remainingMinutes);
  line += "|";
  line += bridgeFloatOrUnknown(entry.hasNozzleTemp, entry.nozzleTemp);
  line += "|";
  line += bridgeFloatOrUnknown(entry.hasBedTemp, entry.bedTemp);
  line += "|";
  line += bridgeValueOrUnknown(sanitizeForBridge(entry.wifiSignal, 24));
  line += "|";
  line += bridgeValueOrUnknown(sanitizeForBridge(entry.gcodeFile, 80));
  line += "|";
  line += bridgeNumberOrUnknown(entry.hasSpeed, entry.speed);
  line += "|";
  line += bridgeNumberOrUnknown(entry.hasFan, entry.fan);
  line += "|";
  line += bridgeNumberOrUnknown(entry.hasFanAux1, entry.fanAux1);
  line += "|";
  line += bridgeNumberOrUnknown(entry.hasFanAux2, entry.fanAux2);
  bridgeReply(line);
}

bool collectPrinterStatus(
    const String& serial,
    const String& ip,
    const String& accessCode,
    CachedPrinterState* cacheEntry,
    String* errorOut) {
  DynamicJsonDocument mergedDoc(kMergedJsonCapacity);
  String requestTopic;
  uint32_t start = 0;
  uint32_t lastPushAll = 0;
  uint32_t firstMessageAt = 0;
  uint32_t lastMessageAt = 0;
  bool receivedAnyPrint = false;
  bool sawPayloadButParseFailed = false;

  if (serial.isEmpty() || ip.isEmpty() || accessCode.isEmpty()) {
    if (errorOut) {
      *errorOut = "Missing serial, ip, or access code";
    }
    return false;
  }

  if (!ensureWifiConnected()) {
    if (errorOut) {
      *errorOut = "WiFi not connected";
    }
    return false;
  }

  mqttNetClient.stop();
  mqttNetClient.setInsecure();
  mqttClient.setServer(ip.c_str(), kMqttPort);
  mqttClient.setCallback(onMqttMessage);
  mqttClient.setBufferSize(kMqttBufferSize);
  mqttClient.setKeepAlive(15);
  mqttClient.setSocketTimeout(4);

  mqttExpectedTopic = "device/" + serial + "/report";
  requestTopic = "device/" + serial + "/request";
  mqttLastPayload = "";
  mqttMessageReceived = false;

  if (!mqttClient.connect(serial.c_str(), "bblp", accessCode.c_str())) {
    if (errorOut) {
      *errorOut = "MQTT connect failed " + String(mqttClient.state());
    }
    return false;
  }

  if (!mqttClient.subscribe(mqttExpectedTopic.c_str())) {
    mqttClient.disconnect();
    if (errorOut) {
      *errorOut = "MQTT subscribe failed";
    }
    return false;
  }

  start = millis();
  while (millis() - start < 150) {
    mqttClient.loop();
    delay(5);
  }

  start = millis();
  lastPushAll = start - kMqttPushallIntervalMs;
  while (millis() - start < kMqttTimeoutMs) {
    uint32_t now = millis();

    if (now - lastPushAll >= kMqttPushallIntervalMs) {
      publishPushAll(requestTopic);
      lastPushAll = now;
    }

    mqttClient.loop();

    if (mqttMessageReceived) {
      mqttMessageReceived = false;
      if (mergePrintPayload(mergedDoc, mqttLastPayload)) {
        if (!receivedAnyPrint) {
          firstMessageAt = now;
        }
        receivedAnyPrint = true;
        lastMessageAt = now;
      } else if (mqttLastPayload.length() > 0) {
        sawPayloadButParseFailed = true;
      }
    }

    if (receivedAnyPrint &&
        (now - firstMessageAt) >= kMqttCollectWindowMs &&
        (now - lastMessageAt) >= kMqttQuietWindowMs) {
      break;
    }

    delay(5);
  }

  mqttClient.disconnect();

  if (!receivedAnyPrint || !mergedDoc.is<JsonObject>()) {
    if (errorOut) {
      *errorOut = sawPayloadButParseFailed ? "MQTT JSON parse failed" : "MQTT timeout";
    }
    return false;
  }

  JsonObject print = mergedDoc.as<JsonObject>();
  if (!hasUsefulPrintData(print)) {
    if (errorOut) {
      *errorOut = "MQTT payload missing print data";
    }
    return false;
  }

  if (cacheEntry) {
    updateCachedPrinterFromPrint(*cacheEntry, print);
  }

  return true;
}

bool probeMqttPort(const IPAddress& ip) {
  WiFiClientSecure probeClient;

  probeClient.setInsecure();
  probeClient.setTimeout((kProbeConnectTimeoutMs + 999U) / 1000U);
  if (!probeClient.connect(ip, kMqttPort, static_cast<int>(kProbeConnectTimeoutMs))) {
    return false;
  }

  probeClient.stop();
  return true;
}

bool mqttAuthSucceeds(const String& serial, const IPAddress& ip, const String& accessCode) {
  WiFiClientSecure authNetClient;
  PubSubClient authClient(authNetClient);

  if (serial.isEmpty() || accessCode.isEmpty()) {
    return false;
  }

  authNetClient.setInsecure();
  authClient.setServer(ip, kMqttPort);
  authClient.setSocketTimeout(2);
  authClient.setKeepAlive(10);

  if (!authClient.connect(serial.c_str(), "bblp", accessCode.c_str())) {
    authClient.disconnect();
    return false;
  }

  authClient.disconnect();
  return true;
}

String resolvePrinterIp(const String& serial, const String& accessCode) {
  IPAddress localIp = WiFi.localIP();
  IPAddress candidate(localIp[0], localIp[1], localIp[2], 0);

  if ((localIp[0] == 0 && localIp[1] == 0 && localIp[2] == 0 && localIp[3] == 0) ||
      accessCode.isEmpty() || serial.isEmpty()) {
    return "";
  }

  for (uint16_t host = 1; host < 255; ++host) {
    bool matched = false;

    if (host == localIp[3]) {
      continue;
    }

    candidate[3] = static_cast<uint8_t>(host);
    if (!probeMqttPort(candidate)) {
      continue;
    }

    for (uint8_t attempt = 0; attempt < kResolveAuthAttempts; ++attempt) {
      if (mqttAuthSucceeds(serial, candidate, accessCode)) {
        matched = true;
        break;
      }
      delay(20);
    }

    if (matched) {
      return candidate.toString();
    }

    delay(1);
  }

  return "";
}

bool warmPrinterStatus(CachedPrinterState* entry, String* errorOut) {
  if (!entry || entry->serial.isEmpty() || entry->ip.isEmpty() || entry->accessCode.isEmpty()) {
    if (errorOut) {
      *errorOut = "Missing serial, ip, or access code";
    }
    return false;
  }

  entry->lastAttemptMs = millis();
  return collectPrinterStatusWithRetry(entry->serial, entry->ip, entry->accessCode, entry, errorOut);
}

bool collectPrinterStatusWithRetry(
    const String& serial,
    const String& ip,
    const String& accessCode,
    CachedPrinterState* cacheEntry,
    String* errorOut) {
  String lastError;

  for (uint8_t attempt = 0; attempt < kStatusAttempts; ++attempt) {
    if (collectPrinterStatus(serial, ip, accessCode, cacheEntry, &lastError)) {
      if (errorOut) {
        *errorOut = "";
      }
      return true;
    }

    delay(100);
  }

  if (errorOut) {
    *errorOut = lastError;
  }
  return false;
}

void serviceCachedPrinterRefresh() {
  uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  for (size_t offset = 0; offset < kMaxCachedPrinters; ++offset) {
    size_t index = (nextRefreshIndex + offset) % kMaxCachedPrinters;
    CachedPrinterState& entry = cachedPrinters[index];
    String error;

    if (!entry.used || entry.ip.isEmpty() || entry.accessCode.isEmpty()) {
      continue;
    }

    if (entry.lastRefreshMs != 0 && (now - entry.lastRefreshMs) < kCacheRefreshIntervalMs) {
      continue;
    }

    if (entry.lastAttemptMs != 0 && (now - entry.lastAttemptMs) < 1000U) {
      continue;
    }

    entry.lastAttemptMs = now;
    collectPrinterStatus(entry.serial, entry.ip, entry.accessCode, &entry, &error);
    nextRefreshIndex = (index + 1U) % kMaxCachedPrinters;
    break;
  }
}

bool connectToWifi(const String& ssid, const String& password, bool persistOnSuccess) {
  if (ssid.isEmpty()) {
    bridgeReply("ERR|SSID required");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), password.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  if (persistOnSuccess) {
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
  }

  return true;
}

void bridgeReply(const String& line) {
  Serial.print(line);
  Serial.print('\n');
}

String splitField(String& input, char delimiter) {
  int pos = input.indexOf(delimiter);
  if (pos < 0) {
    String value = input;
    input = "";
    return value;
  }

  String value = input.substring(0, pos);
  input = input.substring(pos + 1);
  return value;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

String decodeBridgeField(const String& input) {
  String decoded;
  decoded.reserve(input.length());

  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c == '%' && (i + 2) < input.length()) {
      int hi = hexValue(input[i + 1]);
      int lo = hexValue(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        decoded += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    decoded += c;
  }

  return decoded;
}

bool ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  String ssid = getSavedSsid();
  String password = getSavedPassword();
  if (ssid.isEmpty()) {
    bridgeReply("ERR|No WiFi credentials saved");
    return false;
  }

  if (!connectToWifi(ssid, password, false)) {
    bridgeReply("ERR|WiFi connect failed");
    return false;
  }

  return true;
}

bool apiGetJson(const String& path, DynamicJsonDocument& doc) {
  String token = getSavedToken();
  if (token.isEmpty()) {
    bridgeReply("ERR|No bearer token saved");
    return false;
  }

  if (!ensureWifiConnected()) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(kApiBase) + path;
  if (!http.begin(client, url)) {
    bridgeReply("ERR|HTTP begin failed");
    return false;
  }

  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("Content-Type", "application/json");
  http.setUserAgent(kPythonUserAgent);

  int status = http.GET();
  if (status < 200 || status >= 300) {
    String body = sanitizeForBridge(http.getString());
    String detail = "HTTP " + String(status);
    if (body.length() > 0) {
      detail += " ";
      detail += body;
    } else {
      detail += " ";
      detail += sanitizeForBridge(http.errorToString(status));
    }
    bridgeReply("ERR|" + detail);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    bridgeReply("ERR|JSON parse failed");
    return false;
  }

  return true;
}

void runWiFiScan() {
  int count = 0;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(50);

  count = WiFi.scanNetworks(false, true);
  if (count < 0) {
    bridgeReply("ERR|WiFi scan failed");
    return;
  }

  for (int i = 0; i < count; ++i) {
    String line = "WIFI|";
    line += WiFi.SSID(i);
    line += "|";
    line += String(WiFi.RSSI(i));
    line += "|";
    line += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secure";
    bridgeReply(line);
  }

  WiFi.scanDelete();
  bridgeReply("OK|WIFI_SCAN|" + String(count));
}

void runWiFiConnect(const String& ssid, const String& password) {
  if (!connectToWifi(ssid, password, true)) {
    bridgeReply("ERR|WiFi connect failed");
    return;
  }

  bridgeReply("OK|WIFI_CONNECT|" + WiFi.localIP().toString());
}

void runWiFiReconnect() {
  String ssid = getSavedSsid();
  String password = getSavedPassword();

  if (ssid.isEmpty()) {
    bridgeReply("ERR|No WiFi credentials saved");
    return;
  }

  if (!connectToWifi(ssid, password, false)) {
    bridgeReply("ERR|WiFi reconnect failed");
    return;
  }

  bridgeReply("OK|WIFI_RECONNECT|" + WiFi.localIP().toString());
}

void runWiFiStatus() {
  String line = "OK|WIFI_STATUS|";
  line += String(static_cast<int>(WiFi.status()));
  line += "|";
  line += WiFi.SSID();
  line += "|";
  line += WiFi.localIP().toString();
  bridgeReply(line);
}

void runSetToken(const String& token) {
  if (token.isEmpty()) {
    bridgeReply("ERR|Token required");
    return;
  }

  prefs.putString("token", token);
  bridgeReply("OK|TOKEN_SAVED|" + String(token.length()));
}

void runBambuDiscover() {
  DynamicJsonDocument doc(16384);

  if (!apiGetJson("/v1/iot-service/api/user/bind", doc)) {
    return;
  }

  clearCachedPrinters();

  JsonArray devices = doc["devices"].as<JsonArray>();
  size_t total = devices.size();
  size_t count = 0;
  emitProgress(0, total > 0 ? total : 1, total > 0 ? "Fetching printers" : "No printers found");
  for (JsonObject device : devices) {
    String serial = sanitizeForBridge(String(device["dev_id"] | ""), 31);
    String name = sanitizeForBridge(String(device["name"] | ""), 47);
    String model = sanitizeForBridge(String(device["dev_product_name"] | ""), 23);
    String accessCode = sanitizeForBridge(String(device["dev_access_code"] | ""), 31);
    bool online = device["online"] | false;
    String cloudStatus = sanitizeForBridge(String(device["print_status"] | ""), 63);
    String resolvedIp;
    CachedPrinterState* entry = nullptr;

    if (serial.isEmpty()) {
      continue;
    }

    emitProgress(count, total, name.length() > 0 ? name : serial);

    if (!accessCode.isEmpty()) {
      resolvedIp = resolvePrinterIp(serial, accessCode);
    }

    entry = ensureCachedPrinter(serial, resolvedIp, accessCode);
    if (entry) {
      entry->name = name;
      entry->model = model;
      entry->cloudStatus = cloudStatus;
      entry->online = online;
      if (!resolvedIp.isEmpty()) {
        String statusError;
        if (warmPrinterStatus(entry, &statusError) && entry->hasStatus) {
          emitCachedStatus(*entry);
        }
      }
    }
    String line = "PRINTER|";
    line += serial;
    line += "|";
    line += name;
    line += "|";
    line += model;
    line += "|";
    line += resolvedIp;
    line += "|";
    line += accessCode;
    line += "|";
    line += (online ? "1" : "0");
    line += "|";
    line += cloudStatus;
    bridgeReply(line);
    count++;
    emitProgress(count, total, name.length() > 0 ? name : serial);
  }

  bridgeReply("OK|BAMBU_DISCOVER|" + String(count));
}

void runBambuTestProfile() {
  DynamicJsonDocument doc(8192);

  if (!apiGetJson("/v1/user-service/my/profile", doc)) {
    return;
  }

  String name = doc["name"] | "";
  String uid = doc["uid"] | "";
  String account = doc["account"] | "";
  String detail = "PROFILE_TEST";

  if (name.length() > 0) {
    detail += "|";
    detail += sanitizeForBridge(name, 80);
  } else if (account.length() > 0) {
    detail += "|";
    detail += sanitizeForBridge(account, 80);
  } else if (uid.length() > 0) {
    detail += "|";
    detail += sanitizeForBridge(uid, 80);
  }

  bridgeReply("OK|" + detail);
}

void runBambuStatus(const String& serialFilter) {
  CachedPrinterState* entry = findCachedPrinter(serialFilter);

  if (!entry) {
    bridgeReply("ERR|Printer not registered");
    return;
  }

  if (entry->hasStatus) {
    emitCachedStatus(*entry);
    bridgeReply("OK|BAMBU_STATUS|1");
  } else {
    bridgeReply("OK|BAMBU_STATUS|0");
  }
}

void runBambuStatusLocal(const String& serial, const String& ip, const String& accessCode) {
  CachedPrinterState* entry = ensureCachedPrinter(serial, ip, accessCode);
  String error;
  bool success = false;

  if (!entry) {
    bridgeReply("ERR|Printer cache full");
    return;
  }

  if (serial.isEmpty() || ip.isEmpty() || accessCode.isEmpty()) {
    bridgeReply("ERR|Missing serial, ip, or access code");
    return;
  }

  entry->lastAttemptMs = millis();
  success = collectPrinterStatusWithRetry(serial, ip, accessCode, entry, &error);

  if (entry->hasStatus) {
      emitCachedStatus(*entry);
      bridgeReply("OK|BAMBU_STATUS|1");
  } else if(!success && error.length() > 0) {
      bridgeReply("ERR|" + sanitizeForBridge(error, 120));
  } else {
      bridgeReply("OK|BAMBU_STATUS|0");
  }
}

void handleCommand(String line) {
  line.trim();
  if (line.isEmpty()) {
    return;
  }

  if (line == "PING") {
    bridgeReply("OK|PONG");
    return;
  }

  if (line == "WIFI_SCAN") {
    runWiFiScan();
    return;
  }

  if (line == "WIFI_STATUS") {
    runWiFiStatus();
    return;
  }

  if (line == "WIFI_RECONNECT") {
    runWiFiReconnect();
    return;
  }

  if (line == "BAMBU_DISCOVER") {
    runBambuDiscover();
    return;
  }

  if (line == "BAMBU_TEST_PROFILE") {
    runBambuTestProfile();
    return;
  }

  String remainder = line;
  String command = splitField(remainder, '|');

  if (command == "WIFI_CONNECT") {
    String ssid = decodeBridgeField(splitField(remainder, '|'));
    String password = decodeBridgeField(remainder);
    runWiFiConnect(ssid, password);
    return;
  }

  if (command == "BAMBU_SET_TOKEN") {
    runSetToken(remainder);
    return;
  }

  if (command == "BAMBU_STATUS") {
    String serial = splitField(remainder, '|');
    String ip = splitField(remainder, '|');
    String accessCode = remainder;
    if (ip.isEmpty() || accessCode.isEmpty()) {
      runBambuStatus(serial);
    } else {
      runBambuStatusLocal(serial, ip, accessCode);
    }
    return;
  }

  bridgeReply("ERR|Unknown command");
}
}  // namespace

void setup() {
  Serial.begin(kBridgeBaud);
  delay(250);

  prefs.begin(kPrefsNamespace, false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  mqttNetClient.setInsecure();

  bridgeReply("OK|BOOT");
}

void loop() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      handleCommand(bridgeLine);
      bridgeLine = "";
      continue;
    }
    if (bridgeLine.length() < kMaxBridgeLine) {
      bridgeLine += c;
    }
  }

}

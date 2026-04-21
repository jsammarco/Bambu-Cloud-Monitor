#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>

namespace {
constexpr uint32_t kBridgeBaud = 115200;
constexpr size_t kMaxBridgeLine = 1024;
constexpr const char* kPrefsNamespace = "bambu-bridge";
constexpr const char* kApiBase = "https://api.bambulab.com";

Preferences prefs;
String bridgeLine;

String getSavedToken() {
  return prefs.getString("token", "");
}

String getSavedSsid() {
  return prefs.getString("ssid", "");
}

String getSavedPassword() {
  return prefs.getString("pass", "");
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

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), password.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
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

  int status = http.GET();
  if (status < 200 || status >= 300) {
    bridgeReply("ERR|HTTP " + String(status));
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
  if (ssid.isEmpty()) {
    bridgeReply("ERR|SSID required");
    return;
  }

  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), password.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    bridgeReply("ERR|WiFi connect failed");
    return;
  }

  bridgeReply("OK|WIFI_CONNECT|" + WiFi.localIP().toString());
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
  bridgeReply("OK|TOKEN_SAVED");
}

void runBambuDiscover() {
  DynamicJsonDocument doc(16384);

  if (!apiGetJson("/v1/iot-service/api/user/bind", doc)) {
    return;
  }

  JsonArray devices = doc["devices"].as<JsonArray>();
  size_t count = 0;
  for (JsonObject device : devices) {
    String serial = device["dev_id"] | "";
    String name = device["name"] | "";
    String model = device["dev_product_name"] | "";
    String accessCode = device["dev_access_code"] | "";
    bool online = device["online"] | false;
    String cloudStatus = device["print_status"] | "";
    String line = "PRINTER|";
    line += serial;
    line += "|";
    line += name;
    line += "|";
    line += model;
    line += "|";
    line += "";  // local IP placeholder until local MQTT/IP matching is added
    line += "|";
    line += accessCode;
    line += "|";
    line += (online ? "1" : "0");
    line += "|";
    line += cloudStatus;
    bridgeReply(line);
    count++;
  }

  bridgeReply("OK|BAMBU_DISCOVER|" + String(count));
}

void runBambuStatus(const String& serialFilter) {
  DynamicJsonDocument doc(32768);

  if (!apiGetJson("/v1/iot-service/api/user/print?force=true", doc)) {
    return;
  }

  JsonArray devices = doc["devices"].as<JsonArray>();
  size_t count = 0;
  for (JsonObject device : devices) {
    String serial = device["dev_id"] | "";
    if (!serialFilter.isEmpty() && serial != serialFilter) {
      continue;
    }

    JsonObject print = device["print"].as<JsonObject>();
    String state = print["gcode_state"] | (device["print_status"] | "");
    String wifiSignal = print["wifi_signal"] | "";
    String fileName = print["gcode_file"] | "";
    String line = "STATUS|";
    line += serial;
    line += "|";
    line += state;
    line += "|";
    line += print["mc_percent"] | 0;
    line += "|";
    line += print["layer_num"] | 0;
    line += "|";
    line += print["total_layer_num"] | 0;
    line += "|";
    line += print["mc_remaining_time"] | 0;
    line += "|";
    line += print["nozzle_temper"] | 0;
    line += "|";
    line += print["bed_temper"] | 0;
    line += "|";
    line += wifiSignal;
    line += "|";
    line += fileName;
    bridgeReply(line);
    count++;
  }

  bridgeReply("OK|BAMBU_STATUS|" + String(count));
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

  if (line == "BAMBU_DISCOVER") {
    runBambuDiscover();
    return;
  }

  String remainder = line;
  String command = splitField(remainder, '|');

  if (command == "WIFI_CONNECT") {
    String ssid = splitField(remainder, '|');
    String password = remainder;
    runWiFiConnect(ssid, password);
    return;
  }

  if (command == "BAMBU_SET_TOKEN") {
    runSetToken(remainder);
    return;
  }

  if (command == "BAMBU_STATUS") {
    runBambuStatus(remainder);
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

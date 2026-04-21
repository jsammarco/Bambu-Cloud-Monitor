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
constexpr uint32_t kMqttTimeoutMs = 8000;

Preferences prefs;
String bridgeLine;
WiFiClientSecure mqttNetClient;
PubSubClient mqttClient(mqttNetClient);
String mqttExpectedTopic;
String mqttLastPayload;
bool mqttMessageReceived = false;

void bridgeReply(const String& line);

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
  bridgeReply("ERR|BAMBU_STATUS requires serial,ip,access_code");
}

void runBambuStatusLocal(const String& serial, const String& ip, const String& accessCode) {
  DynamicJsonDocument doc(8192);
  JsonObject print;
  String requestTopic;
  String error;
  String line;
  uint32_t start = 0;

  if (serial.isEmpty() || ip.isEmpty() || accessCode.isEmpty()) {
    bridgeReply("ERR|Missing serial, ip, or access code");
    return;
  }

  if (!ensureWifiConnected()) {
    return;
  }

  mqttNetClient.stop();
  mqttNetClient.setInsecure();
  mqttClient.setServer(ip.c_str(), kMqttPort);
  mqttClient.setCallback(onMqttMessage);
  mqttClient.setBufferSize(4096);

  mqttExpectedTopic = "device/" + serial + "/report";
  requestTopic = "device/" + serial + "/request";
  mqttLastPayload = "";
  mqttMessageReceived = false;

  if (!mqttClient.connect(serial.c_str(), "bblp", accessCode.c_str())) {
    bridgeReply("ERR|MQTT connect failed " + String(mqttClient.state()));
    return;
  }

  if (!mqttClient.subscribe(mqttExpectedTopic.c_str())) {
    mqttClient.disconnect();
    bridgeReply("ERR|MQTT subscribe failed");
    return;
  }

  mqttClient.publish(
      requestTopic.c_str(),
      "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}");

  start = millis();
  while (!mqttMessageReceived && millis() - start < kMqttTimeoutMs) {
    mqttClient.loop();
    delay(10);
  }

  mqttClient.disconnect();

  if (!mqttMessageReceived) {
    bridgeReply("ERR|MQTT timeout");
    return;
  }

  DeserializationError jsonError = deserializeJson(doc, mqttLastPayload);
  if (jsonError) {
    bridgeReply("ERR|MQTT JSON parse failed");
    return;
  }

  if (!doc["print"].is<JsonObject>()) {
    bridgeReply("ERR|MQTT payload missing print data");
    return;
  }

  print = doc["print"].as<JsonObject>();
  line = "STATUS|";
  line += serial;
  line += "|";
  line += String(print["gcode_state"] | "");
  line += "|";
  line += String(print["mc_percent"] | 0);
  line += "|";
  line += String(print["layer_num"] | 0);
  line += "|";
  line += String(print["total_layer_num"] | 0);
  line += "|";
  line += String(print["mc_remaining_time"] | 0);
  line += "|";
  line += String(static_cast<float>(print["nozzle_temper"] | 0));
  line += "|";
  line += String(static_cast<float>(print["bed_temper"] | 0));
  line += "|";
  line += String(print["wifi_signal"] | "");
  line += "|";
  line += String(print["gcode_file"] | "");
  bridgeReply(line);
  bridgeReply("OK|BAMBU_STATUS|1");
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

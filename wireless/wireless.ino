/*
 * BanPCBTool ESP8266 wireless bridge
 *
 * Functions:
 *   - STM32 UART3 <-> WiFi TCP transparent bridge, default TCP port 8266.
 *   - UDP discovery for the desktop tool, default UDP port 8267.
 *   - Auto STA connect. If WiFi is unavailable, start a visible AP.
 *   - HTTP provisioning and OTA update. Open http://<device-ip>/update.
 *   - Local control lines beginning with "@BPC " are consumed by ESP8266.
 */

#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <WiFiUdp.h>

#define DEVICE_DEFAULT_NAME   "BanPCBTool"
#define UART_BAUD            115200UL
#define BRIDGE_TCP_PORT      8266
#define DISCOVERY_UDP_PORT   8267
#define HTTP_PORT            80
#define WIFI_CONNECT_TIMEOUT 15000UL
#define EEPROM_BYTES         512
#define CONFIG_MAGIC         0x42504354UL
#define CONTROL_MAX          160
#define WIFI_RETRY_MS        10000UL

struct DeviceConfig {
    uint32_t magic;
    char name[32];
    char ssid[64];
    char pass[64];
};

DeviceConfig cfg;
ESP8266WebServer httpServer(HTTP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
WiFiServer bridgeServer(BRIDGE_TCP_PORT);
WiFiClient bridgeClient;
WiFiUDP discoveryUdp;

bool apRunning = false;
bool bridgeStarted = false;
bool rebootPending = false;
unsigned long rebootAt = 0;
unsigned long staStartAt = 0;
unsigned long lastWifiRetry = 0;

char serialCtl[CONTROL_MAX];
uint16_t serialCtlLen = 0;
bool serialMaybeControl = false;
bool serialLineStart = true;

char tcpCtl[CONTROL_MAX];
uint16_t tcpCtlLen = 0;
bool tcpMaybeControl = false;
bool tcpLineStart = true;

static void scheduleRestart(uint32_t delayMs) {
    rebootPending = true;
    rebootAt = millis() + delayMs;
}

static String deviceName() {
    return cfg.name[0] ? String(cfg.name) : String(DEVICE_DEFAULT_NAME);
}

static void copyField(char *dst, size_t dstSize, const String &src) {
    size_t len = src.length();
    if (len >= dstSize) len = dstSize - 1;
    memcpy(dst, src.c_str(), len);
    dst[len] = '\0';
}

static void loadConfig() {
    EEPROM.begin(EEPROM_BYTES);
    EEPROM.get(0, cfg);
    if (cfg.magic != CONFIG_MAGIC) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = CONFIG_MAGIC;
        copyField(cfg.name, sizeof(cfg.name), DEVICE_DEFAULT_NAME);
        EEPROM.put(0, cfg);
        EEPROM.commit();
    }
}

static void saveConfig() {
    cfg.magic = CONFIG_MAGIC;
    EEPROM.put(0, cfg);
    EEPROM.commit();
}

static bool hasSavedWifi() {
    return cfg.ssid[0] != '\0';
}

static String ipText() {
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
    if (apRunning) return WiFi.softAPIP().toString();
    return String("0.0.0.0");
}

static void sendTcpLine(const String &line) {
    if (bridgeClient && bridgeClient.connected()) {
        bridgeClient.print(line);
        if (!line.endsWith("\n")) bridgeClient.print("\n");
    }
}

static void startBridgeServices() {
    if (bridgeStarted) return;
    bridgeServer.begin();
    bridgeServer.setNoDelay(true);
    discoveryUdp.begin(DISCOVERY_UDP_PORT);
    bridgeStarted = true;
}

static void startConfigAp() {
    if (apRunning) return;
    String ssid = deviceName();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid.c_str());
    apRunning = true;
    startBridgeServices();
}

static void stopConfigApIfConnected() {
    if (!apRunning || WiFi.status() != WL_CONNECTED) return;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apRunning = false;
}

static void startStaConnect() {
    if (!hasSavedWifi()) {
        startConfigAp();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid, cfg.pass);
    staStartAt = millis();
    lastWifiRetry = millis();
}

static String jsonInfo() {
    String json = "{";
    json += "\"name\":\"" + deviceName() + "\",";
    json += "\"ip\":\"" + ipText() + "\",";
    json += "\"bridge\":" + String(BRIDGE_TCP_PORT) + ",";
    json += "\"http\":" + String(HTTP_PORT) + ",";
    json += "\"ap\":" + String(apRunning ? 1 : 0) + ",";
    json += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? 1 : 0) + ",";
    json += "\"ssid\":\"" + String(cfg.ssid) + "\",";
    json += "\"version\":\"1.0.0\"";
    json += "}";
    return json;
}

static void handleRoot() {
    String html;
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>BanPCBTool</title></head><body>";
    html += "<h2>BanPCBTool WiFi</h2>";
    html += "<p>AP fixed IP: <b>192.168.4.1</b> (connect to the hotspot then open "
            "http://192.168.4.1)</p>";
    html += "<pre>" + jsonInfo() + "</pre>";
    html += "<form method='post' action='/api/wifi'>";
    html += "SSID:<br><input name='ssid' value='" + String(cfg.ssid) + "'><br>";
    html += "Password:<br><input name='pass' type='password'><br><br>";
    html += "<button type='submit'>Save WiFi</button></form>";
    html += "<p><a href='/update'>OTA update</a></p>";
    html += "</body></html>";
    httpServer.send(200, "text/html", html);
}

static void handleInfo() {
    httpServer.send(200, "application/json", jsonInfo());
}

static void handleWifiSave() {
    if (!httpServer.hasArg("ssid")) {
        httpServer.send(400, "application/json", "{\"ok\":0,\"error\":\"missing ssid\"}");
        return;
    }
    copyField(cfg.ssid, sizeof(cfg.ssid), httpServer.arg("ssid"));
    copyField(cfg.pass, sizeof(cfg.pass),
              httpServer.hasArg("pass") ? httpServer.arg("pass") : httpServer.arg("password"));
    saveConfig();
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"saved\"}");
    delay(100);
    WiFi.disconnect();
    startStaConnect();
}

static void handleAp() {
    startConfigAp();
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"ap started\"}");
}

static void handleClear() {
    cfg.ssid[0] = '\0';
    cfg.pass[0] = '\0';
    saveConfig();
    startConfigAp();
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"wifi cleared\"}");
}

static void handleRestart() {
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"restarting\"}");
    scheduleRestart(300);
}

static void handleScan() {
    int count = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < count; i++) {
        if (i) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    httpServer.send(200, "application/json", json);
}

static void handleOtaUrl() {
    if (!httpServer.hasArg("url")) {
        httpServer.send(400, "application/json", "{\"ok\":0,\"error\":\"missing url\"}");
        return;
    }
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"ota started\"}");
    WiFiClient otaClient;
    t_httpUpdate_return result = ESPhttpUpdate.update(otaClient, httpServer.arg("url"));
    if (result == HTTP_UPDATE_FAILED) {
        sendTcpLine("@BPC OTA_FAILED " + String(ESPhttpUpdate.getLastError()));
    }
}

static void setupHttp() {
    httpServer.on("/", HTTP_GET, handleRoot);
    httpServer.on("/api/info", HTTP_GET, handleInfo);
    httpServer.on("/api/scan", HTTP_GET, handleScan);
    httpServer.on("/api/wifi", HTTP_POST, handleWifiSave);
    httpServer.on("/api/ap", HTTP_POST, handleAp);
    httpServer.on("/api/clear", HTTP_POST, handleClear);
    httpServer.on("/api/restart", HTTP_POST, handleRestart);
    httpServer.on("/api/ota", HTTP_POST, handleOtaUrl);
    httpUpdater.setup(&httpServer, "/update");
    httpServer.begin();
}

static void handleDiscovery() {
    int size = discoveryUdp.parsePacket();
    if (size <= 0) return;
    char packet[96];
    int len = discoveryUdp.read(packet, sizeof(packet) - 1);
    if (len <= 0) return;
    packet[len] = '\0';
    String req(packet);
    req.trim();
    if (req.indexOf("BANPCBTOOL?") < 0 && req.indexOf("BANUX_DISCOVER") < 0) return;
    String reply = "BANPCBTool name=" + deviceName() +
                   " ip=" + ipText() +
                   " bridge=" + String(BRIDGE_TCP_PORT) +
                   " http=" + String(HTTP_PORT) +
                   " wifi=" + String(WiFi.status() == WL_CONNECTED ? 1 : 0) +
                   " ap=" + String(apRunning ? 1 : 0) + "\n";
    discoveryUdp.beginPacket(discoveryUdp.remoteIP(), discoveryUdp.remotePort());
    discoveryUdp.print(reply);
    discoveryUdp.endPacket();
}

static void handleControlLine(const String &line, bool fromTcp) {
    String cmd = line;
    cmd.trim();
    if (!cmd.startsWith("@BPC")) return;
    cmd = cmd.substring(4);
    cmd.trim();

    if (cmd.startsWith("HELLO")) {
        if (fromTcp) sendTcpLine("@BPC OK " + deviceName());
    } else if (cmd == "STATUS") {
        if (fromTcp) sendTcpLine("@BPC " + jsonInfo());
    } else if (cmd == "AP" || cmd == "CONFIG") {
        startConfigAp();
        if (fromTcp) sendTcpLine("@BPC OK ap");
    } else if (cmd == "CLEAR") {
        cfg.ssid[0] = '\0';
        cfg.pass[0] = '\0';
        saveConfig();
        startConfigAp();
        if (fromTcp) sendTcpLine("@BPC OK clear");
    } else if (cmd.startsWith("NAME ")) {
        copyField(cfg.name, sizeof(cfg.name), cmd.substring(5));
        saveConfig();
        if (fromTcp) sendTcpLine("@BPC OK name=" + deviceName());
    } else if (cmd.startsWith("WIFI ")) {
        int split = cmd.indexOf(' ', 5);
        if (split > 5) {
            copyField(cfg.ssid, sizeof(cfg.ssid), cmd.substring(5, split));
            copyField(cfg.pass, sizeof(cfg.pass), cmd.substring(split + 1));
            saveConfig();
            WiFi.disconnect();
            startStaConnect();
            if (fromTcp) sendTcpLine("@BPC OK wifi");
        } else if (fromTcp) {
            sendTcpLine("@BPC ERR wifi_args");
        }
    } else if (cmd.startsWith("OTA ")) {
        if (fromTcp) sendTcpLine("@BPC OK ota");
        WiFiClient otaClient;
        ESPhttpUpdate.update(otaClient, cmd.substring(4));
    } else if (cmd == "RESTART") {
        if (fromTcp) sendTcpLine("@BPC OK restart");
        scheduleRestart(300);
    } else if (fromTcp) {
        sendTcpLine("@BPC ERR unknown");
    }
}

static bool controlPrefixMatches(const char *buf, uint16_t len) {
    const char prefix[] = "@BPC";
    if (len > strlen(prefix)) return false;
    for (uint16_t i = 0; i < len; i++) {
        if (buf[i] != prefix[i]) return false;
    }
    return true;
}

static void flushSerialControlToTcp() {
    if (bridgeClient && bridgeClient.connected() && serialCtlLen > 0) {
        bridgeClient.write((const uint8_t *)serialCtl, serialCtlLen);
    }
    serialCtlLen = 0;
    serialMaybeControl = false;
}

static void feedSerialByte(uint8_t byte) {
    if (serialLineStart && byte == '@') {
        serialMaybeControl = true;
        serialCtlLen = 0;
    }

    if (serialMaybeControl) {
        if (serialCtlLen < CONTROL_MAX - 1) {
            serialCtl[serialCtlLen++] = (char)byte;
            serialCtl[serialCtlLen] = '\0';
        }
        if (!controlPrefixMatches(serialCtl, serialCtlLen)) {
            flushSerialControlToTcp();
        } else if (byte == '\n' || byte == '\r') {
            handleControlLine(String(serialCtl), false);
            serialCtlLen = 0;
            serialMaybeControl = false;
        }
    } else if (bridgeClient && bridgeClient.connected()) {
        bridgeClient.write(byte);
    }
    serialLineStart = (byte == '\n' || byte == '\r');
}

static void flushTcpControlToSerial() {
    if (tcpCtlLen > 0) Serial.write((const uint8_t *)tcpCtl, tcpCtlLen);
    tcpCtlLen = 0;
    tcpMaybeControl = false;
}

static void feedTcpByte(uint8_t byte) {
    if (tcpLineStart && byte == '@') {
        tcpMaybeControl = true;
        tcpCtlLen = 0;
    }

    if (tcpMaybeControl) {
        if (tcpCtlLen < CONTROL_MAX - 1) {
            tcpCtl[tcpCtlLen++] = (char)byte;
            tcpCtl[tcpCtlLen] = '\0';
        }
        if (!controlPrefixMatches(tcpCtl, tcpCtlLen)) {
            flushTcpControlToSerial();
        } else if (byte == '\n' || byte == '\r') {
            handleControlLine(String(tcpCtl), true);
            tcpCtlLen = 0;
            tcpMaybeControl = false;
        }
    } else {
        Serial.write(byte);
    }
    tcpLineStart = (byte == '\n' || byte == '\r');
}

static void handleBridge() {
    if (bridgeServer.hasClient()) {
        WiFiClient incoming = bridgeServer.available();
        if (bridgeClient && bridgeClient.connected()) {
            bridgeClient.stop();
        }
        bridgeClient = incoming;
        bridgeClient.setNoDelay(true);
        sendTcpLine("@BPC CONNECTED " + deviceName());
    }

    while (Serial.available()) {
        feedSerialByte((uint8_t)Serial.read());
    }
    if (bridgeClient && bridgeClient.connected()) {
        while (bridgeClient.available()) {
            feedTcpByte((uint8_t)bridgeClient.read());
        }
    }
}

static void handleWifi() {
    if (WiFi.status() == WL_CONNECTED) {
        startBridgeServices();
        stopConfigApIfConnected();
        return;
    }
    if (!apRunning && millis() - staStartAt > WIFI_CONNECT_TIMEOUT) {
        startConfigAp();
    }
    if (hasSavedWifi() && millis() - lastWifiRetry > WIFI_RETRY_MS) {
        WiFi.begin(cfg.ssid, cfg.pass);
        lastWifiRetry = millis();
    }
}

void setup() {
    Serial.begin(UART_BAUD);
    Serial.setRxBufferSize(1024);
    loadConfig();
    startStaConnect();
    setupHttp();
    startBridgeServices();
}

void loop() {
    httpServer.handleClient();
    handleWifi();
    handleDiscovery();
    handleBridge();
    if (rebootPending && (int32_t)(millis() - rebootAt) >= 0) {
        ESP.restart();
    }
}

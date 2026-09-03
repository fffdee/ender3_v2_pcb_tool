/*
 * BanPCBTool ESP8266 wireless bridge
 *
 * Functions:
 *   - STM32 UART3 <-> WiFi TCP transparent bridge, default TCP port 8266.
 *   - UDP discovery for the desktop tool, default UDP port 8267.
 *   - Auto STA connect. If WiFi is unavailable, start a secured AP with chipId SSID.
 *   - HTTP provisioning (Ban-IOT compatible) and OTA update.
 *   - Local control lines beginning with "@BPC " are consumed by ESP8266.
 *
 * Provisioning flow (100% Ban-IOT APP compatible):
 *   1. First boot / no saved WiFi → AP mode: SSID "BanPCBToolXXXX", PWD "12345678", IP 192.168.4.1
 *   2. APP scans for "BanPCBTool*" prefix, connects to AP
 *   3. APP POST /config with ssid/pass/topic/static_ip/uid → device saves, returns JSON, restarts in 3s
 *   4. Boot with saved WiFi → STA mode (with optional static IP), connects to router, same HTTP endpoints
 *   5. POST /reconfig clears WiFi & restarts back to AP mode
 *   6. GET  /status returns full device state (ap_mode, sta_mode, ip, ssid, topic, etc.)
 */

#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <WiFiUdp.h>

/* ============================================================
 * EEPROM Layout (Ban-IOT compatible + device name extension)
 *   0     : magic byte (0xAA)
 *   1..32 : SSID (AP_SSID_MAX 32)
 *  33..96 : Password (AP_PASS_MAX 64)
 *  97..128: Topic (AP_TOPIC_MAX 32)
 * 151..154: STA static IP (4 bytes, 0/0xFF = DHCP)
 * 155..194: Bemfa / cloud UID (AP_UID_MAX 40)
 * 200..231: Device name (32 bytes, @BPC NAME <x>)
 * ============================================================ */
#define EEPROM_BYTES         512

#define AP_MAGIC_ADDR        0
#define AP_MAGIC_BYTE        0xAA
#define AP_SSID_ADDR         1
#define AP_SSID_MAX          32
#define AP_PASS_ADDR         33
#define AP_PASS_MAX          64
#define AP_TOPIC_ADDR        97
#define AP_TOPIC_MAX         32
#define AP_STATIC_IP_ADDR   151
#define AP_UID_ADDR         155
#define AP_UID_MAX          40
#define DEVICE_NAME_ADDR    200
#define DEVICE_NAME_MAX      32

#define AP_AP_PASSWORD      "12345678"
#define AP_CONNECT_TIMEOUT  10000UL
#define AP_CONFIG_RESTART_DELAY 3000UL

/* Bridge / legacy parameters */
#define DEVICE_DEFAULT_NAME   "BanPCBTool"
#define UART_BAUD            115200UL
#define BRIDGE_TCP_PORT      8266
#define DISCOVERY_UDP_PORT   8267
#define HTTP_PORT            80
#define WIFI_RETRY_MS        10000UL
#define CONTROL_MAX          160

ESP8266WebServer httpServer(HTTP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
WiFiServer bridgeServer(BRIDGE_TCP_PORT);
WiFiClient bridgeClient;
/* 桥接 TCP 空闲超时：PC 闪退/断网常不发 FIN，ESP 侧半开连接会长期占用唯一连接槽，
 * 导致 PC 重连困难。超过该时长无任何桥接流量即主动断开（可按需调大以免误杀空闲会话）。 */
#define BRIDGE_IDLE_TIMEOUT_MS  60000UL
static unsigned long bridgeLastActivity = 0;
/* 透传批量写出缓冲的冲刷函数（定义在文件后段），此处前置声明以便 sendTcpLine
 * 等前段代码能先冲刷缓冲、保证字节顺序。 */
static void serialBatchFlush();
static void tcpBatchFlush();
WiFiUDP discoveryUdp;

bool apModeActive = false;
bool bridgeStarted = false;
bool rebootPending = false;
unsigned long rebootAt = 0;
unsigned long lastWifiRetry = 0;

String apSsidStr;                    // generated AP SSID: BanPCBTool%04X
String apDeviceType = "pcbtool";     // returned in /config JSON
String apDeviceTopic = "";           // loaded from EEPROM on boot

char serialCtl[CONTROL_MAX];
uint16_t serialCtlLen = 0;
bool serialMaybeControl = false;
bool serialLineStart = true;
bool serialSkipLf = false;

char tcpCtl[CONTROL_MAX];
uint16_t tcpCtlLen = 0;
bool tcpMaybeControl = false;
bool tcpLineStart = true;
bool tcpSkipLf = false;

/* ============================================================
 *  Restart scheduler
 * ============================================================ */
static void scheduleRestart(uint32_t delayMs) {
    rebootPending = true;
    rebootAt = millis() + delayMs;
}

/* ============================================================
 *  EEPROM byte-level helpers (Ban-IOT compatible)
 * ============================================================ */
static String apReadStr(int addr, int maxLen) {
    String s = "";
    for (int i = 0; i < maxLen; i++) {
        byte b = EEPROM.read(addr + i);
        if (b == 0 || b == 0xFF) break;
        s += (char)b;
    }
    return s;
}

static void apWriteStr(int addr, int maxLen, const String &s) {
    for (int i = 0; i < maxLen; i++) {
        EEPROM.write(addr + i, i < (int)s.length() ? s[i] : 0);
    }
}

static bool apHasSavedWifi() {
    return EEPROM.read(AP_MAGIC_ADDR) == AP_MAGIC_BYTE;
}

static String apGetSavedSsid()   { return apReadStr(AP_SSID_ADDR, AP_SSID_MAX); }
static String apGetSavedPass()   { return apReadStr(AP_PASS_ADDR, AP_PASS_MAX); }
static String apGetSavedTopic()  { return apReadStr(AP_TOPIC_ADDR, AP_TOPIC_MAX); }
static String apGetSavedUid()    { return apReadStr(AP_UID_ADDR,  AP_UID_MAX);  }
static String apGetDeviceName() {
    String s = apReadStr(DEVICE_NAME_ADDR, DEVICE_NAME_MAX);
    return s.length() > 0 ? s : String(DEVICE_DEFAULT_NAME);
}

static void apSaveWifi(const String &ssid, const String &pass) {
    apWriteStr(AP_SSID_ADDR, AP_SSID_MAX, ssid);
    apWriteStr(AP_PASS_ADDR, AP_PASS_MAX, pass);
    EEPROM.write(AP_MAGIC_ADDR, AP_MAGIC_BYTE);
    EEPROM.commit();
}

static void apClearWifi() {
    // clear magic + ssid + pass + topic range
    for (int i = 0; i <= AP_TOPIC_ADDR + AP_TOPIC_MAX - 1; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
}

static void apSaveTopic(const String &topic) {
    apWriteStr(AP_TOPIC_ADDR, AP_TOPIC_MAX, topic);
    EEPROM.commit();
    apDeviceTopic = topic;
}

static void apSaveUid(const String &uid) {
    apWriteStr(AP_UID_ADDR, AP_UID_MAX, uid);
    EEPROM.commit();
}

static void apSaveDeviceName(const String &name) {
    apWriteStr(DEVICE_NAME_ADDR, DEVICE_NAME_MAX, name);
    EEPROM.commit();
}

/* Static IP helpers */
static IPAddress apGetStaticIp() {
    uint8_t b1 = EEPROM.read(AP_STATIC_IP_ADDR);
    uint8_t b2 = EEPROM.read(AP_STATIC_IP_ADDR + 1);
    uint8_t b3 = EEPROM.read(AP_STATIC_IP_ADDR + 2);
    uint8_t b4 = EEPROM.read(AP_STATIC_IP_ADDR + 3);
    if (b1 == 0 || b1 == 0xFF) return IPAddress((uint32_t)0); // use DHCP
    return IPAddress(b1, b2, b3, b4);
}

static void apSaveStaticIp(const IPAddress &ip) {
    EEPROM.write(AP_STATIC_IP_ADDR,     ip[0]);
    EEPROM.write(AP_STATIC_IP_ADDR + 1, ip[1]);
    EEPROM.write(AP_STATIC_IP_ADDR + 2, ip[2]);
    EEPROM.write(AP_STATIC_IP_ADDR + 3, ip[3]);
    EEPROM.commit();
}

/* ============================================================
 *  Status / helpers
 * ============================================================ */
static String ipText() {
    if (!apModeActive && WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
    if (apModeActive) return WiFi.softAPIP().toString();
    return String("0.0.0.0");
}

static void sendTcpLine(const String &line) {
    serialBatchFlush();   /* 先冲刷透传缓冲，避免控制应答插到透传数据前面 */
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

/* ============================================================
 *  Full JSON status (Ban-IOT APP /status compatible)
 * ============================================================ */
static String fullStatusJson() {
    String savedTopic = apGetSavedTopic();
    String displayTopic = savedTopic.length() > 0 ? savedTopic : apDeviceTopic;
    bool staMode = !apModeActive && WiFi.status() == WL_CONNECTED;

    String json = "{";
    json += "\"ap_mode\":" + String(apModeActive ? "true" : "false") + ",";
    json += "\"sta_mode\":" + String(staMode ? "true" : "false") + ",";
    json += "\"device_ip\":\"" + (staMode ? WiFi.localIP().toString() : String("")) + "\",";
    json += "\"static_ip\":\"" + apGetStaticIp().toString() + "\",";
    json += "\"ap_ssid\":\"" + apSsidStr + "\",";
    json += "\"device_type\":\"" + apDeviceType + "\",";
    json += "\"device_topic\":\"" + displayTopic + "\",";
    json += "\"device_uid\":\"" + apGetSavedUid() + "\",";
    json += "\"saved_ssid\":\"" + apGetSavedSsid() + "\",";
    json += "\"saved_pass\":\"" + apGetSavedPass() + "\",";
    json += "\"bridge_port\":" + String(BRIDGE_TCP_PORT) + ",";
    json += "\"http_port\":" + String(HTTP_PORT) + ",";
    json += "\"name\":\"" + apGetDeviceName() + "\",";
    json += "\"version\":\"1.1.0\"";
    json += "}";
    return json;
}

/* Legacy /api/info JSON (backward compat) */
static String jsonInfo() {
    String json = "{";
    json += "\"name\":\"" + apGetDeviceName() + "\",";
    json += "\"ip\":\"" + ipText() + "\",";
    json += "\"bridge\":" + String(BRIDGE_TCP_PORT) + ",";
    json += "\"http\":" + String(HTTP_PORT) + ",";
    json += "\"ap\":" + String(apModeActive ? 1 : 0) + ",";
    json += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? 1 : 0) + ",";
    json += "\"ssid\":\"" + apGetSavedSsid() + "\",";
    json += "\"version\":\"1.1.0\"";
    json += "}";
    return json;
}

/* ============================================================
 *  HTTP handlers — Ban-IOT compatible provisioning
 * ============================================================ */
static void apHandleRoot() {
    String html = F("<!DOCTYPE html><html lang='zh-CN'><head>"
        "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>BanPCBTool WiFi</title>"
        "<style>*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
        "background:linear-gradient(135deg,#1a1a2e,#16213e);min-height:100vh;color:#eee;padding:20px}"
        "h1{color:#e94560;font-size:1.8rem;margin-bottom:8px;text-align:center}"
        ".sub{color:#888;font-size:0.85rem;text-align:center;margin-bottom:24px}"
        ".card{background:#16213e;border-radius:12px;padding:20px;margin-bottom:16px;"
        "box-shadow:0 4px 15px rgba(0,0,0,0.3);border-top:3px solid #e94560}"
        "label{display:block;color:#00d9ff;font-size:0.85rem;margin-bottom:6px;font-weight:600}"
        "input,select{width:100%;padding:12px;margin-bottom:14px;border:1px solid #0f3460;"
        "border-radius:8px;background:#0f3460;color:#eee;font-size:1rem;outline:none}"
        "input:focus,select:focus{border-color:#00d9ff}"
        "button{width:100%;padding:14px;background:#e94560;color:#fff;border:none;"
        "border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer}"
        "button:hover{background:#ff6b81}"
        "#status{margin-top:12px;text-align:center;color:#aaa;font-size:0.9rem}"
        "#banner{padding:10px;border-radius:8px;margin-bottom:16px;text-align:center;font-weight:bold;background:#0d3b00;color:#00e676;display:none}"
        "</style></head><body>"
        "<h1>&#x1F527; BanPCBTool</h1>"
        "<div class='sub'>WiFi Configuration</div>"
        "<div id='banner'></div>"
        "<div class='card'>"
        "<label>WiFi Name</label>"
        "<select id='ssidSel' onchange='document.getElementById(\"ssid\").value=this.value'>"
        "<option value=''>-- Select --</option></select>"
        "<input id='ssid' name='ssid' placeholder='Or type WiFi name'>"
        "<label>WiFi Password</label>"
        "<input id='pass' name='pass' type='password' placeholder='WiFi password'>"
        "<label>Topic (optional)</label>"
        "<input id='topic' name='topic' placeholder='e.g. pcbtool001'>"
        "<label>UID (optional)</label>"
        "<input id='uid' name='uid' placeholder='Cloud private key'>"
        "<label>Static IP (optional)</label>"
        "<input id='sip' name='static_ip' placeholder='192.168.1.200'>"
        "<button onclick='doSave()'>Save &amp; Restart</button>"
        "<button onclick='reconfig()' style='background:#555;margin-top:8px'>&#x1F504; Reset WiFi Config</button>"
        "<div id='status'></div>"
        "</div>"
        "<script>"
        "function doSave(){"
        "var s=document.getElementById('ssid').value||document.getElementById('ssidSel').value;"
        "var p=document.getElementById('pass').value;"
        "if(!s){document.getElementById('status').innerText='Please enter WiFi name';return}"
        "document.getElementById('status').innerText='Saving...';"
        "var x=new XMLHttpRequest();"
        "x.open('POST','/config',true);"
        "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
        "x.onload=function(){document.getElementById('status').innerText=x.responseText};"
        "var t=document.getElementById('topic').value;"
        "var u=document.getElementById('uid').value;"
        "var ip=document.getElementById('sip').value;"
        "x.send('ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p)+'&topic='+encodeURIComponent(t)+'&static_ip='+encodeURIComponent(ip)+'&uid='+encodeURIComponent(u))}"
        "function reconfig(){fetch('/reconfig',{method:'POST'}).then(r=>r.text()).then(t=>{document.getElementById('status').innerText=t})}"
        "var sx=new XMLHttpRequest();"
        "sx.onload=function(){"
        "var nets=JSON.parse(this.responseText);"
        "var sel=document.getElementById('ssidSel');"
        "for(var i=0;i<nets.length;i++){"
        "var o=document.createElement('option');"
        "o.value=nets[i].ssid;o.text=nets[i].ssid+' ('+nets[i].rssi+'dBm)';"
        "sel.appendChild(o)}};"
        "sx.open('GET','/scan',true);sx.send();"
        "var stx=new XMLHttpRequest();"
        "stx.onload=function(){try{var info=JSON.parse(this.responseText);"
        "if(info.device_topic)document.getElementById('topic').value=info.device_topic;"
        "if(info.static_ip && info.static_ip!='0.0.0.0')document.getElementById('sip').value=info.static_ip;"
        "if(info.device_uid)document.getElementById('uid').value=info.device_uid;"
        "if(info.saved_ssid){document.getElementById('ssidSel').value=info.saved_ssid;document.getElementById('ssid').value=info.saved_ssid;}"
        "if(info.saved_pass)document.getElementById('pass').value=info.saved_pass;"
        "if(info.sta_mode){var b=document.getElementById('banner');b.style.display='block';b.innerHTML='&#x2705; STA Connected | IP: '+info.device_ip;}"
        "}catch(e){}};"
        "stx.open('GET','/status',true);stx.send()"
        "</script></body></html>");
    httpServer.send(200, "text/html", html);
}

/* Ban-IOT APP compatible: POST /config → saves config, returns JSON, restarts in 3s */
static void apHandleConfig() {
    if (!httpServer.hasArg("ssid") || httpServer.arg("ssid").length() == 0) {
        httpServer.send(400, "text/plain", "SSID is required");
        return;
    }
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.arg("pass");
    // if user didn't retype password, keep the saved one (Ban-IOT behavior)
    if (pass.length() == 0) pass = apGetSavedPass();

    apSaveWifi(ssid, pass);

    String topic = httpServer.arg("topic");
    if (topic.length() > 0) apSaveTopic(topic);

    if (httpServer.hasArg("static_ip")) {
        IPAddress sip;
        if (sip.fromString(httpServer.arg("static_ip"))) apSaveStaticIp(sip);
    }

    String uid = httpServer.arg("uid");
    if (uid.length() > 0) apSaveUid(uid);

    // Echo back everything we have
    String resp = "{";
    resp += "\"status\":\"ok\",";
    resp += "\"device_type\":\"" + apDeviceType + "\",";
    resp += "\"device_topic\":\"" + apDeviceTopic + "\",";
    resp += "\"static_ip\":\"" + apGetStaticIp().toString() + "\",";
    resp += "\"device_uid\":\"" + apGetSavedUid() + "\"";
    resp += "}";
    httpServer.send(200, "application/json", resp);

    // Restart after delay so the APP can read the response
    scheduleRestart(AP_CONFIG_RESTART_DELAY);
}

static void apHandleScan() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    httpServer.send(200, "application/json", json);
}

static void apHandleStatus() {
    httpServer.send(200, "application/json", fullStatusJson());
}

static void apHandleReconfig() {
    apClearWifi();
    httpServer.send(200, "text/plain", "WiFi config cleared. Restarting in AP mode...");
    scheduleRestart(1500);
}

static void apHandleSetIp() {
    String ipStr = httpServer.arg("static_ip");
    IPAddress ip;
    if (!ipStr.isEmpty() && ip.fromString(ipStr)) {
        apSaveStaticIp(ip);
        httpServer.send(200, "text/plain", "Static IP saved: " + ipStr + ". Restarting...");
        scheduleRestart(1500);
    } else {
        httpServer.send(400, "text/plain", "Invalid IP address");
    }
}

/* ============================================================
 *  Legacy HTTP handlers (backward compat with existing tools)
 * ============================================================ */
static void handleInfo() {
    httpServer.send(200, "application/json", jsonInfo());
}

static void handleWifiSave() {
    // legacy /api/wifi - redirect logic to the new path then respond old format
    if (!httpServer.hasArg("ssid")) {
        httpServer.send(400, "application/json", "{\"ok\":0,\"error\":\"missing ssid\"}");
        return;
    }
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.hasArg("pass") ? httpServer.arg("pass")
                 : (httpServer.hasArg("password") ? httpServer.arg("password") : "");
    apSaveWifi(ssid, pass);
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"saved\"}");
    scheduleRestart(AP_CONFIG_RESTART_DELAY);
}

static void handleAp() {
    // don't actually switch mode here — just say ok, real AP only happens when STA fails on boot
    // Or force start AP now
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(apSsidStr.c_str(), AP_AP_PASSWORD);
    apModeActive = true;
    startBridgeServices();
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"ap started\"}");
}

static void handleClear() {
    apClearWifi();
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"wifi cleared\"}");
    scheduleRestart(1500);
}

static void handleRestart() {
    httpServer.send(200, "application/json", "{\"ok\":1,\"message\":\"restarting\"}");
    scheduleRestart(300);
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

/* ============================================================
 *  HTTP server setup — both new Ban-IOT paths + legacy paths
 * ============================================================ */
static void setupHttp() {
    // Ban-IOT compatible provisioning paths
    httpServer.on("/",                HTTP_GET,  apHandleRoot);
    httpServer.on("/config",          HTTP_POST, apHandleConfig);
    httpServer.on("/scan",            HTTP_GET,  apHandleScan);
    httpServer.on("/status",          HTTP_GET,  apHandleStatus);
    httpServer.on("/reconfig",        HTTP_POST, apHandleReconfig);
    httpServer.on("/setip",           HTTP_POST, apHandleSetIp);

    // Legacy paths (backward compatible with older PC tools)
    httpServer.on("/api/info",        HTTP_GET,  handleInfo);
    httpServer.on("/api/scan",        HTTP_GET,  apHandleScan);
    httpServer.on("/api/wifi",        HTTP_POST, handleWifiSave);
    httpServer.on("/api/ap",          HTTP_POST, handleAp);
    httpServer.on("/api/clear",       HTTP_POST, handleClear);
    httpServer.on("/api/restart",     HTTP_POST, handleRestart);
    httpServer.on("/api/ota",         HTTP_POST, handleOtaUrl);

    httpUpdater.setup(&httpServer, "/update");
    httpServer.begin();
}

/* ============================================================
 *  UDP discovery (legacy format — keep PC tool discovery working)
 * ============================================================ */
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
    String reply = "BANPCBTool name=" + apGetDeviceName() +
                   " ip=" + ipText() +
                   " bridge=" + String(BRIDGE_TCP_PORT) +
                   " http=" + String(HTTP_PORT) +
                   " wifi=" + String(WiFi.status() == WL_CONNECTED ? 1 : 0) +
                   " ap=" + String(apModeActive ? 1 : 0) + "\n";
    discoveryUdp.beginPacket(discoveryUdp.remoteIP(), discoveryUdp.remotePort());
    discoveryUdp.print(reply);
    discoveryUdp.endPacket();
}

/* ============================================================
 *  @BPC control line handling (serial + tcp)
 * ============================================================ */
static void handleControlLine(const String &line, bool fromTcp) {
    String cmd = line;
    cmd.trim();
    if (!cmd.startsWith("@BPC")) return;
    cmd = cmd.substring(4);
    cmd.trim();

    if (cmd.startsWith("PING")) {
        if (fromTcp) sendTcpLine("@BPC PONG " + apGetDeviceName());
    } else if (cmd.startsWith("HELLO")) {
        if (fromTcp) sendTcpLine("@BPC OK " + apGetDeviceName());
    } else if (cmd == "STATUS") {
        if (fromTcp) sendTcpLine("@BPC " + fullStatusJson());
    } else if (cmd == "AP" || cmd == "CONFIG") {
        // Force AP mode (keeps STA if already connected)
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                          IPAddress(255, 255, 255, 0));
        WiFi.softAP(apSsidStr.c_str(), AP_AP_PASSWORD);
        apModeActive = true;
        startBridgeServices();
        if (fromTcp) sendTcpLine("@BPC OK ap=" + apSsidStr + " pwd=" AP_AP_PASSWORD);
    } else if (cmd == "CLEAR") {
        apClearWifi();
        if (fromTcp) sendTcpLine("@BPC OK clear");
        scheduleRestart(500);
    } else if (cmd.startsWith("NAME ")) {
        apSaveDeviceName(cmd.substring(5));
        if (fromTcp) sendTcpLine("@BPC OK name=" + apGetDeviceName());
    } else if (cmd.startsWith("WIFI ")) {
        int split = cmd.indexOf(' ', 5);
        if (split > 5) {
            String ssid = cmd.substring(5, split);
            String pass = cmd.substring(split + 1);
            apSaveWifi(ssid, pass);
            if (fromTcp) sendTcpLine("@BPC OK wifi");
            scheduleRestart(1000);
        } else if (fromTcp) {
            sendTcpLine("@BPC ERR wifi_args");
        }
    } else if (cmd.startsWith("TOPIC ")) {
        apSaveTopic(cmd.substring(6));
        if (fromTcp) sendTcpLine("@BPC OK topic=" + apDeviceTopic);
    } else if (cmd.startsWith("OTA ")) {
        if (fromTcp) sendTcpLine("@BPC OK ota");
        WiFiClient otaClient;
        ESPhttpUpdate.update(otaClient, cmd.substring(4));
    } else if (cmd == "RESTART" || cmd == "REBOOT") {
        if (fromTcp) sendTcpLine("@BPC OK restart");
        scheduleRestart(300);
    } else if (fromTcp) {
        sendTcpLine("@BPC ERR unknown");
    }
}

static bool controlPrefixMatches(const char *buf, uint16_t len) {
    const char prefix[] = "@BPC";
    const size_t plen = strlen(prefix);
    // 已经完整匹配 "@BPC" 前缀后，后面的字节都是控制行参数，必须继续判定为控制行。
    // 注意：能走到 len > plen 说明前 plen 个字节都逐一匹配过（否则早已返回 false 并转发），
    // 因此这里必须返回 true，否则 "@BPC PING" 这类命令会被误判成普通数据转发到串口，
    // 导致模块永不回复 @BPC PONG / @BPC OK，上位机握手失败。
    if ((size_t)len > plen) return true;
    for (uint16_t i = 0; i < len; i++) {
        if (buf[i] != prefix[i]) return false;
    }
    return true;
}

/* ---- 透传批量写出缓冲 ----------------------------------------------------
 * 原实现每收到一个字节就单独调用 Serial.write(byte) / bridgeClient.write(byte)。
 * bridgeClient 开了 setNoDelay(true)(TCP_NODELAY)，等于每字节一个独立 TCP 包，
 * 40+ 字节的 IP/TCP 头远大于 1 字节载荷，WiFi 侧吞吐被严重拖垮；Serial.write
 * 逐字节调用同样有大量函数开销。
 * 改为：控制行判定仍然逐字节进行（语义与字节顺序完全不变），但确定要透传的字节
 * 先攒进批量缓冲，缓冲满或在每轮 handleBridge() 末尾整段写出。
 * 顺序保证：所有直接写 bridgeClient / Serial 的地方（控制行刷新、sendTcpLine）
 * 都先冲刷对应批量缓冲，避免乱序。 */
#define BRIDGE_BATCH  128
static uint8_t  serialOut[BRIDGE_BATCH];   /* Serial(设备) → TCP(上位机) */
static uint16_t serialOutLen = 0;
static uint8_t  tcpOut[BRIDGE_BATCH];      /* TCP(上位机) → Serial(设备) */
static uint16_t tcpOutLen = 0;

static void serialBatchFlush() {
    if (serialOutLen > 0) {
        if (bridgeClient && bridgeClient.connected()) {
            bridgeClient.write((const uint8_t *)serialOut, serialOutLen);
            bridgeLastActivity = millis();
        }
        serialOutLen = 0;
    }
}

static void tcpBatchFlush() {
    if (tcpOutLen > 0) {
        Serial.write((const uint8_t *)tcpOut, tcpOutLen);
        tcpOutLen = 0;
    }
}

static void serialBatchPush(uint8_t byte) {
    serialOut[serialOutLen++] = byte;
    if (serialOutLen >= (uint16_t)sizeof(serialOut)) serialBatchFlush();
}

static void tcpBatchPush(uint8_t byte) {
    tcpOut[tcpOutLen++] = byte;
    if (tcpOutLen >= (uint16_t)sizeof(tcpOut)) tcpBatchFlush();
}

static void flushSerialControlToTcp() {
    serialBatchFlush();   /* 先冲刷已攒的透传字节，保证字节顺序 */
    if (bridgeClient && bridgeClient.connected() && serialCtlLen > 0) {
        bridgeClient.write((const uint8_t *)serialCtl, serialCtlLen);
    }
    serialCtlLen = 0;
    serialMaybeControl = false;
}

static void feedSerialByte(uint8_t byte) {
    if (serialSkipLf) {
        serialSkipLf = false;
        if (byte == '\n') return;
    }
    if (serialLineStart && byte == '@') {
        serialMaybeControl = true;
        serialCtlLen = 0;
    }

    if (serialMaybeControl) {
        if (serialCtlLen < CONTROL_MAX - 1) {
            serialCtl[serialCtlLen++] = (char)byte;
            serialCtl[serialCtlLen] = '\0';
        } else {
            flushSerialControlToTcp();
            serialBatchPush(byte);
            serialLineStart = (byte == '\n' || byte == '\r');
            return;
        }
        if (!controlPrefixMatches(serialCtl, serialCtlLen)) {
            flushSerialControlToTcp();
        } else if (byte == '\n' || byte == '\r') {
            handleControlLine(String(serialCtl), false);
            serialCtlLen = 0;
            serialMaybeControl = false;
            serialSkipLf = (byte == '\r');
        }
    } else {
        serialBatchPush(byte);
    }
    serialLineStart = (byte == '\n' || byte == '\r');
}

static void flushTcpControlToSerial() {
    tcpBatchFlush();   /* 先冲刷已攒的透传字节，保证字节顺序 */
    if (tcpCtlLen > 0) Serial.write((const uint8_t *)tcpCtl, tcpCtlLen);
    tcpCtlLen = 0;
    tcpMaybeControl = false;
}

static void feedTcpByte(uint8_t byte) {
    if (tcpSkipLf) {
        tcpSkipLf = false;
        if (byte == '\n') return;
    }
    if (tcpLineStart && byte == '@') {
        tcpMaybeControl = true;
        tcpCtlLen = 0;
    }

    if (tcpMaybeControl) {
        if (tcpCtlLen < CONTROL_MAX - 1) {
            tcpCtl[tcpCtlLen++] = (char)byte;
            tcpCtl[tcpCtlLen] = '\0';
        } else {
            flushTcpControlToSerial();
            tcpBatchPush(byte);
            tcpLineStart = (byte == '\n' || byte == '\r');
            return;
        }
        if (!controlPrefixMatches(tcpCtl, tcpCtlLen)) {
            flushTcpControlToSerial();
        } else if (byte == '\n' || byte == '\r') {
            handleControlLine(String(tcpCtl), true);
            tcpCtlLen = 0;
            tcpMaybeControl = false;
            tcpSkipLf = (byte == '\r');
        }
    } else {
        tcpBatchPush(byte);
    }
    tcpLineStart = (byte == '\n' || byte == '\r');
}

/* ============================================================
 *  Bridge: accept new TCP client + pass bytes between UART & TCP
 * ============================================================ */
static void handleBridge() {
    if (bridgeServer.hasClient()) {
        WiFiClient incoming = bridgeServer.available();
        if (bridgeClient && bridgeClient.connected()) {
            bridgeClient.stop();
        }
        bridgeClient = incoming;
        bridgeClient.setNoDelay(true);
        bridgeLastActivity = millis();
        sendTcpLine("@BPC CONNECTED " + apGetDeviceName());
    }

    while (Serial.available()) {
        feedSerialByte((uint8_t)Serial.read());
    }
    if (bridgeClient && bridgeClient.connected()) {
        while (bridgeClient.available()) {
            feedTcpByte((uint8_t)bridgeClient.read());
        }
        bridgeLastActivity = millis();   /* 收到任意桥接数据 = 活跃 */
    }

    /* 本轮透传字节整段写出：把逐字节 write 聚合成少量大包。
     * 放在两个读循环之后，保证本轮已到达的字节不会滞留到下一轮 loop()（低延迟）。 */
    serialBatchFlush();
    tcpBatchFlush();

    /* 空闲超时：PC 闪退/断网未发 FIN 时，半开连接会长期占用唯一连接槽，
     * 导致 PC 重连困难。超过 BRIDGE_IDLE_TIMEOUT_MS 无流量即主动断开。 */
    if (bridgeClient && bridgeClient.connected() &&
        (unsigned long)(millis() - bridgeLastActivity) > BRIDGE_IDLE_TIMEOUT_MS) {
        bridgeClient.stop();
        bridgeLastActivity = 0;
    }
}

/* ============================================================
 *  WiFi state machine (STA retry when disconnected)
 * ============================================================ */
static void handleWifi() {
    if (!apModeActive && WiFi.status() == WL_CONNECTED) {
        startBridgeServices();
        return;
    }
    // retry STA every WIFI_RETRY_MS if we have saved credentials & are not in pure AP boot
    if (apHasSavedWifi() && !apModeActive && millis() - lastWifiRetry > WIFI_RETRY_MS) {
        WiFi.begin(apGetSavedSsid().c_str(), apGetSavedPass().c_str());
        lastWifiRetry = millis();
    }
}

/* ============================================================
 *  apConfigSetup() — Ban-IOT style boot flow
 *    Returns: true = STA connected, false = AP fallback mode active
 * ============================================================ */
static bool apConfigSetup() {
    EEPROM.begin(EEPROM_BYTES);

    /* --------------------------------------------------------------
     * Legacy EEPROM migration: original wireless.ino stored:
     *   bytes 0..3  : CONFIG_MAGIC (uint32 LE = 0x54435042 → bytes "BPC T")
     *   bytes 4..35 : device name
     *   bytes 36..99: SSID (64 bytes was allocated — up to 99)
     *   bytes 100..163: password (64 bytes — up to 163)
     *
     * New Ban-IOT layout puts magic byte at 0 (0xAA). If we see the
     * uint32 LE legacy marker at bytes 0..3 we migrate the three fields
     * to their new addresses then write the 0xAA marker.
     * -------------------------------------------------------------- */
    {
        uint8_t b0 = EEPROM.read(0);
        uint8_t b1 = EEPROM.read(1);
        uint8_t b2 = EEPROM.read(2);
        uint8_t b3 = EEPROM.read(3);
        bool hasLegacyMagic = (b0 == 0x42 && b1 == 0x50 && b2 == 0x43 && b3 == 0x54); // "BPC T" LE uint32
        if (hasLegacyMagic && !apHasSavedWifi()) {
            Serial.println("[AP] Migrating legacy EEPROM layout → Ban-IOT format");
            String legacyName;
            String legacySsid;
            String legacyPass;
            for (int i = 0; i < 32; i++) {
                uint8_t b = EEPROM.read(4 + i);
                if (b == 0 || b == 0xFF) break;
                legacyName += (char)b;
            }
            for (int i = 0; i < 64; i++) {
                uint8_t b = EEPROM.read(36 + i);
                if (b == 0 || b == 0xFF) break;
                legacySsid += (char)b;
            }
            for (int i = 0; i < 64; i++) {
                uint8_t b = EEPROM.read(100 + i);
                if (b == 0 || b == 0xFF) break;
                legacyPass += (char)b;
            }
            Serial.println("[AP] legacy name=" + legacyName + " ssid=" + legacySsid);
            if (legacySsid.length() > 0) apSaveWifi(legacySsid, legacyPass);
            if (legacyName.length() > 0) apSaveDeviceName(legacyName);
            Serial.println("[AP] Migration complete.");
        }
    }

    // Generate AP SSID from chipId (BanPCBTool + 4 hex digits)
    uint32_t chipId = ESP.getChipId();
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%04X", DEVICE_DEFAULT_NAME,
             (unsigned int)(chipId & 0xFFFF));
    apSsidStr = String(buf);

    Serial.println("[AP] fallback SSID: " + apSsidStr + "  PWD: " AP_AP_PASSWORD);

    apDeviceTopic = apGetSavedTopic();

    if (apHasSavedWifi()) {
        String ssid = apGetSavedSsid();
        String pass = apGetSavedPass();
        Serial.println("[AP] Saved WiFi: " + ssid);

        WiFi.mode(WIFI_STA);

        // Apply static IP if configured (not 0/0xFF)
        IPAddress staticIp = apGetStaticIp();
        if (!(staticIp[0] == 0 || staticIp[0] == 0xFF)) {
            IPAddress gateway(staticIp[0], staticIp[1], staticIp[2], 1);
            IPAddress subnet(255, 255, 255, 0);
            WiFi.config(staticIp, gateway, subnet);
            Serial.println("[AP] Static IP: " + staticIp.toString());
        }

        WiFi.begin(ssid.c_str(), pass.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < AP_CONNECT_TIMEOUT) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[AP] STA Connected! IP: " + WiFi.localIP().toString());
            Serial.println("[AP] Web: http://" + WiFi.localIP().toString() + "/");
            setupHttp();
            startBridgeServices();
            lastWifiRetry = millis();
            return true;
        }
        Serial.println("\n[AP] STA connection failed → falling back to AP mode");
    }

    // Fallback: pure AP mode
    apModeActive = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(apSsidStr.c_str(), AP_AP_PASSWORD);

    setupHttp();
    startBridgeServices();

    Serial.println("[AP] AP Mode started");
    Serial.println("[AP] IP: " + WiFi.softAPIP().toString());
    Serial.println("[AP] Open http://" + WiFi.softAPIP().toString() + "/");
    return false;
}

/* ============================================================
 *  setup / loop
 * ============================================================ */
void setup() {
    Serial.begin(UART_BAUD);
    Serial.setRxBufferSize(1024);
    apConfigSetup();
}

void loop() {
    httpServer.handleClient();
    handleWifi();
    handleDiscovery();
    handleBridge();
    if (rebootPending && (int32_t)(millis() - rebootAt) >= 0) {
        Serial.println("[SYS] Restarting...");
        ESP.restart();
    }
}

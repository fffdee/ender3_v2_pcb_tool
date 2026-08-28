/*
 * mylight.ino
 *
 * 硬件：ESP8266 + 3个舵机
 *   舵机1 (GPIO0) — 大灯
 *   舵机2 (GPIO2) — 氛围灯1
 *   舵机3 (GPIO3) — 氛围灯2
 *
 * 话题：
 *   TOPIC_LIGHT    (mainlight002) — 大灯
 *   TOPIC_AMBIENT  (seclight002)  — 氛围灯1
 *   TOPIC_AMBIENT2 (seclight002c) — 氛围灯2
 *
 * 标准灯协议（每个灯独立 ON/OFF，使用同一个舵机）：
 *   大灯   ON : 舵机1 → 180° → 90°
 *   大灯   OFF: 舵机1 → 0°   → 90°
 *   氛围灯1 ON : 舵机2 → 20° → 170° → 90°
 *   氛围灯1 OFF: 舵机2 → 20° → 90°  → 170° (反向)
 *   氛围灯2 ON : 舵机3 → 20° → 170° → 90°
 *   氛围灯2 OFF: 舵机3 → 20° → 90°  → 170° (反向)
 */

#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <Servo.h>
#include "ap_config.h"

// ---- Bemfa 服务器 ----
#define server_ip   "bemfa.com"
#define server_port "8344"

String UID            = "c03b0385d6468c31245bd9f86236fc4d";
String TOPIC_LIGHT    = "mainlight002";   // 大灯话题
String TOPIC_AMBIENT  = "seclight002";    // 氛围灯1话题
String TOPIC_AMBIENT2 = "seclight002c";   // 氛围灯2话题

// ---- 舵机引脚 ----
#define SERVO1_PIN 0   // 大灯舵机
#define SERVO2_PIN 2   // 氛围灯1舵机
#define SERVO3_PIN 3   // 氛围灯2舵机

// 舵机动作间隔（毫秒）
#define SERVO_STEP_MS 500

Servo servo1;
Servo servo2;
Servo servo3;

// ---- TCP ----
#define MAX_PACKETSIZE  512
#define KEEPALIVEATIME  (30 * 1000)

WiFiClient TCPclient;
String TcpClient_Buff = "";
unsigned int TcpClient_BuffIndex = 0;
unsigned long TcpClient_preTick  = 0;
unsigned long preHeartTick        = 0;
unsigned long preTCPStartTick     = 0;
bool preTCPConnected              = false;

// ====================================================================
// 灯控制函数
// ====================================================================

// 大灯 ON：舵机1 → 180° → 90°
void lightOn() {
    servo1.write(180);
    delay(SERVO_STEP_MS);
    servo1.write(90);
}

// 大灯 OFF：舵机1 → 0° → 90°
void lightOff() {
    servo1.write(0);
    delay(SERVO_STEP_MS);
    servo1.write(90);
}

// 氛围灯1 ON：舵机2 → 20° → 170° → 90°
void ambientOn() {
    servo2.write(20);
    delay(SERVO_STEP_MS);
    servo2.write(170);
    delay(SERVO_STEP_MS);
    servo2.write(90);
}

// 氛围灯1 OFF：舵机2 → 20° → 90° → 170° (反向动作)
void ambientOff() {
    servo2.write(20);
    delay(SERVO_STEP_MS);
    servo2.write(90);
    delay(SERVO_STEP_MS);
    servo2.write(170);
}

// 氛围灯2 ON：舵机3 → 20° → 170° → 90°
void ambient2On() {
    servo3.write(20);
    delay(SERVO_STEP_MS);
    servo3.write(170);
    delay(SERVO_STEP_MS);
    servo3.write(90);
}

// 氛围灯2 OFF：舵机3 → 20° → 90° → 170° (反向动作)
void ambient2Off() {
    servo3.write(20);
    delay(SERVO_STEP_MS);
    servo3.write(90);
    delay(SERVO_STEP_MS);
    servo3.write(170);
}

// ====================================================================
// TCP / WiFi
// ====================================================================

void sendtoTCPServer(String p) {
    if (!TCPclient.connected()) return;
    TCPclient.print(p);
    preHeartTick = millis();
}

void startTCPClient() {
    // 先尝试域名，失败则直连 IP（应对 ESP8266 DNS 解析失败）
    const char* ip_fallback = "119.91.109.180";
    bool connected = TCPclient.connect(server_ip, atoi(server_port));
    if (!connected) {
        DBG_PRINTF("DNS connect failed, trying IP %s:%s\r\n", ip_fallback, server_port);
        connected = TCPclient.connect(ip_fallback, atoi(server_port));
    }
    if (connected) {
        sendtoTCPServer("cmd=1&uid=" + UID + "&topic=" + TOPIC_LIGHT + "\r\n");
        delay(100);
        sendtoTCPServer("cmd=1&uid=" + UID + "&topic=" + TOPIC_AMBIENT + "\r\n");
        delay(100);
        sendtoTCPServer("cmd=1&uid=" + UID + "&topic=" + TOPIC_AMBIENT2 + "\r\n");
        preTCPConnected = true;
        TCPclient.setNoDelay(true);
    } else {
        TCPclient.stop();
        preTCPConnected = false;
    }
    preTCPStartTick = millis();
}

void startSTA() {
    if (apHasSavedWifi()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(apGetSavedSsid().c_str(), apGetSavedPass().c_str());
    }
}

void doWiFiTick() {
    static bool startSTAFlag = false;
    static bool taskStarted  = false;
    static uint32_t lastWiFiCheckTick = 0;

    if (!startSTAFlag) {
        startSTAFlag = true;
        startSTA();
    }

    if (WiFi.status() != WL_CONNECTED) {
        if (millis() - lastWiFiCheckTick > 1000) {
            lastWiFiCheckTick = millis();
        }
    } else {
        if (!taskStarted) {
            taskStarted = true;
            startTCPClient();
        }
    }
}

// 处理一条完整的 Bemfa 消息（已去掉 \r\n 的单行文本）
void handleTCPMessage(const String& line) {
    if (line.length() <= 15) return;
    int topicIdx = line.indexOf("&topic=");
    int msgIdx   = line.indexOf("&msg=");
    if (topicIdx < 0 || msgIdx <= topicIdx) return;

    String getTopic = line.substring(topicIdx + 7, msgIdx);
    String getMsg   = line.substring(msgIdx + 5);
    DBG_PRINTLN("[TCP] topic=" + getTopic + " msg=" + getMsg);

    // ---- 大灯话题 ----
    if (getTopic == TOPIC_LIGHT) {
        if      (getMsg == "on")  lightOn();
        else if (getMsg == "off") lightOff();
    }
    // ---- 氛围灯1话题 ----
    else if (getTopic == TOPIC_AMBIENT) {
        if      (getMsg == "on")  ambientOn();
        else if (getMsg == "off") ambientOff();
    }
    // ---- 氛围灯2话题 ----
    else if (getTopic == TOPIC_AMBIENT2) {
        if      (getMsg == "on")  ambient2On();
        else if (getMsg == "off") ambient2Off();
    }
    // ---- 通用指令（任意话题均可触发）----
    if (getMsg == "config") {
        apClearWifi();
        delay(500);
        ESP.restart();
    }
}

void doTCPClientTick() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!TCPclient.connected()) {
        if (preTCPConnected) {
            preTCPConnected = false;
            preTCPStartTick = millis();
            TCPclient.stop();
        } else if (millis() - preTCPStartTick > 10 * 1000) {
            startTCPClient();
        }
        return;
    }

    // 读取所有可用字节，遇到 \n 立即处理当前行（支持缓冲区中多条消息并发）
    while (TCPclient.available()) {
        char c = TCPclient.read();
        TcpClient_preTick = millis();
        if (c == '\n') {
            TcpClient_Buff.trim();          // 去掉末尾 \r 及多余空白
            handleTCPMessage(TcpClient_Buff);
            TcpClient_Buff      = "";
            TcpClient_BuffIndex = 0;
        } else {
            TcpClient_Buff += c;
            TcpClient_BuffIndex++;
            if (TcpClient_BuffIndex >= MAX_PACKETSIZE - 1) {
                DBG_PRINTLN("[TCP] buffer overflow, cleared");
                TcpClient_Buff      = "";
                TcpClient_BuffIndex = 0;
            }
        }
    }

    if (millis() - preHeartTick >= KEEPALIVEATIME) {
        // 三个话题各自重发 cmd=1 以保持订阅活跃，避免其中一个掉线
        TCPclient.print("cmd=1&uid=" + UID + "&topic=" + TOPIC_LIGHT + "\r\n");
        delay(100);
        TCPclient.print("cmd=1&uid=" + UID + "&topic=" + TOPIC_AMBIENT + "\r\n");
        delay(100);
        TCPclient.print("cmd=1&uid=" + UID + "&topic=" + TOPIC_AMBIENT2 + "\r\n");
        preHeartTick = millis();
    }
}

// ====================================================================
// setup / loop
// ====================================================================

void setup() {
    // 先完成 WiFi / AP 初始化（此时舵机还未 attach，GPIO 安全）
    apSetDeviceInfo("mylight", TOPIC_LIGHT);
    bool staMode = apConfigSetup();  // 有已保存 WiFi 且连上→true，否则→false

    // WiFi 初始化完成后再 attach 舵机，避免 GPIO 状态干扰
    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);
    servo3.attach(SERVO3_PIN);
    servo1.write(90);
    servo2.write(90);
    servo3.write(90);

    String savedTopic = apGetSavedTopic();
    String savedTopic2 = apGetSavedTopic2();
    String savedTopic3 = apGetSavedTopic3();
    if (savedTopic.length() > 0)   TOPIC_LIGHT    = savedTopic;
    if (savedTopic2.length() > 0)  TOPIC_AMBIENT  = savedTopic2;
    if (savedTopic3.length() > 0)  TOPIC_AMBIENT2 = savedTopic3;

    // 注册舵机控制回调，供 ap_config.h 的 /control 端点调用
    apRegisterControlCallbacks(lightOn, lightOff,
                               ambientOn, ambientOff,
                               ambient2On, ambient2Off);
}

void loop() {
    apConfigLoop();
    if (isApMode()) return;
    doWiFiTick();
    doTCPClientTick();
}

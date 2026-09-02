#include <stdio.h>
#include <string.h>
#include "wireless_control.h"
#include "banux_config.h"
#include "banux_component.h"
#include "banux_io.h"
#include "bg_shell.h"

#define WIFI_UART_PATH "/driver/uart/uart3"
#define WIFI_CMD_MAX   192u          /* enough for ssid + pass + topic + uid */

/* -------------------------------------------------------------------------- */
/*  UART line helper                                                          */
/* -------------------------------------------------------------------------- */

static int wifi_send_line(const char *line)
{
    int ret;

    if (!line) return -1;
    ret = banux_write(WIFI_UART_PATH, line, (uint32_t)strlen(line));
    if (ret < 0) return ret;
    ret = banux_write(WIFI_UART_PATH, "\r\n", 2u);
    return ret < 0 ? ret : 0;
}

static int wifi_send_simple(const char *verb)
{
    char line[WIFI_CMD_MAX];

    if (!verb) return -1;
    snprintf(line, sizeof(line), "@BPC %s", verb);
    return wifi_send_line(line);
}

/* -------------------------------------------------------------------------- */
/*  Shell commands                                                            */
/* -------------------------------------------------------------------------- */

static int cmd_wifi_info(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = wifi_send_simple("STATUS");
    Shell_Print(ret == 0
        ? "wifi: status requested (reply @BPC {json} via UART3)\r\n"
        : "wifi: send failed\r\n");
    return ret;
}

/** Request ESP8266 to start the provisioning AP.
 *  Reply line (if sent back over TCP / UART):
 *    @BPC OK ap=BanPCBToolXXXX pwd=12345678
 */
static int cmd_wifi_config(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = wifi_send_simple("AP");
    Shell_Print(ret == 0
        ? "wifi: config AP requested\r\n"
          "  -> SSID prefix: BanPCBToolXXXX  password: 12345678  IP: 192.168.4.1\r\n"
          "  -> WebUI: http://192.168.4.1/  or  APP scan 'BanPCBTool*'\r\n"
        : "wifi: send failed\r\n");
    return ret;
}

/** Set WiFi credentials only. */
static int cmd_wifi_set(int argc, char *argv[])
{
    char line[WIFI_CMD_MAX];
    int ret;

    if (argc != 2) {
        Shell_Print("Usage: wifi -s <ssid> <password>\r\n");
        return -1;
    }
    if (strlen(argv[0]) + strlen(argv[1]) + 11u >= sizeof(line)) {
        Shell_Print("wifi: ssid/password too long\r\n");
        return -2;
    }
    snprintf(line, sizeof(line), "@BPC WIFI %s %s", argv[0], argv[1]);
    ret = wifi_send_line(line);
    Shell_Print(ret == 0
        ? "wifi: credentials sent -> ESP8266 will restart in ~1s\r\n"
        : "wifi: send failed\r\n");
    return ret;
}

/** Set device topic (cloud / discovery identifier). */
static int cmd_wifi_topic(int argc, char *argv[])
{
    char line[WIFI_CMD_MAX];
    int ret;

    if (argc != 1) {
        Shell_Print("Usage: wifi -t <topic>\r\n");
        return -1;
    }
    if (strlen(argv[0]) + 10u >= sizeof(line)) {
        Shell_Print("wifi: topic too long (max 32 chars)\r\n");
        return -2;
    }
    snprintf(line, sizeof(line), "@BPC TOPIC %s", argv[0]);
    ret = wifi_send_line(line);
    Shell_Print(ret == 0 ? "wifi: topic sent\r\n" : "wifi: send failed\r\n");
    return ret;
}

/** Set device friendly name. */
static int cmd_wifi_name(int argc, char *argv[])
{
    char line[WIFI_CMD_MAX];
    int ret;

    if (argc != 1) {
        Shell_Print("Usage: wifi -n <name>\r\n");
        return -1;
    }
    snprintf(line, sizeof(line), "@BPC NAME %s", argv[0]);
    ret = wifi_send_line(line);
    Shell_Print(ret == 0 ? "wifi: name update requested\r\n" : "wifi: send failed\r\n");
    return ret;
}

/** Clear saved WiFi + restart into AP mode (same as APP POST /reconfig). */
static int cmd_wifi_reconfig(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = wifi_send_simple("CLEAR");
    Shell_Print(ret == 0
        ? "wifi: saved WiFi cleared -> ESP8266 restarts into AP mode shortly\r\n"
          "  -> SSID: BanPCBToolXXXX  password: 12345678\r\n"
        : "wifi: send failed\r\n");
    return ret;
}

/** Legacy alias for reconfig — kept for backward compat. */
static int cmd_wifi_clear(int argc, char *argv[])
{
    return cmd_wifi_reconfig(argc, argv);
}

/** Restart ESP8266 module. */
static int cmd_wifi_restart(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = wifi_send_simple("RESTART");
    Shell_Print(ret == 0 ? "wifi: restart requested\r\n" : "wifi: send failed\r\n");
    return ret;
}

/** OTA update via URL. */
static int cmd_wifi_ota(int argc, char *argv[])
{
    char line[WIFI_CMD_MAX];
    int ret;

    if (argc != 1) {
        Shell_Print("Usage: wifi -u <firmware-url>\r\n");
        return -1;
    }
    if (strlen(argv[0]) + 10u >= sizeof(line)) {
        Shell_Print("wifi: url too long\r\n");
        return -2;
    }
    snprintf(line, sizeof(line), "@BPC OTA %s", argv[0]);
    ret = wifi_send_line(line);
    Shell_Print(ret == 0 ? "wifi: ota requested\r\n" : "wifi: send failed\r\n");
    return ret;
}

/** Provisioning helper: set ssid + password + topic in one command.
 *  Mirrors what the Ban-IOT APP sends via HTTP POST /config.
 */
static int cmd_wifi_provision(int argc, char *argv[])
{
    char line[WIFI_CMD_MAX];
    int ret;

    /* require ssid + pass; topic optional */
    if (argc < 2 || argc > 3) {
        Shell_Print("Usage: wifi -p <ssid> <password> [topic]\r\n"
                    "  -> ESP8266 saves credentials and restarts. Same flow as Ban-IOT APP.\r\n");
        return -1;
    }
    const char *ssid  = argv[0];
    const char *pass  = argv[1];
    const char *topic = (argc == 3) ? argv[2] : NULL;

    /* Step 1: WIFI */
    snprintf(line, sizeof(line), "@BPC WIFI %s %s", ssid, pass);
    ret = wifi_send_line(line);
    if (ret != 0) {
        Shell_Print("wifi: provision (WIFI) send failed\r\n");
        return ret;
    }

    /* Step 2: TOPIC (if provided) */
    if (topic && topic[0] != '\0') {
        /* allow a little time for the first cmd to be consumed; not strictly required
         * because the UART ringbuffer queues them, but be safe. */
        snprintf(line, sizeof(line), "@BPC TOPIC %s", topic);
        ret = wifi_send_line(line);
        if (ret != 0) {
            Shell_Print("wifi: provision (TOPIC) send failed\r\n");
            return ret;
        }
    }

    Shell_Print("wifi: provision sent -> ESP8266 will save + restart in ~1s\r\n"
                "  -> After restart, device connects to '" );
    Shell_Print(ssid);
    Shell_Print("'. Check with 'wifi -i' for status.\r\n");
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Help page                                                                 */
/* -------------------------------------------------------------------------- */

static int cmd_wifi_help(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell_Print(
        "ESP8266 WiFi bridge control — provisioning flow matches Ban-IOT APP:\r\n"
        "\r\n"
        "  Quick provisioning:\r\n"
        "    wifi -p <ssid> <pass> [topic]   One-shot config + restart (like APP)\r\n"
        "\r\n"
        "  AP provisioning mode:\r\n"
        "    wifi -c / --config              Start AP (SSID: BanPCBToolXXXX, PWD: 12345678)\r\n"
        "                                    -> Connect phone/PC, open http://192.168.4.1/\r\n"
        "                                    -> Or use Ban-IOT style APP scanning 'BanPCBTool*'\r\n"
        "\r\n"
        "  Individual settings:\r\n"
        "    wifi -s <ssid> <password>       Set WiFi credentials only\r\n"
        "    wifi -t <topic>                 Set MQTT / cloud topic (e.g. pcbtool001)\r\n"
        "    wifi -n <name>                  Set friendly device name\r\n"
        "\r\n"
        "  Maintenance:\r\n"
        "    wifi -i / --info                Request full status JSON\r\n"
        "    wifi -r / --restart             Restart ESP8266 module\r\n"
        "    wifi -x / --clear               Reset WiFi + restart into AP mode (= /reconfig)\r\n"
        "    wifi -u <url>                   OTA firmware from HTTP(S) URL\r\n"
        "\r\n"
        "  HTTP endpoints on ESP8266 (STA or AP network):\r\n"
        "    GET  /           Config web page (Ban-IOT themed)\r\n"
        "    POST /config     ssid=&pass=&topic=&static_ip=&uid=  -> save + reboot\r\n"
        "    GET  /status     Full status JSON (ap_mode/sta_mode/ip/topic/...)\r\n"
        "    GET  /scan       Nearby WiFi scan JSON\r\n"
        "    POST /reconfig   Clear saved WiFi + reboot to AP mode\r\n"
        "    POST /setip      static_ip=...  -> save + reboot\r\n"
        "    Legacy: /api/info, /api/wifi, /api/ap, /api/clear, /api/restart\r\n"
    );
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  Module registration                                                       */
/* -------------------------------------------------------------------------- */

static const ShellOpt_t s_wifiOptions[] = {
    OPT("p", "provision", "<ssid> <password> [topic]",
        "One-shot provision + restart (Ban-IOT APP flow)",      cmd_wifi_provision),
    OPT("c", "config",    NULL,
        "Enter ESP8266 config AP (SSID BanPCBToolXXXX pwd 12345678)", cmd_wifi_config),
    OPT("s", "set",       "<ssid> <password>",
        "Set ESP8266 WiFi credentials",                        cmd_wifi_set),
    OPT("t", "topic",     "<topic>",
        "Set cloud/MQTT topic (saved in EEPROM)",              cmd_wifi_topic),
    OPT("n", "name",      "<name>",
        "Set ESP8266 device name",                             cmd_wifi_name),
    OPT("i", "info",      NULL,
        "Request ESP8266 full status (@BPC STATUS)",           cmd_wifi_info),
    OPT("u", "upgrade",   "<url>",
        "Run ESP8266 OTA update from URL",                     cmd_wifi_ota),
    OPT("x", "clear",     NULL,
        "Clear WiFi config + restart AP mode (= /reconfig)",   cmd_wifi_clear),
    OPT("r", "restart",   NULL,
        "Restart ESP8266 module",                              cmd_wifi_restart),
    OPT("h", "help",      NULL,
        "Show this help",                                      cmd_wifi_help),
    OPT_END()
};

static const ShellModule_t s_wifiModule = {
    "wifi", "ESP8266 wireless bridge control (Ban-IOT provisioning)",
    MOD_CAT_SYSTEM,
    s_wifiOptions, OPT_COUNT(s_wifiOptions)
};

int WirelessControl_Init(void)
{
    return Shell_RegisterModule(&s_wifiModule) ? 0 : -1;
}

BANUX_COMPONENT_DEFINE_EX(g_banux_component_wireless_control,
                          "wireless_control", "1.1.0",
                          BANUX_COMPONENT_APPLICATION, BANUX_WIRELESS_CONTROL_EN,
                          "ESP8266 WiFi bridge control — Ban-IOT provisioning protocol",
                          WirelessControl_Init, NULL);

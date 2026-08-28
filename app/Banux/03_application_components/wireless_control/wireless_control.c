#include <stdio.h>
#include <string.h>
#include "wireless_control.h"
#include "banux_config.h"
#include "banux_component.h"
#include "banux_io.h"
#include "bg_shell.h"

#define WIFI_UART_PATH "/driver/uart/uart3"
#define WIFI_CMD_MAX   128u

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

static int cmd_wifi_info(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = wifi_send_simple("STATUS");
    Shell_Print(ret == 0 ? "wifi: status requested\r\n" : "wifi: send failed\r\n");
    return ret;
}

static int cmd_wifi_config(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = wifi_send_simple("AP");
    Shell_Print(ret == 0 ? "wifi: config AP requested\r\n" : "wifi: send failed\r\n");
    return ret;
}

static int cmd_wifi_clear(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = wifi_send_simple("CLEAR");
    Shell_Print(ret == 0 ? "wifi: saved WiFi cleared\r\n" : "wifi: send failed\r\n");
    return ret;
}

static int cmd_wifi_restart(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;
    ret = wifi_send_simple("RESTART");
    Shell_Print(ret == 0 ? "wifi: restart requested\r\n" : "wifi: send failed\r\n");
    return ret;
}

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
    Shell_Print(ret == 0 ? "wifi: credentials sent\r\n" : "wifi: send failed\r\n");
    return ret;
}

static const ShellOpt_t s_wifiOptions[] = {
    OPT("i", "info", NULL, "Request ESP8266 status", cmd_wifi_info),
    OPT("c", "config", NULL, "Enter ESP8266 config AP mode", cmd_wifi_config),
    OPT("s", "set", "<ssid> <password>", "Set ESP8266 WiFi credentials", cmd_wifi_set),
    OPT("n", "name", "<name>", "Set ESP8266 device name", cmd_wifi_name),
    OPT("u", "upgrade", "<url>", "Run ESP8266 OTA update from URL", cmd_wifi_ota),
    OPT("x", "clear", NULL, "Clear ESP8266 WiFi credentials", cmd_wifi_clear),
    OPT("r", "restart", NULL, "Restart ESP8266 module", cmd_wifi_restart),
    OPT_END()
};

static const ShellModule_t s_wifiModule = {
    "wifi", "ESP8266 wireless bridge control", MOD_CAT_SYSTEM,
    s_wifiOptions, OPT_COUNT(s_wifiOptions)
};

int WirelessControl_Init(void)
{
    return Shell_RegisterModule(&s_wifiModule) ? 0 : -1;
}

BANUX_COMPONENT_DEFINE_EX(g_banux_component_wireless_control,
                          "wireless_control", "1.0.0",
                          BANUX_COMPONENT_APPLICATION, BANUX_WIRELESS_CONTROL_EN,
                          "ESP8266 WiFi bridge control shell component",
                          WirelessControl_Init, NULL);

/**
 * @file main.c
 * @brief WiFi/Cellular Failover — MicroLink Network Switching Demo
 *
 * Connects via WiFi first. If WiFi fails, falls back to cellular (SIM7600).
 * When on cellular, periodically checks if WiFi has recovered and fails back.
 *
 * Hardware: XIAO ESP32S3 + Waveshare SIM7600X
 *   (or LILYGO T-SIM7670G-S3 — change board in sdkconfig.defaults)
 *
 * Credentials: All via menuconfig (git-ignored sdkconfig).
 *   See sdkconfig.credentials.example for template.
 *
 * Test failover:
 *   1. Flash and start — connects via WiFi
 *   2. Turn off WiFi router — after ~90s, switches to cellular
 *   3. Turn WiFi back on — after ~120s, fails back to WiFi
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "microlink.h"
#include "ml_net_switch.h"
#include "ml_config_httpd.h"     /* NVS WiFi override at boot */

static const char *TAG = "failover";

#define UDP_PORT CONFIG_ML_EXAMPLE_UDP_PORT

/* WiFi credentials — start with Kconfig defaults, NVS may override */
static char wifi_ssid[33]     = CONFIG_ML_WIFI_SSID;
static char wifi_password[65] = CONFIG_ML_WIFI_PASSWORD;

static void on_connected(ml_net_type_t type, void *user_data)
{
    ESP_LOGI(TAG, "=== CONNECTED via %s ===", type == ML_NET_WIFI ? "WiFi" : "Cellular");
}

static void on_disconnected(void *user_data)
{
    ESP_LOGW(TAG, "=== DISCONNECTED ===");
}

static void on_switching(ml_net_type_t from, ml_net_type_t to, void *user_data)
{
    ESP_LOGW(TAG, "=== SWITCHING: %s -> %s ===",
             from == ML_NET_WIFI ? "WiFi" : "Cellular",
             to == ML_NET_WIFI ? "WiFi" : "Cellular");
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    printf("\n");
    printf("================================================================\n");
    printf("  MicroLink WiFi/Cellular Failover Demo\n");
    printf("  WiFi: %s\n", CONFIG_ML_WIFI_SSID);
    printf("  Cellular: %s (APN: %s)\n",
#if defined(CONFIG_ML_BOARD_LILYGO_T_SIM7670G)
           "LILYGO T-SIM7670G-S3",
#else
           "Waveshare SIM7600X",
#endif
           CONFIG_ML_CELLULAR_APN);
    printf("  UDP Port: %d\n", UDP_PORT);
    printf("================================================================\n\n");

    /* Check NVS for saved WiFi credentials (from web config UI) */
    if (ml_config_get_nvs_wifi(wifi_ssid, sizeof(wifi_ssid),
                                wifi_password, sizeof(wifi_password))) {
        ESP_LOGI(TAG, "Using NVS WiFi: %s", wifi_ssid);
    } else {
        ESP_LOGI(TAG, "Using Kconfig WiFi: %s", wifi_ssid);
    }

    ml_net_switch_config_t config = {
        .wifi_ssid = wifi_ssid,
        .wifi_password = wifi_password,
        .sim_pin = CONFIG_ML_CELLULAR_SIM_PIN,
        .apn = CONFIG_ML_CELLULAR_APN,
        .tailscale_auth_key = CONFIG_ML_TAILSCALE_AUTH_KEY,
        .device_name = CONFIG_ML_DEVICE_NAME,
        .on_connected = on_connected,
        .on_disconnected = on_disconnected,
        .on_switching = on_switching,
    };

    ESP_ERROR_CHECK(ml_net_switch_init(&config));
    ESP_ERROR_CHECK(ml_net_switch_start());

    /* Wait for VPN connection */
    ESP_LOGI(TAG, "Waiting for VPN connection...");
    while (!ml_net_switch_get_handle() ||
           !microlink_is_connected(ml_net_switch_get_handle())) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "  %s", ml_net_switch_state_str(ml_net_switch_get_state()));
    }

    /* Create UDP socket */
    microlink_t *ml = ml_net_switch_get_handle();
    microlink_udp_socket_t *sock = microlink_udp_create(ml, UDP_PORT);
    if (!sock) {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return;
    }

    uint32_t vpn_ip = microlink_get_vpn_ip(ml);
    char ip_str[16];
    microlink_ip_to_str(vpn_ip, ip_str);
    ESP_LOGI(TAG, "Ready: %s:%d via %s",
             ip_str, UDP_PORT,
             ml_net_switch_get_active() == ML_NET_WIFI ? "WiFi" : "Cellular");

    uint32_t target_ip = 0;
#ifdef CONFIG_ML_EXAMPLE_TARGET_PEER_IP
    if (strlen(CONFIG_ML_EXAMPLE_TARGET_PEER_IP) > 0) {
        target_ip = microlink_parse_ip(CONFIG_ML_EXAMPLE_TARGET_PEER_IP);
    }
#endif

    uint32_t tx_count = 0, rx_count = 0;
    uint64_t last_status = 0, last_tx = 0;

    while (1) {
        uint64_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* RX */
        uint32_t src_ip;
        uint16_t src_port;
        uint8_t buf[256];
        size_t len = sizeof(buf) - 1;

        if (microlink_udp_recv(sock, &src_ip, &src_port, buf, &len, 0) == ESP_OK && len > 0) {
            buf[len] = '\0';
            char src[16];
            microlink_ip_to_str(src_ip, src);
            ESP_LOGI(TAG, "RX [%s:%u]: %s", src, src_port, buf);
            rx_count++;

            if (strncmp((char *)buf, "ping", 4) == 0) {
                const char *pong = "pong";
                microlink_udp_send(sock, src_ip, src_port, pong, strlen(pong));
            }
        }

        /* TX to target peer every 5s */
        if (target_ip && (now - last_tx >= 5000)) {
            last_tx = now;
            char msg[80];
            snprintf(msg, sizeof(msg), "failover-test #%lu via %s",
                     (unsigned long)tx_count++,
                     ml_net_switch_get_active() == ML_NET_WIFI ? "wifi" : "cell");
            microlink_udp_send(sock, target_ip, UDP_PORT, msg, strlen(msg));
        }

        /* Status every 10s */
        if (now - last_status >= 10000) {
            last_status = now;
            ESP_LOGI(TAG, "[%s] state=%s tx=%lu rx=%lu",
                     ml_net_switch_get_active() == ML_NET_WIFI ? "WiFi" : "Cell",
                     ml_net_switch_state_str(ml_net_switch_get_state()),
                     (unsigned long)tx_count, (unsigned long)rx_count);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

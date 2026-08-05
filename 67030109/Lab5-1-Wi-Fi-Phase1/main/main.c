#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "LAB_WIFI_SCAN";

// Convert wifi_auth_mode_t enum to readable string
static const char *get_auth_mode_name(wifi_auth_mode_t authmode) {
  switch (authmode) {
  case WIFI_AUTH_OPEN:
    return "OPEN (No Password)";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA_PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2_PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA_WPA2_PSK";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2_ENTERPRISE";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3_PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2_WPA3_PSK";
  case WIFI_AUTH_WAPI_PSK:
    return "WAPI_PSK";
  default:
    return "UNKNOWN";
  }
}

// Perform Wi-Fi scan and display detailed AP records
static void perform_wifi_scan(wifi_scan_config_t *scan_config,
                              const char *test_title, char *found_first_ssid,
                              size_t max_ssid_len) {
  ESP_LOGI(
      TAG,
      "------------------------------------------------------------------");
  ESP_LOGI(TAG, ">>> %s", test_title);
  ESP_LOGI(
      TAG,
      "------------------------------------------------------------------");

  ESP_LOGI(TAG,
           "[FORENSIC]: Call esp_wifi_scan_start(scan_config, block=true)");
  int64_t start_time = esp_timer_get_time();
  esp_err_t err = esp_wifi_scan_start(scan_config, true);
  int64_t duration_ms = (esp_timer_get_time() - start_time) / 1000;
  ESP_LOGI(TAG,
           "[FORENSIC]: esp_wifi_scan_start() returned %s (0x%x) [Duration: "
           "%lld ms]",
           esp_err_to_name(err), err, duration_ms);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "[STATUS]: Scan failed (Error: %s / Code: 0x%x)",
             esp_err_to_name(err), err);
    return;
  }

  uint16_t ap_count = 0;
  ESP_LOGI(TAG, "[FORENSIC]: Call esp_wifi_scan_get_ap_num(&ap_count)");
  esp_err_t err_ap_num = esp_wifi_scan_get_ap_num(&ap_count);
  ESP_LOGI(
      TAG,
      "[FORENSIC]: esp_wifi_scan_get_ap_num() returned %s (0x%x), ap_count=%u",
      esp_err_to_name(err_ap_num), err_ap_num, ap_count);

  ESP_LOGI(TAG, "[STATUS]: Scan SUCCESS");
  ESP_LOGI(TAG, "[AP COUNT]: %u network(s) found", ap_count);

  if (ap_count == 0) {
    ESP_LOGW(TAG, "[NOTE]: No Access Point found matching the criteria.");
  } else {
    wifi_ap_record_t *ap_info =
        (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_info == NULL) {
      ESP_LOGE(TAG, "Failed to allocate memory for AP scan records");
      return;
    }

    uint16_t number = ap_count;
    ESP_LOGI(TAG,
             "[FORENSIC]: Call esp_wifi_scan_get_ap_records(&number, ap_info)");
    esp_err_t err_ap_rec = esp_wifi_scan_get_ap_records(&number, ap_info);
    ESP_LOGI(TAG,
             "[FORENSIC]: esp_wifi_scan_get_ap_records() returned %s (0x%x), "
             "records=%u",
             esp_err_to_name(err_ap_rec), err_ap_rec, number);
    ESP_ERROR_CHECK(err_ap_rec);

    // Save first found SSID for targeted scan test
    if (found_first_ssid != NULL && number > 0) {
      snprintf(found_first_ssid, max_ssid_len, "%s", (char *)ap_info[0].ssid);
    }

    printf("\n-----------------------------------------------------------------"
           "---------------------------------\n");
    printf("%-4s | %-24s | %-17s | %-6s | %-4s | %-20s\n", "No.", "SSID",
           "MAC Address (BSSID)", "RSSI", "Chan", "Encryption Type");
    printf("-------------------------------------------------------------------"
           "-------------------------------\n");

    for (int i = 0; i < number; i++) {
      char bssid_str[18];
      snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
               ap_info[i].bssid[0], ap_info[i].bssid[1], ap_info[i].bssid[2],
               ap_info[i].bssid[3], ap_info[i].bssid[4], ap_info[i].bssid[5]);

      const char *ssid_display = (strlen((char *)ap_info[i].ssid) > 0)
                                     ? (char *)ap_info[i].ssid
                                     : "<Hidden SSID>";

      printf("%-4d | %-24s | %-17s | %-4d dBm | %-4d | %-20s\n", i + 1,
             ssid_display, bssid_str, ap_info[i].rssi, ap_info[i].primary,
             get_auth_mode_name(ap_info[i].authmode));
    }
    printf("-------------------------------------------------------------------"
           "-------------------------------\n\n");

    free(ap_info);
  }
}

void app_main(void) {
  // 1. Initialize NVS Flash (Required for Wi-Fi stack)
  ESP_LOGI(TAG, "[FORENSIC]: Call nvs_flash_init()");
  esp_err_t ret = nvs_flash_init();
  ESP_LOGI(TAG, "[FORENSIC]: nvs_flash_init() returned %s (0x%x)",
           esp_err_to_name(ret), ret);
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGI(TAG, "[FORENSIC]: Call nvs_flash_erase()");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
    ESP_LOGI(TAG, "[FORENSIC]: nvs_flash_init() retry returned %s (0x%x)",
             esp_err_to_name(ret), ret);
  }
  ESP_ERROR_CHECK(ret);

  // 2. Initialize Network Interface and Default Event Loop
  ESP_LOGI(TAG, "[FORENSIC]: Call esp_netif_init()");
  esp_err_t err_netif = esp_netif_init();
  ESP_LOGI(TAG, "[FORENSIC]: esp_netif_init() returned %s (0x%x)",
           esp_err_to_name(err_netif), err_netif);
  ESP_ERROR_CHECK(err_netif);

  ESP_LOGI(TAG, "[FORENSIC]: Call esp_event_loop_create_default()");
  esp_err_t err_event = esp_event_loop_create_default();
  ESP_LOGI(TAG,
           "[FORENSIC]: esp_event_loop_create_default() returned %s (0x%x)",
           esp_err_to_name(err_event), err_event);
  ESP_ERROR_CHECK(err_event);

  ESP_LOGI(TAG, "[FORENSIC]: Call esp_netif_create_default_wifi_sta()");
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  ESP_LOGI(
      TAG,
      "[FORENSIC]: esp_netif_create_default_wifi_sta() returned pointer %p",
      sta_netif);

  // 3. Initialize Wi-Fi Driver in Station Mode
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_LOGI(TAG, "[FORENSIC]: Call esp_wifi_init(&cfg)");
  esp_err_t err_wifi_init = esp_wifi_init(&cfg);
  ESP_LOGI(TAG, "[FORENSIC]: esp_wifi_init() returned %s (0x%x)",
           esp_err_to_name(err_wifi_init), err_wifi_init);
  ESP_ERROR_CHECK(err_wifi_init);

  ESP_LOGI(TAG, "[FORENSIC]: Call esp_wifi_set_mode(WIFI_MODE_STA)");
  esp_err_t err_mode = esp_wifi_set_mode(WIFI_MODE_STA);
  ESP_LOGI(TAG, "[FORENSIC]: esp_wifi_set_mode() returned %s (0x%x)",
           esp_err_to_name(err_mode), err_mode);
  ESP_ERROR_CHECK(err_mode);

  ESP_LOGI(TAG, "[FORENSIC]: Call esp_wifi_start()");
  esp_err_t err_start = esp_wifi_start();
  ESP_LOGI(TAG, "[FORENSIC]: esp_wifi_start() returned %s (0x%x)",
           esp_err_to_name(err_start), err_start);
  ESP_ERROR_CHECK(err_start);

  ESP_LOGI(
      TAG,
      "==================================================================");
  ESP_LOGI(TAG,
           "  Lab 5.1: Wi-Fi Connection and Scanning Phase (ESP-IDF Forensic)");
  ESP_LOGI(
      TAG,
      "==================================================================");

  char first_found_ssid[33] = "";

  // ------------------------------------------------------------------
  // 5.1.1 General AP Scan (All Channels & All SSIDs)
  // ------------------------------------------------------------------
  wifi_scan_config_t scan_config_all = {.ssid = NULL,
                                        .bssid = NULL,
                                        .channel =
                                            0, // 0 = Scan all channels (1-13)
                                        .show_hidden = true,
                                        .scan_type = WIFI_SCAN_TYPE_ACTIVE};
  perform_wifi_scan(&scan_config_all,
                    "Experiment 5.1.1: General AP Scan (All Channels)",
                    first_found_ssid, sizeof(first_found_ssid));

  vTaskDelay(pdMS_TO_TICKS(1000));

  // ------------------------------------------------------------------
  // 5.1.2 Channel-Specific Scan
  // ------------------------------------------------------------------
  uint8_t target_channel = 1; // Scan specifically on Channel 1
  char title_buf[128];
  snprintf(title_buf, sizeof(title_buf),
           "Experiment 5.1.2: Channel-Specific Scan (Channel %d)",
           target_channel);

  wifi_scan_config_t scan_config_chan = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = target_channel, // Scan specified channel only
      .show_hidden = true,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE};
  perform_wifi_scan(&scan_config_chan, title_buf, NULL, 0);

  vTaskDelay(pdMS_TO_TICKS(1000));

  // ------------------------------------------------------------------
  // 5.1.3 Targeted SSID Scan - Existing
  // ------------------------------------------------------------------
  const char *target_exist_ssid =
      (strlen(first_found_ssid) > 0) ? first_found_ssid : "WiFi-Test-Guest";
  snprintf(title_buf, sizeof(title_buf),
           "Experiment 5.1.3: Targeted SSID Scan - Existing (\"%s\")",
           target_exist_ssid);

  wifi_scan_config_t scan_config_exist = {.ssid = (uint8_t *)target_exist_ssid,
                                          .bssid = NULL,
                                          .channel = 0,
                                          .show_hidden = true,
                                          .scan_type = WIFI_SCAN_TYPE_ACTIVE};
  perform_wifi_scan(&scan_config_exist, title_buf, NULL, 0);

  vTaskDelay(pdMS_TO_TICKS(1000));

  // ------------------------------------------------------------------
  // 5.1.4 Targeted SSID Scan - Non-Existent
  // ------------------------------------------------------------------
  const char *dummy_ssid = "NON_EXISTENT_AP_9999";
  snprintf(title_buf, sizeof(title_buf),
           "Experiment 5.1.4: Targeted SSID Scan - Non-Existent (\"%s\")",
           dummy_ssid);

  wifi_scan_config_t scan_config_not_exist = {.ssid = (uint8_t *)dummy_ssid,
                                              .bssid = NULL,
                                              .channel = 0,
                                              .show_hidden = true,
                                              .scan_type =
                                                  WIFI_SCAN_TYPE_ACTIVE};
  perform_wifi_scan(&scan_config_not_exist, title_buf, NULL, 0);

  // Complete Scan Phase
  ESP_LOGI(
      TAG,
      "==================================================================");
  ESP_LOGI(TAG, "  [Phase 1 Completed: Wi-Fi Scan Finished]");
  ESP_LOGI(TAG,
           "  Program stopped after scanning. Auth/Assoc Phase not started.");
  ESP_LOGI(
      TAG,
      "==================================================================");
}
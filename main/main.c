
#include "driver/ledc.h"
#include "esp_camera.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <string.h>

#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 10
#define CAM_PIN_D2 8
#define CAM_PIN_D1 9
#define CAM_PIN_D0 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

static const char *TAG = "camera";
static bool wifi_connected = false;

static const char index_html[] =
    "<html><head><title>ESP32-S3 Camera</title></head>"
    "<body><img src=\"/stream\" style=\"width:100%;\"></body></html>";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    wifi_connected = true;
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "Disconnected from Wi-Fi, retrying...");
    esp_wifi_connect();
    wifi_connected = false;
  }
}

void wifi_init(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, NULL));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = CONFIG_CAMERA_WIFI_SSID,
              .password = CONFIG_CAMERA_WIFI_PASSWORD,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Wi-Fi started, connecting to SSID: %s",
           CONFIG_CAMERA_WIFI_SSID);
}

static framesize_t get_camera_frame_size() {
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "UXGA") == 0)
    return FRAMESIZE_UXGA;
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "SXGA") == 0)
    return FRAMESIZE_SXGA;
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "XGA") == 0)
    return FRAMESIZE_XGA;
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "SVGA") == 0)
    return FRAMESIZE_SVGA;
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "VGA") == 0)
    return FRAMESIZE_VGA;
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "CIF") == 0)
    return FRAMESIZE_CIF;
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "QVGA") == 0)
    return FRAMESIZE_QVGA;
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "HQVGA") == 0)
    return FRAMESIZE_HQVGA;
  if (strcmp(CONFIG_CAMERA_FRAME_SIZE, "QQVGA") == 0)
    return FRAMESIZE_QQVGA;
  return FRAMESIZE_SVGA;
}

esp_err_t camera_init(void) {
  camera_config_t config = {.pin_d0 = CAM_PIN_D0,
                            .pin_d1 = CAM_PIN_D1,
                            .pin_d2 = CAM_PIN_D2,
                            .pin_d3 = CAM_PIN_D3,
                            .pin_d4 = CAM_PIN_D4,
                            .pin_d5 = CAM_PIN_D5,
                            .pin_d6 = CAM_PIN_D6,
                            .pin_d7 = CAM_PIN_D7,
                            .pin_xclk = CAM_PIN_XCLK,
                            .pin_pclk = CAM_PIN_PCLK,
                            .pin_vsync = CAM_PIN_VSYNC,
                            .pin_href = CAM_PIN_HREF,
                            .pin_sccb_sda = CAM_PIN_SIOD,
                            .pin_sccb_scl = CAM_PIN_SIOC,
                            .pin_pwdn = -1,
                            .pin_reset = -1,
                            .xclk_freq_hz = 20000000,
                            .ledc_timer = LEDC_TIMER_0,
                            .ledc_channel = LEDC_CHANNEL_0,
                            .pixel_format = PIXFORMAT_JPEG,
                            .frame_size = get_camera_frame_size(),
                            .jpeg_quality = CONFIG_CAMERA_JPEG_QUALITY,
                            .fb_count = 1,
                            .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
                            .fb_location = CAMERA_FB_IN_PSRAM};

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed with error: 0x%x", err);
    return err;
  }

  ESP_LOGI(TAG, "Camera initialized: frame size: %s, quality: %d",
           CONFIG_CAMERA_FRAME_SIZE, CONFIG_CAMERA_JPEG_QUALITY);
  return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  char part_buf[64];

  ESP_LOGI(TAG, "Starting camera stream...");
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      ESP_LOGE(TAG, "Camera capture failed");
      return ESP_FAIL;
    }

    size_t hlen = snprintf(
        part_buf, sizeof(part_buf),
        "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);

    if (httpd_resp_send_chunk(req, "\r\n--frame\r\n", 12) != ESP_OK ||
        httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK ||
        httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(30));
  }

  return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_html, strlen(index_html));
}

void start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(
        server, &(httpd_uri_t){
                    .uri = "/", .method = HTTP_GET, .handler = index_handler});
    httpd_register_uri_handler(server,
                               &(httpd_uri_t){.uri = "/stream",
                                              .method = HTTP_GET,
                                              .handler = stream_handler});
    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
  } else {
    ESP_LOGE(TAG, "Failed to start HTTP server");
  }
}

void print_chip_info(void) {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  ESP_LOGI(TAG, "Chip: %s, Cores: %d, Revision: %d", CONFIG_IDF_TARGET,
           chip_info.cores, chip_info.revision);

  uint32_t flash_size = 0;
  if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
    ESP_LOGI(TAG, "Flash size: %luMB",
             (unsigned long)(flash_size / (1024 * 1024)));
  } else {
    ESP_LOGE(TAG, "Failed to get flash size");
  }

  size_t psram = esp_psram_get_size();
  if (esp_psram_is_initialized()) {
    ESP_LOGI(TAG, "PSRAM: %luMB", (unsigned long)(psram / (1024 * 1024)));
  } else {
    ESP_LOGE(TAG, "PSRAM NOT available or failed to init!");
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Booting ESP32-S3 Camera Module...");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  print_chip_info();
  wifi_init();

  for (int i = 0; i < 20 && !wifi_connected; ++i) {
    ESP_LOGI(TAG, "Waiting for Wi-Fi...");
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (!wifi_connected) {
    ESP_LOGE(TAG, "Wi-Fi connection failed");
    return;
  }

  ESP_ERROR_CHECK(camera_init());
  start_webserver();

  ESP_LOGI(TAG, "Setup complete! Access the camera stream at http://<IP>/");

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

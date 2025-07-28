/* main.c - Simple ESP32-S3 OV5640 Camera Stream
 * ESP-IDF v5.5 Compatible - Fixed Version
 */

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "camera";

// WiFi Configuration
#define WIFI_SSID "pixel"
#define WIFI_PASS "71111111"

// Camera GPIO pins
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

// Simple HTML page
static const char index_html[] =
    "<!DOCTYPE html><html><head><title>Camera</title></head>"
    "<body style='text-align:center; font-family:Arial;'>"
    "<h1>ESP32-S3 Camera</h1>"
    "<img src='/stream' style='max-width:100%; border:2px solid black;'>"
    "</body></html>";

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
  }
}

// Initialize WiFi
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
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

// Initialize camera
esp_err_t camera_init(void) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda =
      CAM_PIN_SIOD; // Fixed: pin_sccb_sda instead of pin_scc_sda
  config.pin_sccb_scl =
      CAM_PIN_SIOC; // Fixed: pin_sccb_scl instead of pin_scc_scl
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA; // 800x600
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
    return err;
  }
  ESP_LOGI(TAG, "Camera initialized successfully");
  return ESP_OK;
}

// Stream handler
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];

  res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      ESP_LOGE(TAG, "Camera capture failed");
      res = ESP_FAIL;
      break;
    }

    res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 10);
    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    size_t hlen = snprintf(
        part_buf, 64, "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
        fb->len);
    res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    esp_camera_fb_return(fb);
    fb = NULL;

    if (res != ESP_OK) {
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(30)); // Control frame rate
  }

  return res;
}

// Index handler
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_html, strlen(index_html));
}

// Start web server
void start_webserver(void) {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t index_uri = {
        .uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_register_uri_handler(server, &index_uri);

    httpd_uri_t stream_uri = {
        .uri = "/stream", .method = HTTP_GET, .handler = stream_handler};
    httpd_register_uri_handler(server, &stream_uri);

    ESP_LOGI(TAG, "Web server started on port 80");
  } else {
    ESP_LOGE(TAG, "Failed to start web server");
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Starting ESP32-S3 Camera...");

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize camera
  ESP_LOGI(TAG, "Initializing camera...");
  ESP_ERROR_CHECK(camera_init());

  // Initialize WiFi
  ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
  wifi_init();

  // Wait for WiFi connection
  vTaskDelay(pdMS_TO_TICKS(5000));

  // Start web server
  start_webserver();

  ESP_LOGI(TAG,
           "Setup complete! Open browser and navigate to ESP32's IP address");

  // Keep running
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

/* main.c - ESP32-S3 OV5640 Camera Stream with PSRAM Fix
 * ESP-IDF v5.5 Compatible - PSRAM Optimized Version
 */

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
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

// Camera GPIO pins for ESP32-S3
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
    "<!DOCTYPE html><html><head><title>ESP32-S3 Camera</title></head>"
    "<body style='text-align:center; font-family:Arial; background:#f0f0f0;'>"
    "<h1 style='color:#333;'>ESP32-S3 Camera Stream</h1>"
    "<div style='margin:20px;'>"
    "<img src='/stream' style='max-width:90%; border:3px solid #333; "
    "border-radius:10px; box-shadow:0 4px 8px rgba(0,0,0,0.3);'>"
    "</div>"
    "<p style='color:#666; font-size:14px;'>Real-time camera feed from "
    "ESP32-S3</p>"
    "</body></html>";

// Check PSRAM availability
void check_psram(void) {
  if (esp_psram_is_initialized()) {
    ESP_LOGI(TAG, "PSRAM initialized successfully");
    ESP_LOGI(TAG, "PSRAM size: %d bytes", esp_psram_get_size());
    ESP_LOGI(TAG, "Free PSRAM: %d bytes",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else {
    ESP_LOGE(TAG, "PSRAM not initialized! Check your configuration.");
  }

  // Print general memory info
  ESP_LOGI(TAG, "Free internal memory: %d bytes",
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  ESP_LOGI(TAG, "Total free memory: %d bytes", esp_get_free_heap_size());
}

// WiFi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    ESP_LOGI(TAG, "WiFi starting...");
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    ESP_LOGI(TAG, "WiFi connected");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "WiFi disconnected, attempting reconnect...");
    esp_wifi_connect();
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
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg = {.capable = true, .required = false},
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

// Initialize camera with PSRAM optimization
esp_err_t camera_init(void) {
  camera_config_t config;

  // Basic pin configuration
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
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = -1;
  config.pin_reset = -1;

  // Clock and format configuration
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Frame buffer configuration optimized for PSRAM
  if (esp_psram_is_initialized()) {
    ESP_LOGI(TAG, "PSRAM detected, using high quality settings");
    config.frame_size = FRAMESIZE_UXGA; // 1600x1200 for OV5640
    config.jpeg_quality = 10; // Better quality (lower number = better quality)
    config.fb_count = 2;      // Double buffering with PSRAM
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST; // Always get latest frame
  } else {
    ESP_LOGW(TAG, "PSRAM not available, using lower quality settings");
    config.frame_size = FRAMESIZE_SVGA; // 800x600
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
    return err;
  }

  // Get camera sensor for additional configuration
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != NULL) {
    // OV5640 specific optimizations
    sensor->set_brightness(sensor, 0);     // -2 to 2
    sensor->set_contrast(sensor, 0);       // -2 to 2
    sensor->set_saturation(sensor, 0);     // -2 to 2
    sensor->set_special_effect(sensor, 0); // 0 to 6 (0=normal)
    sensor->set_whitebal(sensor, 1);       // 0 = disable, 1 = enable
    sensor->set_awb_gain(sensor, 1);       // 0 = disable, 1 = enable
    sensor->set_wb_mode(sensor, 0);        // 0 to 4
    sensor->set_exposure_ctrl(sensor, 1);  // 0 = disable, 1 = enable
    sensor->set_aec2(sensor, 0);           // 0 = disable, 1 = enable
    sensor->set_ae_level(sensor, 0);       // -2 to 2
    sensor->set_aec_value(sensor, 300);    // 0 to 1200
    sensor->set_gain_ctrl(sensor, 1);      // 0 = disable, 1 = enable
    sensor->set_agc_gain(sensor, 0);       // 0 to 30
    sensor->set_gainceiling(sensor, (gainceiling_t)0); // 0 to 6
    sensor->set_bpc(sensor, 0);      // 0 = disable, 1 = enable
    sensor->set_wpc(sensor, 1);      // 0 = disable, 1 = enable
    sensor->set_raw_gma(sensor, 1);  // 0 = disable, 1 = enable
    sensor->set_lenc(sensor, 1);     // 0 = disable, 1 = enable
    sensor->set_hmirror(sensor, 0);  // 0 = disable, 1 = enable
    sensor->set_vflip(sensor, 0);    // 0 = disable, 1 = enable
    sensor->set_dcw(sensor, 1);      // 0 = disable, 1 = enable
    sensor->set_colorbar(sensor, 0); // 0 = disable, 1 = enable

    ESP_LOGI(TAG, "Camera sensor configured for OV5640");
  }

  ESP_LOGI(TAG, "Camera initialized successfully");
  return ESP_OK;
}

// Stream handler with error handling
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[128];

  ESP_LOGI(TAG, "Stream started from client: %s", req->uri);

  // Set response type for MJPEG stream
  res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  if (res != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set response type");
    return res;
  }

  // Add CORS headers for web compatibility
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "30");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      ESP_LOGE(TAG, "Camera capture failed");
      res = ESP_FAIL;
      break;
    }

    // Send frame boundary
    res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 10);
    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    // Send headers
    size_t hlen = snprintf(part_buf, sizeof(part_buf),
                           "Content-Type: image/jpeg\r\n"
                           "Content-Length: %u\r\n"
                           "X-Timestamp: %lld\r\n\r\n",
                           fb->len, esp_timer_get_time());

    res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    // Send image data
    res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    fb = NULL;

    if (res != ESP_OK) {
      break;
    }

    // Control frame rate (about 30 FPS)
    vTaskDelay(pdMS_TO_TICKS(33));
  }

  if (fb) {
    esp_camera_fb_return(fb);
  }

  ESP_LOGI(TAG, "Stream ended");
  return res;
}

// Index handler
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "identity");
  return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

// Status handler for debugging
static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t *sensor = esp_camera_sensor_get();
  char *sensor_name = "Unknown";
  if (sensor) {
    switch (sensor->id.PID) {
    case OV5640_PID:
      sensor_name = "OV5640";
      break;
    case OV2640_PID:
      sensor_name = "OV2640";
      break;
    case OV3660_PID:
      sensor_name = "OV3660";
      break;
    default:
      sensor_name = "Unknown";
      break;
    }
  }

  snprintf(json_response, sizeof(json_response),
           "{"
           "\"sensor\":\"%s\","
           "\"framesize\":\"%s\","
           "\"quality\":%d,"
           "\"brightness\":%d,"
           "\"contrast\":%d,"
           "\"saturation\":%d,"
           "\"psram_size\":%u,"
           "\"free_psram\":%u,"
           "\"free_heap\":%u"
           "}",
           sensor_name,
           "UXGA", // or get actual frame size
           sensor ? sensor->status.quality : 0,
           sensor ? sensor->status.brightness : 0,
           sensor ? sensor->status.contrast : 0,
           sensor ? sensor->status.saturation : 0,
           (unsigned int)esp_psram_get_size(),
           (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned int)esp_get_free_heap_size());

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
}

// Start web server
void start_webserver(void) {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  // Increase timeouts for better stability
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  config.task_priority = 5;
  config.stack_size = 8192;

  if (httpd_start(&server, &config) == ESP_OK) {
    // Root page
    httpd_uri_t index_uri = {.uri = "/",
                             .method = HTTP_GET,
                             .handler = index_handler,
                             .user_ctx = NULL};
    httpd_register_uri_handler(server, &index_uri);

    // Stream endpoint
    httpd_uri_t stream_uri = {.uri = "/stream",
                              .method = HTTP_GET,
                              .handler = stream_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &stream_uri);

    // Status endpoint for debugging
    httpd_uri_t status_uri = {.uri = "/status",
                              .method = HTTP_GET,
                              .handler = status_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &status_uri);

    ESP_LOGI(TAG, "Web server started on port 80");
    ESP_LOGI(TAG, "Available endpoints:");
    ESP_LOGI(TAG, "  http://[ESP32_IP]/       - Camera web interface");
    ESP_LOGI(TAG, "  http://[ESP32_IP]/stream - Raw MJPEG stream");
    ESP_LOGI(TAG, "  http://[ESP32_IP]/status - System status (JSON)");
  } else {
    ESP_LOGE(TAG, "Failed to start web server");
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Starting ESP32-S3 Camera Application...");

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Check PSRAM availability
  ESP_LOGI(TAG, "Checking PSRAM...");
  check_psram();

  // Initialize camera
  ESP_LOGI(TAG, "Initializing camera...");
  if (camera_init() != ESP_OK) {
    ESP_LOGE(TAG, "Camera initialization failed!");
    return;
  }

  // Initialize WiFi
  ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
  wifi_init();

  // Wait for WiFi connection
  ESP_LOGI(TAG, "Waiting for WiFi connection...");
  vTaskDelay(pdMS_TO_TICKS(10000));

  // Start web server
  start_webserver();

  ESP_LOGI(TAG, "Setup complete!");
  ESP_LOGI(TAG, "Open your browser and navigate to the ESP32's IP address");

  // Main loop with memory monitoring
  while (1) {
    if (esp_psram_is_initialized()) {
      ESP_LOGI(TAG, "Free PSRAM: %u bytes, Free heap: %u bytes",
               (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               (unsigned int)esp_get_free_heap_size());
    }
    vTaskDelay(pdMS_TO_TICKS(30000)); // Log every 30 seconds
  }
}

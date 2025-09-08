
// main.c
#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

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

static const char *TAG = "esp32cam";
static bool wifi_connected = false;
static uint32_t frame_counter = 0;
static uint32_t last_fps_time = 0;
static uint32_t fps_frame_count = 0;
static float current_fps = 0.0;

static const char index_html[] =
    "<html><head><title>ESP32-CAM</title></head>"
    "<body><img src=\"/stream\" style=\"width:100%;\"></body></html>";

//---------------- mDNS ----------------//
static void mdns_init_service(void) {
  // Chuẩn 5.x: bỏ tiền tố esp_
  ESP_ERROR_CHECK(mdns_init());
  ESP_ERROR_CHECK(mdns_hostname_set("esp32-cam"));
  ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Camera Stream"));
  ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
  ESP_LOGI(TAG, "mDNS started: esp32-cam.local");
}

//---------------- Wi-Fi ----------------//
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    wifi_connected = true;
    mdns_init_service();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
    wifi_connected = false;
    esp_wifi_connect();
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

  esp_log_level_set("wifi", ESP_LOG_WARN);
  ESP_LOGI(TAG, "Wi-Fi initialized, SSID: %s", CONFIG_CAMERA_WIFI_SSID);
}

//---------------- Camera ----------------//
static framesize_t get_camera_frame_size(void) {
#if CONFIG_CAMERA_FRAME_SIZE_UXGA
  return FRAMESIZE_UXGA;
#elif CONFIG_CAMERA_FRAME_SIZE_SXGA
  return FRAMESIZE_SXGA;
#elif CONFIG_CAMERA_FRAME_SIZE_XGA
  return FRAMESIZE_XGA;
#elif CONFIG_CAMERA_FRAME_SIZE_SVGA
  return FRAMESIZE_SVGA;
#elif CONFIG_CAMERA_FRAME_SIZE_VGA
  return FRAMESIZE_VGA;
#else
  return FRAMESIZE_SVGA;
#endif
}

static uint32_t get_timestamp_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static void calculate_fps(void) {
  fps_frame_count++;
  uint32_t now = get_timestamp_ms();
  if (now - last_fps_time >= 5000) {
    current_fps = (float)fps_frame_count * 1000.0 / (now - last_fps_time);
    fps_frame_count = 0;
    last_fps_time = now;
  }
}

static bool is_client_connected(httpd_req_t *req) {
  int sock = httpd_req_to_sockfd(req);
  int err = 0;
  socklen_t len = sizeof(err);
  return getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
}

esp_err_t camera_init(void) {
  camera_config_t config = {
      .pin_d0 = CAM_PIN_D0,
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
      .fb_count = 2,
      .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
      .fb_location = CAMERA_FB_IN_PSRAM,
  };
  return esp_camera_init(&config);
}

//---------------- HTTP Handlers ----------------//
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_html, strlen(index_html));
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb;
  char hdr_buf[128];
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  last_fps_time = get_timestamp_ms();

  while (true) {
    if (!is_client_connected(req))
      break;
    fb = esp_camera_fb_get();
    if (!fb)
      continue;
    frame_counter++;

    size_t hdr_len = snprintf(hdr_buf, sizeof(hdr_buf),
                              "Content-Type: image/jpeg\r\n"
                              "Content-Length: %u\r\n"
                              "X-Frame-Number: %" PRIu32 "\r\n"
                              "X-Timestamp: %" PRIu32 "\r\n"
                              "X-FPS: %.2f\r\n\r\n",
                              (unsigned int)fb->len, frame_counter,
                              get_timestamp_ms(), current_fps);

    if (httpd_resp_send_chunk(req, "\r\n--frame\r\n", 12) != ESP_OK ||
        httpd_resp_send_chunk(req, hdr_buf, hdr_len) != ESP_OK ||
        httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }
    esp_camera_fb_return(fb);
    calculate_fps();
    vTaskDelay(pdMS_TO_TICKS(CONFIG_CAMERA_STREAM_FRAME_INTERVAL));
  }
  return ESP_OK;
}

//---------------- Webserver ----------------//
void start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = 5;
  config.stack_size = 8192;
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(
        server, &(httpd_uri_t){
                    .uri = "/", .method = HTTP_GET, .handler = index_handler});
    httpd_register_uri_handler(server,
                               &(httpd_uri_t){.uri = "/stream",
                                              .method = HTTP_GET,
                                              .handler = stream_handler});
    ESP_LOGI(TAG, "HTTP server started");
  }
}

//---------------- app_main ----------------//
void app_main(void) {
  ESP_LOGI(TAG, "Starting ESP32-CAM stream...");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  wifi_init();

  for (int i = 0; i < 60 && !wifi_connected; i++) {
    ESP_LOGI(TAG, "Waiting for Wi-Fi... (%d/60)", i + 1);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  ESP_ERROR_CHECK(camera_init());
  start_webserver();

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    ESP_LOGI(TAG, "Frames sent: %" PRIu32 ", FPS: %.2f", frame_counter,
             current_fps);
  }
}

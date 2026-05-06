
idf.py add-dependency "espressif/mdns"
need to add component esp32-cam clone from espressif 
# ESP32-CAM High-Performance Video Streamer

Dự án triển khai một máy chủ truyền phát video (Video Streaming Server) hiệu năng cao sử dụng SoC ESP32 và module Camera (OV2640). Hệ thống được phát triển trên nền tảng **ESP-IDF**, tối ưu hóa cho độ trễ thấp và khả năng hoạt động ổn định trong môi trường mạng nội bộ.

## 🚀 Tính năng nổi bật
- **MJPEG Streaming:** Truyền phát luồng video trực tiếp qua giao diện Web với cơ chế multipart streaming.
- **mDNS Support:** Truy cập thiết bị không cần nhớ địa chỉ IP thông qua hostname `http://esp32-cam.local`.
- **FPS Monitoring:** Hệ thống tự động tính toán và hiển thị tốc độ khung hình (FPS) thời gian thực qua Serial Log và HTTP Header.
- **Resource Management:** Tối ưu hóa bộ đệm khung hình (Frame Buffer) trong **PSRAM**, giúp xử lý ổn định các độ phân giải cao như SVGA, XGA hoặc UXGA.
- **Connection Recovery:** Tự động giám sát và kết nối lại Wi-Fi khi gặp sự cố mất tín hiệu.

## 🛠 Công nghệ sử dụng
- **Ngôn ngữ:** C
- **Framework:** ESP-IDF (Espressif IoT Development Framework) v5.x
- **Hệ điều hành:** FreeRTOS (Xử lý đa tác vụ: Camera polling, Web serving, System monitoring)
- **Giao thức:** HTTP (Multipart/x-mixed-replace), mDNS, TCP/IP, SCCB/I2C (Camera control).

## 📋 Sơ đồ logic hệ thống
1. **Camera Driver:** Khởi tạo và thu thập dữ liệu JPEG từ cảm biến OV2640 thông qua giao tiếp song song 8-bit.
2. **HTTP Server:** Xử lý các request từ client, duy trì kết nối persistent cho luồng stream.
3. **Socket Monitoring:** Sử dụng `getsockopt` để kiểm tra trạng thái kết nối của client, giải phóng tài nguyên ngay khi client ngắt kết nối.
4. **mDNS Service:** Đăng ký dịch vụ HTTP trên mạng cục bộ.

## 🔧 Cấu hình chân (Pinout - AI-Thinker)
- **D0 - D7:** GPIO 11, 9, 8, 10, 12, 18, 17, 16
- **XCLK / PCLK:** GPIO 15 / GPIO 13
- **VSYNC / HREF:** GPIO 6 / GPIO 7
- **SDA (SIOD) / SCL (SIOC):** GPIO 4 / GPIO 5

## 📥 Hướng dẫn cài đặt & Chạy
1. **Thiết lập môi trường:** Cài đặt ESP-IDF SDK (v5.0 trở lên).
2. **Cấu hình:**
   ```bash
   idf.py menuconfig
idf.py build flash monitor ```

Truy cập: Mở trình duyệt và nhập http://esp32-cam.local

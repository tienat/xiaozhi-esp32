# Custom HCP ESP32-P4 Smart Panel Board for Xiaozhi

Board support cho bo mạch HCP Panel (ESP32-P4 + ESP32-C6) chạy trợ lý thông minh Xiaozhi.

## Tính năng Phần cứng
- **SoC**: ESP32-P4 (RISC-V dual-core 400MHz, 32MB PSRAM XIP 200MHz, 32MB Nor Flash, Rev v1.3).
- **Phân vùng Flash**: `partitions/v2/32m.csv` (16MB assets, 4MB ota_0, 4MB ota_1).
- **Wi-Fi**: ESP32-C6 qua SDIO 1 (`esp-hosted`).
- **Màn hình**: 4.0" ST7102 RGB565 480x480 (SPI 3-wire init bit-bang + RGB LCD).
- **m thanh**: Song công qua I2S0, DAC ES8311 (phát) + ADC ES7210 (thu 2 mic stereo), PA amp NS4150.
- **Đèn nền an toàn**: GPIO27 (Q4 AO3401, active-LOW). Không sử dụng GPIO12.

## Rào chắn Bảo vệ Phần cứng
1. **CẤM cấu hình GPIO12 làm output/PWM**: GPIO12 nối FB của boost AP3032.
2. **GPIO54 chỉ dùng reset C6**: Đường LCD R4 nối vào GPIO49.
3. **LDO_VO4 (3.3V)**: Bắt buộc kích hoạt để cấp nguồn cho miền GPIO39-48.

## Hướng dẫn Build với ESP-IDF v6.1

```bash
# 1. Gắn biến môi trường ESP-IDF (bắt buộc)
. ./esp-idf/export.sh

# 2. Chuyển vào thư mục xiaozhi
cd firmware/xiaozhi-esp32

# 3. Chọn target và board
idf.py set-target esp32p4
# Trong menuconfig: Xiaozhi Assistant -> Target Board -> Custom HCP ESP32-P4 Smart Panel
idf.py menuconfig

# 4. Build firmware
idf.py build
```

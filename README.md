# Secure Gateway AES-STM32

**Hệ thống Secure Gateway mã hóa dữ liệu IP thời gian thực sử dụng STM32F103.**

Dự án này biến vi điều khiển STM32F103C8T6 thành một cổng kết nối bảo mật lớp 2/3, có khả năng tiếp nhận, mã hóa Payload bằng thuật toán **AES-128-CBC** và chuyển tiếp gói tin IP/UDP theo thời gian thực.

---

## Kiến trúc Hệ thống (Architecture)

Hệ thống được xây dựng trên nền tảng **FreeRTOS** với kiến trúc đa nhiệm và tối ưu hóa tài nguyên phần cứng giới hạn (20KB RAM).

### Các Quyết định Kiến trúc Cốt lõi (ADRs):
* **ADR-01: Sử dụng FreeRTOS:** Điều phối 6 tác vụ (Tasks) để quản lý đồng thời luồng nhận, xử lý mã hóa và truyền dữ liệu.
* **ADR-02: Cấp phát bộ nhớ tĩnh (Static Buffer Pool):** Loại bỏ hoàn toàn `malloc`/`free` để triệt tiêu rủi ro phân mảnh RAM.
* **ADR-03: Kỹ thuật Zero-copy IPC:** Chỉ truyền địa chỉ con trỏ của gói tin qua các Queue, giúp tiết kiệm chu kỳ CPU và giảm độ trễ.

### Luồng dữ liệu (Data Flow):
`ENC28J60 (RX)` -> `SPI + DMA` -> `Static Buffer Pool` -> `AES-128 Processing` -> `xTX_Queue` -> `SPI + DMA` -> `ENC28J60 (TX)`

---

## Tính năng chính

* **Xử lý mạng:** Nhận gói tin Ethernet thô, bóc tách IP/UDP Header, định tuyến tĩnh giữa 2 phân vùng mạng.
* **Bảo mật:** Mã hóa Payload UDP bằng AES-128-CBC với cơ chế đệm PKCS#7.
* **Độ tin cậy:** Giám sát đa nhiệm bằng **Independent Watchdog (IWDG)** và cơ chế tự phục hồi lỗi kết nối vật lý (Link Status).

---

## Cấu hình Phần cứng (Hardware)

| Thành phần | Chi tiết |
| :--- | :--- |
| **MCU** | STM32F103C8T6 (Cortex-M3, 72MHz) |
| **Network Controller** | 2 x ENC28J60 (Sử dụng 2 bộ SPI độc lập) |
| **Giao tiếp** | SPI1 (Master), SPI2 (Master) - Clock 18MHz |
| **Nguồn cấp** | 5V DC (chuyển đổi nội bộ sang 3.3V cho IC) |

**Sơ đồ kết nối chân (Pinout):**
* **SPI1:** SCK (PA5), MISO (PA6), MOSI (PA7), NSS (PA4), INT (PA0), RST (PA2).
* **SPI2:** SCK (PB13), MISO (PB14), MOSI (PB15), NSS (PB12), INT (PA1), RST (PA3).

---

## Phân bổ Tài nguyên thực tế

Dựa trên dữ liệu biên dịch thực tế từ dự án:

* **RAM (~17.35 KB / 20 KB):**
  * Static Buffer Pool: 6.0 KB.
  * Task Stacks: 6.38 KB.
  * FreeRTOS & HAL Variables: ~4.97 KB.

* **Flash (~33.75 KB / 64 KB):**
  * FreeRTOS Kernel: 8.56 KB.
  * STM32 HAL Drivers: 9.44 KB.
  * Tiny-AES & Logic: 5.24 KB.
  * ENC28J60 Driver: 7.07 KB.

---

## Hướng dẫn Sử dụng

### 1. Yêu cầu môi trường
* **Phần mềm:** STM32CubeIDE (v1.12.0 trở lên).
* **Thư viện:** Tiny-AES-C (Đã tích hợp trong mã nguồn).
* **Công cụ Test:** Script Python và phần mềm Wireshark để bắt gói tin.

### 2. Triển khai
1. Clone repository: `git clone https://github.com/TIENVUZ-tech/Secure-Gateway-AES-STM32.git`
2. Mở dự án trong **STM32CubeIDE**.
3. Cấu hình bảng địa chỉ IP và Key AES trong file `main.c` (biến `key_table`).
4. Biên dịch và nạp firmware cho board STM32F103C8T6.

### 3. Kiểm thử (Testing)
Sử dụng script Python đi kèm để bắn gói tin UDP 512 bytes vào cổng mạng của Gateway và kiểm tra bản mã bắt được trên thiết bị đích bằng Wireshark.

---

## Tác giả

* **Vũ Văn Tiến** - *Sinh viên Đại học Công nghệ (UET), VNU*.

---

## Tài liệu

Dự án được xây dựng phục vụ mục đích nghiên cứu học thuật. Các chi tiết thiết kế chuyên sâu có thể tham khảo tại:
* **[SRS]** Đặc tả yêu cầu phần mềm.
* **[SAD]** Đặc tả kiến trúc phần mềm.
* **[SDD]** Đặc tả thiết kế chi tiết.

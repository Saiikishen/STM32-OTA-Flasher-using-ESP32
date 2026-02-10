#include <WiFi.h>
#include <HTTPClient.h>

const char* WIFI_SSID = "WIFI_SSID_HERE";
const char* WIFI_PASS = "WIFI_PASSWROD_HERE";
const char* FW_URL = "URL_OF_.BIN";

#define STM32_BOOT0 25
#define STM32_NRST  26
#define RX_PIN 16
#define TX_PIN 17
#define ACK  0x79
#define NACK 0x1F
#define STM32_FLASH_BASE 0x08000000
#define WRITE_BLOCK_SIZE 128  

HardwareSerial STM32_UART(2);

#define MAX_FIRMWARE_SIZE 100000
uint8_t *firmwareBuffer = nullptr;
int firmwareSize = 0;

void clear_uart_buffer() {
  while (STM32_UART.available()) STM32_UART.read();
}

bool stm32_wait_ack(uint32_t timeout = 1000) {
  uint32_t start = millis();
  while (millis() - start < timeout) {
    if (STM32_UART.available()) {
      uint8_t r = STM32_UART.read();
      if (r == ACK) return true;
      if (r == NACK) return false;
    }
  }
  return false;
}

void stm32_enter_bootloader() {
  pinMode(STM32_BOOT0, OUTPUT);
  pinMode(STM32_NRST, OUTPUT);
  digitalWrite(STM32_BOOT0, LOW);
  digitalWrite(STM32_NRST, LOW);
  delay(200);
  digitalWrite(STM32_BOOT0, HIGH);
  delay(200);
  digitalWrite(STM32_NRST, HIGH);
  delay(1000);
}

bool stm32_sync() {
  clear_uart_buffer();
  for (int i = 0; i < 5; i++) {
    STM32_UART.write(0x7F);
    STM32_UART.flush();
    if (stm32_wait_ack(500)) return true;
    delay(100);
  }
  return false;
}

bool stm32_extended_erase() {
  Serial.print("Erasing flash... ");
  clear_uart_buffer();
  
  STM32_UART.write(0x44);
  STM32_UART.write(0xBB);
  
  if (!stm32_wait_ack(1000)) {
    Serial.println("Command rejected");
    return false;
  }

  STM32_UART.write(0xFF);
  STM32_UART.write(0xFF);
  STM32_UART.write(0x00);
  

  if (!stm32_wait_ack(40000)) {
    Serial.println("Erase timeout");
    return false;
  }
  
  Serial.println("Stabilizing ...");
  delay(3000);
  return true;
}

bool stm32_write_chunk(uint32_t addr, uint8_t *data, uint8_t len) {
  if (len == 0 || len > 256) return false;

  clear_uart_buffer();

  STM32_UART.write(0x31);
  STM32_UART.write(0xCE);
  STM32_UART.flush();
  
  if (!stm32_wait_ack(500)) { Serial.print("C"); return false; }

  uint8_t addr_bytes[4] = {
    (uint8_t)(addr >> 24), (uint8_t)(addr >> 16),
    (uint8_t)(addr >> 8), (uint8_t)(addr)
  };
  uint8_t addr_cs = addr_bytes[0] ^ addr_bytes[1] ^ addr_bytes[2] ^ addr_bytes[3];
  
  STM32_UART.write(addr_bytes, 4);
  STM32_UART.write(addr_cs);
  STM32_UART.flush();
  
  if (!stm32_wait_ack(500)) { Serial.print("A"); return false; }

  uint8_t N = len - 1;
  uint8_t data_cs = N;
  
  STM32_UART.write(N);
  for (int i = 0; i < len; i++) {
    STM32_UART.write(data[i]);
    data_cs ^= data[i];
  }
  STM32_UART.write(data_cs);
  STM32_UART.flush();
  
  if (!stm32_wait_ack(2000)) { Serial.print("D"); return false; }
  
  return true;
}

void stm32_boot_app() {
  Serial.println("\nBooting app...");
  digitalWrite(STM32_BOOT0, LOW);
  delay(100);
  digitalWrite(STM32_NRST, LOW);
  delay(100);
  digitalWrite(STM32_NRST, HIGH);
  delay(200);
}

bool download_firmware() {
  HTTPClient http;
  http.begin(FW_URL);
  
  Serial.print("Downloading... ");
  int code = http.GET();
  if (code != 200) {
    Serial.printf(" HTTP %d\n", code);
    return false;
  }

  firmwareSize = http.getSize();
  if (firmwareSize <= 0 || firmwareSize > MAX_FIRMWARE_SIZE) {
    Serial.printf("size bad: %d\n", firmwareSize);
    return false;
  }

  if (firmwareBuffer) free(firmwareBuffer);
  firmwareBuffer = (uint8_t*)malloc(firmwareSize);
  
  WiFiClient *stream = http.getStreamPtr();
  int read = 0;
  while (http.connected() && read < firmwareSize) {
    if (stream->available()) {
      read += stream->readBytes(firmwareBuffer + read, firmwareSize - read);
    }
  }
  http.end();
  
  Serial.printf("%d bytes \n", read);
  return (read == firmwareSize);
}

bool flash_firmware() {
  if (!firmwareBuffer) return false;

  uint32_t addr = STM32_FLASH_BASE;
  int written = 0;
  uint8_t chunk[WRITE_BLOCK_SIZE];
  
  Serial.println("Flashing:");
  
  while (written < firmwareSize) {
    int remaining = firmwareSize - written;
    int len = min(remaining, (int)WRITE_BLOCK_SIZE);
    
    memset(chunk, 0xFF, WRITE_BLOCK_SIZE);
    memcpy(chunk, firmwareBuffer + written, len);
    
    int alignedLen = (len % 4 == 0) ? len : ((len / 4 + 1) * 4);

    bool success = false;
    for (int retry = 0; retry < 3; retry++) {
      if (stm32_write_chunk(addr, chunk, alignedLen)) {
        success = true;
        break;
      }
      Serial.print("."); 
      delay(100);
    }
    
    if (!success) {
      Serial.printf("\nFailed at 0x%08X\n", addr);
      return false;
    }
    
    addr += alignedLen;
    written += len;
    
    if (written % 1024 == 0) Serial.print("#");
  }
  
  Serial.println("\n✓ Flash Complete");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println(" WiFi OK");
  
  if (!download_firmware()) return;
  
  STM32_UART.begin(115200, SERIAL_8E1, RX_PIN, TX_PIN);
  delay(100);
  
  stm32_enter_bootloader();
  
  if (!stm32_sync()) {
    Serial.println("Sync failed");
    return;
  }
  Serial.println("Sync OK");
  
  if (!stm32_extended_erase()) return;
  
  if (flash_firmware()) {
    stm32_boot_app();
    Serial.println("Success!");
  } else {
    Serial.println("Failure!");
  }
  
  if (firmwareBuffer) free(firmwareBuffer);
}

void loop() {}

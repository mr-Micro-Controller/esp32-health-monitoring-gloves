#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <MPU6050.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "esp_wifi.h"

#define FIXED_CHANNEL 6   // 🔧 Same as receiver

#define HEART_RATE_TASK_STACK 4096
#define HEART_RATE_TASK_PRIORITY 2
#define TTP223_PIN 4
#define ONE_WIRE_BUS 5

MAX30105 particleSensor;
MPU6050 mpu;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define mpuTrigger 1000

volatile long irValue = 0;
volatile float beatsPerMinute = 0.0;
volatile int beatAvg = 0;

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE] = {0};
byte rateSpot = 0;
volatile long lastBeat = 0;

unsigned long lastTempSend = 0;
const unsigned long tempInterval = 10000; 

typedef struct struct_message {
  int16_t ax;
  int16_t ay;
  int16_t az;
  long ir;
  float bpm;
  int avgBpm;
  float tempC;
} struct_message;

struct_message myData;

esp_now_peer_info_t peerInfo;
uint8_t broadcastAddress[] = {0x24, 0xDC, 0xC3, 0x45, 0x89, 0xB8};

void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void heartRateTask(void * parameter) {
  while (1) {
    long currentIr = particleSensor.getIR();
    irValue = currentIr;

    if (currentIr > 50000) {
      if (checkForBeat(currentIr)) {
        long delta = millis() - lastBeat;
        lastBeat = millis();
        float currentBPM = 60.0 / (delta / 1000.0);

        if (currentBPM > 20 && currentBPM < 255) {
          rates[rateSpot++] = (byte)currentBPM;
          rateSpot %= RATE_SIZE;

          int sum = 0;
          for (byte i = 0; i < RATE_SIZE; i++) sum += rates[i];
          beatAvg = sum / RATE_SIZE;
          beatsPerMinute = currentBPM;
        }
      }
    } else {
      beatsPerMinute = 0;
      beatAvg = 0;
      rateSpot = 0;
      memset((void*)rates, 0, RATE_SIZE);
    }
    delay(30);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(TTP223_PIN, INPUT);

  // MPU setup
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed!");
    while (1);
  }

  // MAX30102 setup
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 not found. Check wiring/power.");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x02);
  particleSensor.setPulseAmplitudeGreen(0);

  // DS18B20 setup
  sensors.begin();

  // Wi-Fi + ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  WiFi.setSleep(false);
  delay(100);

  // 🔧 Force fixed channel
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (1);
  }
  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = FIXED_CHANNEL;   
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    while (1);
  }

  xTaskCreatePinnedToCore(heartRateTask, "HeartRateTask", HEART_RATE_TASK_STACK, NULL, HEART_RATE_TASK_PRIORITY, NULL, 0);

  Serial.printf("Transmitter ready on fixed channel %d\n", FIXED_CHANNEL);
}

void loop() {
  unsigned long currentMillis = millis();
  bool sendData = false;

  noInterrupts();
  long currentIr = irValue;
  float currentBPM = beatsPerMinute;
  int currentAvgBPM = beatAvg;
  interrupts();

  bool fingerDetected = (currentIr > 50000);

  if (fingerDetected) {
    myData.ir = currentIr;
    myData.bpm = currentBPM;
    myData.avgBpm = currentAvgBPM;
    sendData = true;
  }

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  bool touchActive = (digitalRead(TTP223_PIN) == HIGH);
  bool motionTrigger = (ax < -mpuTrigger || ax > mpuTrigger || ay < -mpuTrigger || ay > mpuTrigger);
  if (touchActive && motionTrigger) {
    myData.ax = ax;
    myData.ay = ay;
    myData.az = az;
    sendData = true;
  }
    
  if (currentMillis - lastTempSend >= tempInterval) {
    lastTempSend = currentMillis;
    sensors.requestTemperatures();
    myData.tempC = sensors.getTempCByIndex(0);
    sendData = true;
  }

  if (sendData) {
    Serial.printf("Send: IR=%ld BPM=%.1f Avg=%d AX=%d AY=%d AZ=%d Temp=%.2f\n",
                  myData.ir, myData.bpm, myData.avgBpm, myData.ax, myData.ay, myData.az, myData.tempC);
    esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
  }

  delay(50);
}

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <MPU6050.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#define TTP223_PIN 4
#define ONE_WIRE_BUS 5
#define trigger 3000

MAX30105 particleSensor;
MPU6050 mpu;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define mpuTrigger 1500

volatile float beatsPerMinute = 0.0;
volatile int beatAvg = 0;

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE] = {0};
byte rateSpot = 0;
volatile long lastBeat = 0;

unsigned long lastTempSend = 0;
const unsigned long tempInterval = 10000;

// Reduced struct (removed IR and AZ)
typedef struct struct_message {
  uint8_t ap;
  float bpm;
  int avgBpm;
  float tempC;
} struct_message;

struct_message myData;

WiFiUDP udp;
const char *ssid = "ESP32_AP";
const char *password = "12345678";
const char *receiverIP = "192.168.4.1";
const int udpPort = 3333;

void heartRateTask(void * parameter) {
  while (1) {
    long currentIr = particleSensor.getIR();

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

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed!");
    while (1);
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 not found. Check wiring/power.");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x02);
  particleSensor.setPulseAmplitudeGreen(0);

  sensors.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to receiver AP");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Receiver AP!");
  Serial.print("Transmitter IP: ");
  Serial.println(WiFi.localIP());

  xTaskCreatePinnedToCore(heartRateTask, "HeartRateTask", 4096, NULL, 2, NULL, 0);
}

void loop() {
  unsigned long currentMillis = millis();
  bool sendData = false;

  noInterrupts();
  float currentBPM = beatsPerMinute;
  int currentAvgBPM = beatAvg;
  interrupts();

  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  bool touchActive = (digitalRead(TTP223_PIN) == HIGH);
  bool motionTrigger = (ax < -mpuTrigger || ax > mpuTrigger || ay < -mpuTrigger || ay > mpuTrigger);
  if (touchActive && motionTrigger) {
    (ax>trigger) ? myData.ap|=(1<<0):myData.ap&=~(1<<0);
    (ax<-trigger) ? myData.ap|=(1<<1):myData.ap&=~(1<<1);
    (ay>trigger) ? myData.ap|=(1<<2):myData.ap&=~(1<<2);
    (ay<-trigger) ? myData.ap|=(1<<3):myData.ap&=~(1<<3);
    sendData = true;

  }

  if (currentMillis - lastTempSend >= tempInterval) {
    lastTempSend = currentMillis;
    sensors.requestTemperatures();
    myData.tempC = sensors.getTempCByIndex(0);
    sendData = true;
  }

  if (currentBPM > 0) {
    myData.bpm = currentBPM;
    myData.avgBpm = currentAvgBPM;
    sendData = true;
  }

  if (sendData) {
    Serial.printf("Send: BPM=%.1f Avg=%d Ap=%d Temp=%.2f\n",
                  myData.bpm, myData.avgBpm,myData.ap, myData.tempC);

    udp.beginPacket(receiverIP, udpPort);
    udp.write((uint8_t *)&myData, sizeof(myData));
    udp.endPacket();
  }

  delay(50);
}

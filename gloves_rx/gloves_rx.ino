#define BLYNK_TEMPLATE_ID "TMPL4SFGHDLPu"
#define BLYNK_TEMPLATE_NAME "health"
#define BLYNK_AUTH_TOKEN "nnvak4cF2B0mYWjzFIktYcQGq3qiz963"

char ssid[] = "Eglvoes";
char pass[] = "11223344";

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <esp_now.h>
#include "esp_wifi.h"

#define FIXED_CHANNEL 6  // Fixed ESPNOW channel
#define trigger 2500
#define TEMP_THRESHOLD 32  
unsigned long lastTempAlert = 0;  
const unsigned long tempInterval = 120000; 

unsigned long lastBlynkSend = 0;
const unsigned long blynkInterval = 120000; // 2 minutes

byte ap1 = 5;
byte ap2 = 17;
byte ap3 = 18;
byte ap4 = 19;

// LED states
bool ledState1 = false;
bool ledState2 = false;
bool ledState3 = false;
bool ledState4 = false;

// Previous state for edge detection
bool prevAbove1 = false;
bool prevAbove2 = false;
bool prevAbove3 = false;
bool prevAbove4 = false;

// Structure must match transmitter
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

// Flag to indicate new data has arrived
volatile bool newDataFlag = false;

// Previous Blynk-sent values
float prevTemp = -100;     
float prevBPM = -1;
int prevAvgBPM = -1;

// ESP-NOW receive callback
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    memcpy(&myData, incomingData, sizeof(myData));
    newDataFlag = true; 
    Serial.printf("ESP-NOW Received: IR=%ld BPM=%.1f Avg=%d AX=%d AY=%d AZ=%d Temp=%.2f\n",
                  myData.ir, myData.bpm, myData.avgBpm,
                  myData.ax, myData.ay, myData.az, myData.tempC);
}

// Blynk handlers for manual LED control
BLYNK_WRITE(V4) { ledState1 = param.asInt(); digitalWrite(ap1, ledState1); }
BLYNK_WRITE(V5) { ledState2 = param.asInt(); digitalWrite(ap2, ledState2); }
BLYNK_WRITE(V6) { ledState3 = param.asInt(); digitalWrite(ap3, ledState3); }
BLYNK_WRITE(V7) { ledState4 = param.asInt(); digitalWrite(ap4, ledState4); }

void setup() {
    Serial.begin(115200);

    pinMode(ap1, OUTPUT);
    pinMode(ap2, OUTPUT);
    pinMode(ap3, OUTPUT);
    pinMode(ap4, OUTPUT);

    Serial.println("Starting receiver");

    // Wi-Fi STA mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);

    // Fixed ESPNOW channel
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // Init ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        while (1);
    }
    esp_now_register_recv_cb(OnDataRecv);

    Serial.printf("ESP-NOW Receiver ready on channel %d\n", FIXED_CHANNEL);

    // Blynk manual connect
    Blynk.config(BLYNK_AUTH_TOKEN);
    WiFi.begin(ssid, pass);
}

void loop() {
    unsigned long now = millis();

    // Handle Blynk periodically
    if (now - lastBlynkSend >= blynkInterval) {
        lastBlynkSend = now;

        // Only send if data has changed
        if (myData.tempC != prevTemp) {
            Blynk.virtualWrite(V1, myData.tempC);
            prevTemp = myData.tempC;
        }
        if (myData.bpm != prevBPM) {
            Blynk.virtualWrite(V2, myData.bpm);
            prevBPM = myData.bpm;
        }
        if (myData.avgBpm != prevAvgBPM) {
            Blynk.virtualWrite(V3, myData.avgBpm);
            prevAvgBPM = myData.avgBpm;
        }

        // Send current LED states
        Blynk.virtualWrite(V4, ledState1);
        Blynk.virtualWrite(V5, ledState2);
        Blynk.virtualWrite(V6, ledState3);
        Blynk.virtualWrite(V7, ledState4);
    }

    Blynk.run(); // keep connection alive

    // Edge detection for MPU-triggered LED toggling
    bool above1 = (myData.ax > trigger);
    if (above1 && !prevAbove1) {
        ledState1 = !ledState1;
        digitalWrite(ap1, ledState1);
        Blynk.virtualWrite(V4, ledState1); // immediate update
    }
    prevAbove1 = above1;

    bool above2 = (myData.ax < -trigger);
    if (above2 && !prevAbove2) {
        ledState2 = !ledState2;
        digitalWrite(ap2, ledState2);
        Blynk.virtualWrite(V5, ledState2);
    }
    prevAbove2 = above2;

    bool above3 = (myData.ay > trigger);
    if (above3 && !prevAbove3) {
        ledState3 = !ledState3;
        digitalWrite(ap3, ledState3);
        Blynk.virtualWrite(V6, ledState3);
    }
    prevAbove3 = above3;

    bool above4 = (myData.ay < -trigger);
    if (above4 && !prevAbove4) {
        ledState4 = !ledState4;
        digitalWrite(ap4, ledState4);
        Blynk.virtualWrite(V7, ledState4);
    }
    prevAbove4 = above4;

    // Temperature alert every 2 minutes
    if (now - lastTempAlert >= tempInterval) {
        if (myData.tempC >= TEMP_THRESHOLD) {
            Blynk.virtualWrite(V8, 1); 
        } else {
            Blynk.virtualWrite(V8, 0); 
        }
        lastTempAlert = now;
    }
}

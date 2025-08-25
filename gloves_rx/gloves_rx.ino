
#define BLYNK_TEMPLATE_ID "TMPL4SFGHDLPu"
#define BLYNK_TEMPLATE_NAME "health"
#define BLYNK_AUTH_TOKEN "cF2B0mYWjzFIktYcQGq3qiz963"

char ssid[] = "Deepak Verma";     
char pass[] = "1122334";

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <BlynkSimpleEsp32.h>

WiFiUDP udp;
#define UDP_PORT 3333

const char* apSSID = "ESP32_AP";
const char* apPASS = "12345678";

#define trigger 2500
#define TEMP_THRESHOLD 32  
unsigned long lastTempAlert = 0;  
const unsigned long tempInterval = 120000; 

unsigned long lastBlynkSend = 0;
const unsigned long blynkInterval = 120000;

#define LED_PIN    14
#define LED_COUNT  4
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

byte ap1 = 4, ap2 = 17, ap3 = 18, ap4 = 19;
bool ledState1 = false, ledState2 = false, ledState3 = false, ledState4 = false;
bool prevAbove1 = false, prevAbove2 = false, prevAbove3 = false, prevAbove4 = false;

// Reduced struct (no IR, no AZ)
typedef struct struct_message {
  uint8_t ap;
  float bpm;
  int avgBpm;
  float tempC;
} struct_message;
struct_message myData;

float prevTemp = -100;     
float prevBPM = -1;
int prevAvgBPM = -1;

void updateLeds() {
    if (digitalRead(ap1)) strip.setPixelColor(0, strip.Color(0, 255, 0)); 
    else strip.setPixelColor(0, strip.Color(255, 0, 0));
    if (digitalRead(ap2)) strip.setPixelColor(1, strip.Color(0, 255, 0)); 
    else strip.setPixelColor(1, strip.Color(255, 0, 0));
    if (digitalRead(ap3)) strip.setPixelColor(2, strip.Color(0, 255, 0)); 
    else strip.setPixelColor(2, strip.Color(255, 0, 0));
    if (digitalRead(ap4)) strip.setPixelColor(3, strip.Color(0, 255, 0)); 
    else strip.setPixelColor(3, strip.Color(255, 0, 0));
    strip.setBrightness(50);
    strip.show();
}

BLYNK_WRITE(V4) { ledState1 = param.asInt(); digitalWrite(ap1, ledState1); }
BLYNK_WRITE(V5) { ledState2 = param.asInt(); digitalWrite(ap2, ledState2); }
BLYNK_WRITE(V6) { ledState3 = param.asInt(); digitalWrite(ap3, ledState3); }
BLYNK_WRITE(V7) { ledState4 = param.asInt(); digitalWrite(ap4, ledState4); }

void setup() {
    Serial.begin(115200);

    pinMode(ap1, OUTPUT); pinMode(ap2, OUTPUT);
    pinMode(ap3, OUTPUT); pinMode(ap4, OUTPUT);
    strip.begin(); strip.show();

    Serial.println("Starting Receiver (WiFi+UDP)");

    WiFi.softAP(apSSID, apPASS);
    Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

    WiFi.begin(ssid, pass);
    Blynk.config(BLYNK_AUTH_TOKEN);

    udp.begin(UDP_PORT);
    Serial.printf("UDP listening on port %d\n", UDP_PORT);
}

void loop() {
    unsigned long now = millis();

    int packetSize = udp.parsePacket();
    if (packetSize == sizeof(myData)) {
        udp.read((uint8_t*)&myData, sizeof(myData));
        Serial.printf("UDP Received: BPM=%.1f Avg=%d Ap=%d Temp=%.2f\n",
                      myData.bpm, myData.avgBpm, myData.ap, myData.tempC);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Blynk.run();
        if (now - lastBlynkSend >= blynkInterval) {
            lastBlynkSend = now;
            if (myData.tempC != prevTemp) { Blynk.virtualWrite(V1, myData.tempC); prevTemp = myData.tempC; }
            if (myData.bpm != prevBPM) { Blynk.virtualWrite(V2, myData.bpm); prevBPM = myData.bpm; }
            if (myData.avgBpm != prevAvgBPM) { Blynk.virtualWrite(V3, myData.avgBpm); prevAvgBPM = myData.avgBpm; }
            Blynk.virtualWrite(V4, ledState1);
            Blynk.virtualWrite(V5, ledState2);
            Blynk.virtualWrite(V6, ledState3);
            Blynk.virtualWrite(V7, ledState4);
        }
    }

    bool above1 = (myData.ap == 1);
    if (above1 && !prevAbove1) { ledState1 = !ledState1; digitalWrite(ap1, ledState1); Blynk.virtualWrite(V4, ledState1); }
    prevAbove1 = above1;

    bool above2 = (myData.ap == 2);
    if (above2 && !prevAbove2) { ledState2 = !ledState2; digitalWrite(ap2, ledState2); Blynk.virtualWrite(V5, ledState2); }
    prevAbove2 = above2;

    bool above3 = (myData.ap == 4);
    if (above3 && !prevAbove3) { ledState3 = !ledState3; digitalWrite(ap3, ledState3); Blynk.virtualWrite(V6, ledState3); }
    prevAbove3 = above3;

    bool above4 = (myData.ap == 8);
    if (above4 && !prevAbove4) { ledState4 = !ledState4; digitalWrite(ap4, ledState4); Blynk.virtualWrite(V7, ledState4); }
    prevAbove4 = above4;

    if (now - lastTempAlert >= tempInterval) {
        if (myData.tempC >= TEMP_THRESHOLD) Blynk.virtualWrite(V8, 1);
        else Blynk.virtualWrite(V8, 0);
        lastTempAlert = now;
    }

    updateLeds();
}

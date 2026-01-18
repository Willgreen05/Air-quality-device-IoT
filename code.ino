#include <WiFiS3.h>
#include <ThingSpeak.h>
#include <DHT.h>
#include <ArduinoBLE.h>

// ---------- WiFi Credentials ----------
const char* ssid = "EE-782FWJ";
const char* password = "4J3NmPQhkvikCYcm";

// ---------- ThingSpeak Settings ----------
unsigned long myChannelNumber = 3192905;
const char* myWriteAPIKey = "POHQ2W84E9YG942L";

// ---------- Sensor Pins ----------
const int ldrPin = A3;
const int MQ2_PIN = A0;
#define DHT_PIN    2
#define DHT_TYPE   DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// ---------- Traffic Light Module Pins (your wiring) ----------
const int GREEN_LED_PIN  = 7;  // G -> D7
const int YELLOW_LED_PIN = 6;  // Y -> D6
const int RED_LED_PIN    = 5;  // R -> D5

// ---------- MQ-2 Thresholds ----------
const int MQ2_WARN_THRESHOLD   = 300;
const int MQ2_DANGER_THRESHOLD = 400;


const bool TRAFFIC_LIGHT_ACTIVE_LOW = false;

// ---------- WiFi ----------
WiFiClient client;
int status = WL_IDLE_STATUS;
const int MAX_WIFI_ATTEMPTS = 10;

// ---------- BLE ----------
BLEService airService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEFloatCharacteristic tempChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEFloatCharacteristic humChar ("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEIntCharacteristic   ldrChar ("19B10003-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEIntCharacteristic   mq2Char ("19B10004-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEByteCharacteristic  gasStatusChar("19B10005-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// 0=SAFE, 1=WARNING, 2=DANGER
byte computeGasStatus(int mq2Value) {
  if (mq2Value >= MQ2_DANGER_THRESHOLD) return 2;
  if (mq2Value >= MQ2_WARN_THRESHOLD) return 1;
  return 0;
}

// ---------- LED helpers ----------
void setLed(int pin, bool on) {
  if (TRAFFIC_LIGHT_ACTIVE_LOW) {
    digitalWrite(pin, on ? LOW : HIGH);
  } else {
    digitalWrite(pin, on ? HIGH : LOW);
  }
}

// Forces all OFF (used every time before turning one ON)
void allLedsOff() {
  setLed(GREEN_LED_PIN, false);
  setLed(YELLOW_LED_PIN, false);
  setLed(RED_LED_PIN, false);
}

// ✅ ONE-HOT traffic light control: only one LED can be ON
void setTrafficLightOneHot(byte gasStatus) {
  allLedsOff();  // ALWAYS clear first

  if (gasStatus == 2) {
    setLed(RED_LED_PIN, true);
  } else if (gasStatus == 1) {
    setLed(YELLOW_LED_PIN, true);
  } else {
    setLed(GREEN_LED_PIN, true);
  }
}

// Robust reset at session start (prevents any LED starting ON)
void trafficLightResetSequence() {
  pinMode(GREEN_LED_PIN, INPUT);
  pinMode(YELLOW_LED_PIN, INPUT);
  pinMode(RED_LED_PIN, INPUT);
  delay(50);

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);

  allLedsOff();
  delay(250);
  allLedsOff();
  delay(250);
}

// ---------- WiFi connect ----------
void connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < MAX_WIFI_ATTEMPTS) {
    status = WiFi.begin(ssid, password);
    delay(3000);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect after multiple attempts.");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { }

  // ✅ Ensure no LEDs start ON
  trafficLightResetSequence();

  dht.begin();
  Serial.println("DHT11 initialized.");

  // BLE init
  if (!BLE.begin()) {
    Serial.println("BLE start failed!");
  } else {
    BLE.setLocalName("AirQualityR4");
    BLE.setDeviceName("AirQualityR4");
    BLE.setAdvertisedService(airService);

    airService.addCharacteristic(tempChar);
    airService.addCharacteristic(humChar);
    airService.addCharacteristic(ldrChar);
    airService.addCharacteristic(mq2Char);
    airService.addCharacteristic(gasStatusChar);
    BLE.addService(airService);

    tempChar.writeValue(-1.0f);
    humChar.writeValue(-1.0f);
    ldrChar.writeValue(0);
    mq2Char.writeValue(0);
    gasStatusChar.writeValue((byte)0);

    BLE.advertise();
    Serial.println("BLE advertising as: AirQualityR4");
  }

  Serial.println("MQ-2 warm-up: waiting 20s...");
  delay(20000);

  connectToWiFi();
  ThingSpeak.begin(client);

  Serial.println("Setup complete. Starting data transmission...");
}

void loop() {
  BLE.poll();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Attempting to reconnect...");
    connectToWiFi();
  }

  int ldrValue = analogRead(ldrPin);
  int mq2Value = analogRead(MQ2_PIN);

  float dhtTemp = dht.readTemperature();
  float dhtHum  = dht.readHumidity();
  bool dht_ok = !isnan(dhtTemp) && !isnan(dhtHum);

  // Compute gasStatus once and use it everywhere
  byte gasStatus = computeGasStatus(mq2Value);

  // ✅ One-hot LED behaviour enforced here
  setTrafficLightOneHot(gasStatus);

  // BLE updates
  if (dht_ok) {
    tempChar.writeValue(dhtTemp);
    humChar.writeValue(dhtHum);
  } else {
    tempChar.writeValue(-1.0f);
    humChar.writeValue(-1.0f);
  }
  ldrChar.writeValue(ldrValue);
  mq2Char.writeValue(mq2Value);
  gasStatusChar.writeValue(gasStatus);

  // Serial
  if (gasStatus == 2) Serial.print("[DANGER] ");
  else if (gasStatus == 1) Serial.print("[WARNING] ");
  else Serial.print("[SAFE] ");

  if (dht_ok) {
    Serial.print("Temp: "); Serial.print(dhtTemp);
    Serial.print(" C | Hum: "); Serial.print(dhtHum);
    Serial.print(" % | ");
  } else {
    Serial.print("Temp/Hum: ERROR | ");
  }

  Serial.print("LDR: "); Serial.print(ldrValue);
  Serial.print(" | MQ2: "); Serial.print(mq2Value);
  Serial.print(" | gasStatus="); Serial.println(gasStatus);

  // ThingSpeak
  if (dht_ok) {
    ThingSpeak.setField(1, dhtTemp);
    ThingSpeak.setField(2, dhtHum);
  } else {
    ThingSpeak.setField(1, -1);
    ThingSpeak.setField(2, -1);
  }
  ThingSpeak.setField(3, ldrValue);
  ThingSpeak.setField(4, mq2Value);

  int httpCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (httpCode == 200) Serial.println("Data sent to ThingSpeak successfully!");
  else {
    Serial.print("Error sending data. HTTP code: ");
    Serial.println(httpCode);
  }

  delay(20000);
}

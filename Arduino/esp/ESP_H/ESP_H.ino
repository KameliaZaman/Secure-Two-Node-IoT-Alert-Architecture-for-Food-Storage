#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <base64.h>
#include "ASCON.h"

// ---------------------------------------------------------
// DEVICE CONFIG (HIGH HUMIDITY STORAGE)
// ---------------------------------------------------------
#define DEVICE_ID "ESP32_H"

#define DHTPIN 26
#define DHTTYPE DHT11

#define LED_WHITE 33   // WHITE LED → too dry (ALERT_LOW)
#define LED_RED   32   // RED LED   → too humid (ALERT_HIGH)
#define BUTTONPIN 27

DHT dht(DHTPIN, DHTTYPE);

bool whiteBlink = false;
bool redBlink   = false;
unsigned long lastBlink = 0;
bool blinkState = false;
bool offlineMode = false;

// ---------------------------------------------------------
// BUTTON EDGE-TRIGGER STATE
// ---------------------------------------------------------
bool lastButtonState = HIGH;

// ---------------------------------------------------------
// KEY + CTR + NVS
// ---------------------------------------------------------
Preferences prefs;
uint8_t key[16];
uint32_t ctr = 1;

uint8_t default_key[16] = {
  0x10,0x20,0x30,0x40,
  0x50,0x60,0x70,0x80,
  0x90,0xA0,0xB0,0xC0,
  0xD0,0xE0,0xF0,0x00
};

void loadKey() {
  prefs.begin("secure", false);
  if (prefs.getBytesLength("ascon_key") == 16) {
    prefs.getBytes("ascon_key", key, 16);
    Serial.println("Loaded key from NVS.");
  } else {
    memcpy(key, default_key, 16);
    Serial.println("Loaded DEFAULT key.");
  }
  prefs.end();
}

void saveKey(uint8_t* new_key) {
  prefs.begin("secure", false);
  prefs.putBytes("ascon_key", new_key, 16);
  prefs.end();
  Serial.println("New key saved to NVS.");
}

// ---------------------------------------------------------
// WIFI + MQTT
// ---------------------------------------------------------
const char* ssid       = "Orange-600e1";
const char* password   = "4438ggYu";
const char* mqtt_server = "192.168.0.19";

WiFiClient   espClient;
PubSubClient client(espClient);

// ---------------------------------------------------------
// ENCRYPT STATUS MESSAGE
// ---------------------------------------------------------
String encrypt_message(String plaintext) {
  uint8_t nonce[16] = {0};

  uint8_t ciphertext[300];
  int ct_len = ascon128_encrypt(ciphertext,
                                (uint8_t*)plaintext.c_str(),
                                plaintext.length(),
                                key,
                                nonce);

  String cipher_b64 = base64::encode(ciphertext, ct_len);
  String nonce_b64  = base64::encode(nonce, 16);

  DynamicJsonDocument doc(256);
  doc["cipher"] = cipher_b64;
  doc["nonce"]  = nonce_b64;
  doc["ctr"]    = ctr++;

  String out;
  serializeJson(doc, out);
  return out;
}

// ---------------------------------------------------------
// BASE64 DECODE
// ---------------------------------------------------------
String b64decode(String input) {
  int len = input.length();
  int padding = 0;

  if (input.endsWith("=="))      padding = 2;
  else if (input.endsWith("=")) padding = 1;

  int decodedLen = (len * 3) / 4 - padding;
  uint8_t decoded[decodedLen];

  int j = 0;
  uint8_t sextet[4];

  for (int i = 0; i < len;) {
    for (int k = 0; k < 4; k++, i++) {
      char c = input[i];

      if (c >= 'A' && c <= 'Z')      sextet[k] = c - 'A';
      else if (c >= 'a' && c <= 'z') sextet[k] = c - 'a' + 26;
      else if (c >= '0' && c <= '9') sextet[k] = c - '0' + 52;
      else if (c == '+')            sextet[k] = 62;
      else if (c == '/')            sextet[k] = 63;
      else                          sextet[k] = 0;
    }

    decoded[j++] = (sextet[0] << 2) | (sextet[1] >> 4);
    if (j < decodedLen) decoded[j++] = (sextet[1] << 4) | (sextet[2] >> 2);
    if (j < decodedLen) decoded[j++] = (sextet[2] << 6) |  sextet[3];
  }

  String out;
  for (int i = 0; i < decodedLen; i++) out += char(decoded[i]);
  return out;
}

// ---------------------------------------------------------
// DECRYPT ALERT / RESET
// ---------------------------------------------------------
void decrypt_alert_or_rotation(String json_str) {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, json_str) != DeserializationError::Ok) return;

  String cipher_raw = b64decode(doc["cipher"]);
  String nonce_raw  = b64decode(doc["nonce"]);

  uint8_t plaintext[300];
  int pt_len = ascon128_decrypt(
      plaintext,
      (uint8_t*)cipher_raw.c_str(),
      cipher_raw.length(),
      key,
      (uint8_t*)nonce_raw.c_str()
  );

  if (pt_len <= 0) {
    Serial.println("!! DECRYPT FAILED");
    return;
  }

  plaintext[pt_len] = 0;
  String pt = (char*)plaintext;

  Serial.print("Decrypted: ");
  Serial.println(pt);

    if (pt == "ALERT_HIGH") {
    // Too humid in low-humidity storage -> blink RED on both
    offlineMode = false;
    redBlink   = true;
    whiteBlink = false;
  }

  else if (pt == "ALERT_LOW") {
    // Too dry in high-humidity storage -> blink WHITE on both
    offlineMode = false;
    whiteBlink = true;
    redBlink   = false;
  }

  else if (pt == "RESET") {
    // Clear any alert or offline state
    offlineMode = false;
    whiteBlink = false;
    redBlink   = false;
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_RED, LOW);
  }

  else if (pt == "OFFLINE_H") {
    // High-humidity storage ESP32_H is offline
    // Show solid WHITE on both devices
    offlineMode = true;
    whiteBlink = false;
    redBlink   = false;
    digitalWrite(LED_RED,   LOW);
    digitalWrite(LED_WHITE, HIGH);
  }

  else if (pt == "OFFLINE_L") {
    // Low-humidity storage ESP32_L is offline
    // Show solid RED on both devices
    offlineMode = true;
    whiteBlink = false;
    redBlink   = false;
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_RED,   HIGH);
  }

}

// ---------------------------------------------------------
// MQTT CALLBACK
// ---------------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  String incoming;
  for (int i = 0; i < length; i++) incoming += (char)payload[i];

  decrypt_alert_or_rotation(incoming);
}

// ---------------------------------------------------------
// MQTT RECONNECT
// ---------------------------------------------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Reconnecting MQTT...");
    if (client.connect(DEVICE_ID)) {
      Serial.println("connected.");

      client.subscribe("alert");
      client.subscribe("reset_alert");
      client.subscribe("key_update");

    } else {
      Serial.println("Retry...");
      delay(1000);
    }
  }
}

// ---------------------------------------------------------
// SETUP
// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(LED_WHITE, OUTPUT);
  pinMode(LED_RED,   OUTPUT);
  pinMode(BUTTONPIN, INPUT_PULLUP);

  loadKey();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  dht.begin();
}

// ---------------------------------------------------------
// LOOP
// ---------------------------------------------------------
unsigned long lastMsg = 0;

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();

  // LED blink
  if (!offlineMode && (now - lastBlink > 500)) {
    lastBlink = now;
    blinkState = !blinkState;

    digitalWrite(LED_WHITE, whiteBlink ? blinkState : LOW);
    digitalWrite(LED_RED,   redBlink   ? blinkState : LOW);
  }

  // SEND STATUS EVERY 60 seconds (test timing we use 4 seconds)
  if (now - lastMsg > 4000) {
    lastMsg = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
      Serial.println("DHT read failed — skipping");
      return;
    }

    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"id\":\"%s\",\"humidity\":%.2f,\"temp\":%.2f,\"ctr\":%lu}",
             DEVICE_ID, h, t, ctr);

    String encrypted = encrypt_message(String(buf));
    client.publish("status", encrypted.c_str());

    Serial.print("Sent encrypted: ");
    Serial.println(encrypted);
  }

  //  EDGE-TRIGGERED ACK BUTTON 
  bool buttonNow = digitalRead(BUTTONPIN);

  if (lastButtonState == HIGH && buttonNow == LOW) {
    Serial.println("Button pressed → ACK");
    client.publish("ack", DEVICE_ID);
    delay(50);
  }

  lastButtonState = buttonNow;
}

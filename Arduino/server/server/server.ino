#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"

#define DHTPIN 26       // GPIO for DHT sensor
#define DHTTYPE DHT11   // or DHT11
#define LED_PIN 32
#define BUTTON_PIN 27

const char* ssid = "Nokia3310";
const char* password = "medhamedha";
const char* mqtt_server = "10.64.83.4"; // <-- Replace with your computer's IP address

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String message = String((char*)payload);
  Serial.printf("Message arrived [%s]: %s\n", topic, message.c_str());

  if (String(topic) == "led/control") {
    if (message == "ON") digitalWrite(LED_PIN, HIGH);
    else digitalWrite(LED_PIN, LOW);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("esp32_B")) {
      Serial.println("connected");
      //client.subscribe("led/control");
      client.subscribe("button/esp1");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  delay(2000);  // give USB time to come up
  Serial.println("=== ESP32 TEST START ===");

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  dht.begin();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h) && !isnan(t)) {
    String payload = String("{\"temp\":") + t + ",\"hum\":" + h + "}";
    client.publish("dht/data", payload.c_str());
  }

  if (digitalRead(BUTTON_PIN) == HIGH ){
    client.publish("button/state", "pressed");
    delay(500);
  }
  else{
    client.publish("button/state", "NOT pressed");
  }
  

  delay(2000);
}
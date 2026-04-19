#include "DHT.h"

// Pin definitions
#define DHTPIN 26        // DHT11 data pin connected to GPIO 26
#define DHTTYPE DHT11    // DHT11 sensor type

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  dht.begin();
}

void loop() {

  // --- DHT11 Sensor Reading ---
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature(); // Celsius

  // Check if the readings are valid
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT11 sensor!");
  } else {
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.print(" °C  |  Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  }

  delay(2000); // wait 2 seconds before next read
}

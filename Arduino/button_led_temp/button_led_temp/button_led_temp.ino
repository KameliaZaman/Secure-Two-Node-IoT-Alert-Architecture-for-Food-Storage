#include "DHT.h"

// Pin definitions
#define DHTPIN 26        // DHT11 data pin connected to GPIO 26
#define DHTTYPE DHT11    // DHT11 sensor type

// constants won't change. They're used here to set pin numbers:
const int buttonPin = 27;  // the number of the pushbutton pin
const int ledPin = 32;    // the number of the LED pin

// variables will change:
int buttonState = 0;  // variable for reading the pushbutton status

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  // initialize the LED pin as an output:
  pinMode(ledPin, OUTPUT);
  // initialize the pushbutton pin as an input:
  pinMode(buttonPin, INPUT);
  Serial.begin(115200);
  dht.begin();
}

void loop() {
  
  // read the state of the pushbutton value:
  buttonState = digitalRead(buttonPin);

  // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  if (buttonState == HIGH) {
    // turn LED on:
    digitalWrite(ledPin, HIGH);
  } else {
    // turn LED off:
    digitalWrite(ledPin, LOW);
  }


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

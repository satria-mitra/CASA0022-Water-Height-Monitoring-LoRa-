/* 
*****************************
Water Height Monitoring
Sensor : Adafruit DYP-A01
Developed by : Satria Utama
Last Update : 12 June 2024
V1.5
*****************************

*/

#include <MKRWAN.h>
#include <ArduinoLowPower.h>
#include "arduino_secrets.h"

/******** Pin, Variables and Class for Solar Charge Controller **********/
// Pin definitions for charge controller
#define PGOOD_PIN 1  // Power Good status pin
#define CHG_PIN 2    // Charge status pin

// Number of samples to take for determining status
#define CHARGE_SAMPLES 10
#define SAMPLE_DELAY 100  // Delay between samples in milliseconds

enum SolarStatus { OFF = 0, ACTIVE = 1 };
enum BatteryStatus { CHARGING = 0, FULL = 1, DRAINING = 2 };

// Variables to hold the status of the solar panel and battery
SolarStatus solar_status = OFF;
BatteryStatus battery_status = DRAINING;

// Class for Solar Charge Controller
class DeviceHealth {
public:
  SolarStatus solar_status;
  BatteryStatus battery_status;
  void updateDeviceHealth();
  void printDeviceHealth();
};
// Declare class globally
DeviceHealth deviceHealth;


/******** Pin, Variables and Class for Ultrasonic Sensor **********/

const int sensorPowerPin = 4; // Pin to control MOSFET gate
int interval = 57000; // Interval to wake up (60000 milliseconds or 60 seconds)

class DYP_A01 {
public:
  void readSensor();
};

// Declare class globally
DYP_A01 sensor;


/******** Pin, Variables and Class for LoRaWAN Connections **********/
#define MAX_JOIN_ATTEMPTS 8
#define JOIN_RETRY_DELAY_MINUTES 2 
#define JOIN_RETRY_SLEEP_MINUTES 30 // Rejoin LoRa Network in the next 30 minutes
#define LORA_CONNECTION_STABILIZATION_DELAY_SECONDS 5 // Stabilize LoRa connections

// Class for LoRaWAN Connection
class LoRaWAN {
public:
  LoRaModem modem;
  String appEui = SECRET_APP_EUI;
  String appKey = SECRET_APP_KEY;
  uint16_t packetCount = 0;

  void init();
  void executeDownlink();
  void sendData(byte* payload, size_t payloadSize);
};

// Declare class globally
LoRaWAN lorawan;


// Declare the payload globally
byte payload[5];

void setup() {
  //Disable unused peripherals
  ADC->CTRLA.bit.ENABLE = 0;
  while (ADC->STATUS.bit.SYNCBUSY);

  DAC->CTRLA.bit.ENABLE = 0;
  while (DAC->STATUS.bit.SYNCBUSY);

  // SERCOM4->USART.CTRLA.bit.ENABLE = 0;
  // while (SERCOM4->USART.SYNCBUSY.bit.ENABLE);

  // Initialize the sensor power pin
  pinMode(sensorPowerPin, OUTPUT);
  digitalWrite(sensorPowerPin, LOW); // Ensure the sensor is off initially

  // Start the serial communication with the sensor and the serial monitor
  Serial.begin(9600);   // Serial monitor
  Serial1.begin(9600);  // Serial1 for hardware serial communication with the sensor
  Serial.println("Ultrasonic Sensor UART Test");

  // Initialize LoRaWAN Connections
  lorawan.init();

  // Attach wakeup function to handle wakeup event
  //LowPower.attachInterruptWakeup(RTC_ALARM_WAKEUP, wakeup, CHANGE);
}

void loop() {
  sensor.readSensor();
  lorawan.sendData(payload, sizeof(payload));
  // Put the board to sleep for the defined interval
  //Serial.println("Entering sleep mode...");
  LowPower.deepSleep(interval);
}

void LoRaWAN::init() {
  int joinAttempts = 0;
    // Initialize LoRa module
  if (!modem.begin(EU868)) {
    Serial.println("Failed to start module");
    while (1) {}
  }
  Serial.print("Your module version is: ");
  Serial.println(modem.version());
  Serial.print("Your device EUI is: ");
  Serial.println(modem.deviceEUI());

  // Join the network
  int connected = modem.joinOTAA(appEui, appKey);

  // Attempt to reconnect to LoRa
  while (!connected && joinAttempts < MAX_JOIN_ATTEMPTS) {
    joinAttempts++;
    Serial.println("Not connected, trying again...");
    delay(JOIN_RETRY_DELAY_MINUTES * 60 * 1000);  // wait before next attempt
    connected = modem.joinOTAA(appEui, appKey);
  }

  if (connected) {
    Serial.println("Successfully joined the network");
  } else {
    Serial.println("Failed to join network after several attempts. The device is going into a low power mode before trying again.");
    LowPower.sleep(JOIN_RETRY_SLEEP_MINUTES * 60 * 1000);  // sleep
    init();  // try to initialize the LoRaWAN connection again
  }

  modem.setADR(true);
  modem.minPollInterval(60);
  delay(LORA_CONNECTION_STABILIZATION_DELAY_SECONDS * 1000);  // wait to stabilize connection
}

void LoRaWAN::executeDownlink() {
  int receivedData;
  int i = 0;
  while (modem.available()) {
    receivedData += (char)modem.read();
    i++;
  }

  if (i == 0) {
    Serial.println("No downlink data received");
    return;
  }

  Serial.println("Received downlink data: " + receivedData);

  // If the received data is the "EXTRA_MEASURE" command, take an extra measurement
  if (receivedData == 1) {
    Serial.println("Command 1 received. Executing instant measurement...");
    //sensor.readSensor();
  } else {
    Serial.println("Invalid command received.");
    return;
    }
}

void LoRaWAN::sendData(byte* payload, size_t payloadSize) {
  int err;
  modem.beginPacket();
  modem.write(payload, payloadSize);
  err = modem.endPacket(true);

  if (err > 0) {
    packetCount++;
    Serial.println("LoRa Packet sent");
  } else {
    Serial.println("Error sending LoRa Packet");
  }
}

void DYP_A01::readSensor() {
  // Turn on the sensor by setting the MOSFET gate HIGH
  Serial.println("Turning on the sensor...");
  digitalWrite(sensorPowerPin, HIGH);
  delay(2000); // Wait for the sensor to power up

  // Clear the serial buffer
  while (Serial1.available()) {
    Serial1.read();
  }

  // Send a trigger pulse
  Serial1.write(0x55); // Sending any data to trigger the sensor

  // Wait for the response with a timeout mechanism
  unsigned long responseTimeout = millis() + 100; // 100ms timeout
  bool sensorDataReceived = false;
  int distance = -1;

  while (millis() < responseTimeout) {
    if (Serial1.available()) {
      // Read the frame header
      if (Serial1.read() == 0xFF) {
        // Ensure enough data is available
        while (Serial1.available() < 3 && millis() < responseTimeout) {
          // Wait for the remaining data to be available
        }

        if (Serial1.available() >= 3) {
          // Read the next three bytes
          byte Data_H = Serial1.read();
          byte Data_L = Serial1.read();
          byte SUM = Serial1.read();

          // Verify checksum
          byte calculatedSUM = (0xFF + Data_H + Data_L) & 0xFF;
          if (calculatedSUM == SUM) {
            // Calculate distance
            distance = (Data_H << 8) + Data_L;
            Serial.print("Distance: ");
            Serial.print(distance);
            Serial.println(" mm");
            sensorDataReceived = true;
          } else {
            Serial.println("Checksum error");
          }
        } else {
          Serial.println("Timeout waiting for full data packet");
        }
        break; // Exit the loop after reading data
      }
    }
  }

  if (!sensorDataReceived) {
    Serial.println("No sensor data received");
  }

  // Turn off the sensor to save power
  Serial.println("Turning off the sensor...");
  digitalWrite(sensorPowerPin, LOW);

  // Update and print device health status
  deviceHealth.updateDeviceHealth();
  deviceHealth.printDeviceHealth();

  // Prepare payload into CayenneLPP format
  payload[0] = highByte(distance);
  payload[1] = lowByte(distance);
  payload[2] = (deviceHealth.battery_status << 2) | deviceHealth.solar_status;
  payload[3] = highByte(lorawan.packetCount);
  payload[4] = lowByte(lorawan.packetCount);
}

// Function to update the status of the solar panel and battery
void DeviceHealth::updateDeviceHealth() {
  int pgood_count = 0;
  int chg_count = 0;

  // Take multiple samples to determine status
  for (int i = 0; i < CHARGE_SAMPLES; i++) {
    pgood_count += digitalRead(PGOOD_PIN) == LOW ? 1 : 0;
    chg_count += digitalRead(CHG_PIN) == LOW ? 1 : 0;
    delay(SAMPLE_DELAY);
  }

  // Determine solar panel status
  if (pgood_count > CHARGE_SAMPLES / 2) {
    solar_status = ACTIVE;
  } else {
    solar_status = OFF;
  }

  // Determine battery status
  if (chg_count > CHARGE_SAMPLES / 2) {
    if (solar_status == ACTIVE) {
      battery_status = CHARGING;
    } else {
      battery_status = DRAINING;
    }
  } else {
    if (solar_status == ACTIVE) {
      battery_status = FULL;
    } else {
      battery_status = DRAINING;
    }
  }
}

// Function to print the current status of the solar panel and battery
void DeviceHealth::printDeviceHealth() {
  // Print solar panel status
  if (solar_status == ACTIVE) {
    Serial.println("Solar panel is ACTIVE.");
  } else {
    Serial.println("Solar panel is OFF.");
  }

  // Print battery status
  switch (battery_status) {
    case CHARGING:
      Serial.println("Battery is CHARGING.");
      break;
    case FULL:
      Serial.println("Battery is FULL.");
      break;
    case DRAINING:
      Serial.println("Battery is DRAINING.");
      break;
  }

  // Print a blank line for readability
  Serial.println();
}

void wakeup() {
  // This function will be called when the microcontroller wakes up
  // Re-enable peripherals if needed
  // Serial.begin(9600);   // Serial monitor
  // Serial1.begin(9600);  // Serial1 for hardware serial communication with the sensor
  //Serial.println("Woke up from sleep");
}

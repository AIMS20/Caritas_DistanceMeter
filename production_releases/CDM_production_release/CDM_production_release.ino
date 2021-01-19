
/*
//TODO: Test Return Battery Status with actual Battery (!)
//TODO: Optimize measurements for minimal Error-readings
//TODO: Power management by
        -deactivating PINS
        -...
*/

// Your GPRS credentials (leave empty, if not needed)
                                  // APN (example: internet.vodafone.pt)
const char apn[]      = "webaut"; // use https://web.archive.org/web/20180119161650/http://wiki.apnchanger.org/Austria#Hofer_.28Hot.29
const char gprsUser[] = ""; // GPRS User
const char gprsPass[] = ""; // GPRS Password

// SIM card PIN (leave empty, if not defined)
//TODO: Remove //FOR TESTING PURPOSES, WILL NOT WORK IN PRODUCTION ENVIRONMENT
const char simPIN[]   = "7928"; 

// Blynk Server details //TODO: Remove //FOR TESTING PURPOSES, WILL NOT WORK IN PRODUCTION ENVIRONMENT
// "WIFI MAnager esp32" or EEPROM ()
char auth[] = "OIYHUu6ibNNhhu7l9bGg36XXuTbW0OAz";

// TTGO T-Call pins
#define MODEM_RST            5
#define MODEM_PWKEY          4
#define MODEM_POWER_ON       23
#define MODEM_TX             27
#define MODEM_RX             26
#define I2C_SDA              21
#define I2C_SCL              22

// SR04 pins
const int SR04_triggerpin = 18;   // MISO pin
const int SR04_echopin    = 19;   // SCL pin


#define uS_TO_S_FACTOR 1000000  // Conversion factor for micro seconds to seconds 
int TIME_TO_SLEEP = 120;        // Time ESP32 will go to sleep (in seconds) 3600 seconds = 1 hour

// #define BLYNK_PRINT Serial   // Defines the object that is used for printing
#define BLYNK_DEBUG BlynkSerial // Optional, this enables more detailed prints
// Set serial for debug console (to Serial Monitor, default speed 115200)
#define SerialMon Serial
// Set serial for AT commands (to SIM800 module)
#define SerialAT Serial1

// Configure TinyGSM library
#define TINY_GSM_MODEM_SIM800      // Modem is SIM800
#define TINY_GSM_RX_BUFFER   1024  // Set RX buffer to 1Kb


// For I2C connection to SIM800L
#define IP5306_ADDR          0x75
#define IP5306_REG_SYS_CTL0  0x00

// Libraries
// #include <Arduino.h>         // General, power management etc
#include <WiFi.h>
#include <esp_bt.h>             // For power management
#include <esp_wifi.h>           // For power management
#include "driver/adc.h"         // For power management
#include <Wire.h>               // For communication with I2C devices
#include <TinyGsmClient.h>      // For coms with SIM800L
#include <HCSR04.h>             // For ultrasonic sensor
#include "QuickMedianLib.h"     // For median calc of SR04 values
#include <BlynkSimpleSIM800.h>  // For Blynk-Server uploading


// Create objects
TinyGsm modem(SerialAT);

// I2C for SIM800 (to keep it running when powered from battery) //TODO: Research if necessary
TwoWire I2CPower = TwoWire(0);

UltraSonicDistanceSensor distanceSensor(SR04_triggerpin, SR04_echopin);

// Disables connection attempts & Blynk
bool keepOffline = false;
bool isInitialized;
bool isConnected;
bool blynkConnected;

// Vars of container and sensor
const float mountingHeight = 114;   //in cm //TODO: Adjust after exact measuring in MIDDLE of Container (in Maya)
const int echoCount = 15;           //how often measurement will be taken before going back to sleep
const int pauseMeasurement = 1000;  //in miliseconds
float distanceVals[echoCount];      //in cm
float distance;                     //in cm
int fillLevel;                      //in percent
float roundingMultiple = 5;

// Vars of modem
const int connectionRetries = 2;
float battPercent;
float battVolt;


void setup() { 

  // Set serial monitor debugging window baud rate to 9600 (default 115200)
  SerialMon.begin(9600);
  Serial.println("Starting up...");

  // For power saving:
  WiFi.disconnect();            //disable Wifi
  esp_bt_controller_disable();  //disable Bluetooth
  adc_power_off();              //disable analog-dig-converter
  setCpuFrequencyMhz(10);       //set cpu frequency to lowest 

  // Start I2C communication
  I2CPower.begin(I2C_SDA, I2C_SCL, 400000);
  
  // Set SR04 pins
  pinMode(SR04_triggerpin, OUTPUT);
  pinMode(SR04_echopin, INPUT);

  // Keep power when running from battery //TODO: check if deepsleep reboot on battery works without this
  bool isOk = setPowerBoostKeepOn(1);
  SerialMon.println(String("IP5306 KeepOn ") + (isOk ? "OK" : "FAIL")); 

  // Configure the wake up source as timer wake up  
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  // Set modem reset, enable, power pins
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  digitalWrite(MODEM_PWKEY, LOW);
  digitalWrite(MODEM_RST, HIGH); 
  digitalWrite(MODEM_POWER_ON, HIGH);

  // Set GSM module baud rate and UART pins
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  // Unlock your SIM card with a PIN if needed
  if (strlen(simPIN) && modem.getSimStatus() != 3 ) {
    modem.simUnlock(simPIN);
    SerialMon.println("Unlocked SIM!");
  }

  // Try to connect
  if (!keepOffline){
    SerialMon.println("Initializing modem...");
    isInitialized = modem.init();
    SerialMon.println("Modem initialized!");
    isConnected = modem.gprsConnect(apn, gprsUser, gprsPass);

    // Test GPRS modem connection, if not possible after n tries: back to deepsleep
    if (isConnected == 0){
      SerialMon.println("Testing connection...");
      if(!testModemConnection(modem, connectionRetries, apn, gprsUser, gprsPass)){
        SerialMon.println("Can't connect to GPRS. Back to deepsleep.");
        TIME_TO_SLEEP /= 2;
        esp_deep_sleep_start();
      }
    }
    else { // If GPRS connection successful, test for Blynk connection, if not possible: deepsleep
      SerialMon.println("Connecting to Blynk...");
      Blynk.config(modem, auth);
      blynkConnected = Blynk.connect(15000); // Timeout in ms

      // If Blynk connection successful, begin. If not: deepsleep
      if (blynkConnected == 1){
        SerialMon.println("Connected to Blynk!");
        SerialMon.println("Starting up Blynk...");
        Blynk.begin(auth, modem, apn, gprsUser, gprsPass);
      }
      else{
        SerialMon.println("Can't connect to Blynk. Back to deepsleep.");
        TIME_TO_SLEEP /= 2;
        esp_deep_sleep_start();
      }
    }
  }

  battPercent = modem.getBattPercent();
  battVolt = modem.getBattVoltage();

  SerialMon.println("Battery %: ");
  SerialMon.println(battPercent);
  SerialMon.println("Battery V: ");
  SerialMon.println(battVolt);

}

// Every n miliseconds, do a measurement using the sensor,
// print the distance in centimeters and send the fillLevel
void loop() {
  SerialMon.println("Starting loop...");

  //get array of multiple distance-levels to calc median afterwards: prunes out false readings 
  calcDistanceVals(echoCount, pauseMeasurement);

  //calculate median of distancevals
  calcDistance(distanceVals);

  //calculate fill-percentage depending on mounting-height of sensor (!)
  calcPercentage(distance, mountingHeight, roundingMultiple);
  printLevel(fillLevel);
    
  if ((!keepOffline) && Blynk.connected()){
    SerialMon.println("Blynk connected...");
    Blynk.run();

    SerialMon.println("Sending values to Blynk...");
    sendData(fillLevel, 5);
    sendData(battPercent, 6);
    sendData(battVolt, 7);
    delay(3000); // Otherwise disconnecting too fast and sending won't go through (!) //TODO: decrement


    SerialMon.println("Disconnecting from Blynk...");
    Blynk.disconnect();
  }

  // Put ESP32 into deep sleep mode (with timer wake up)
  SerialMon.println("Going back to sleep...");
  esp_deep_sleep_start();
}


bool setPowerBoostKeepOn(int en){
  I2CPower.beginTransmission(IP5306_ADDR);
  I2CPower.write(IP5306_REG_SYS_CTL0);
  if (en) {
    I2CPower.write(0x37); // Set bit1: 1 enable 0 disable boost keep on
  } else {
    I2CPower.write(0x35); // 0x37 is default reg value
  }
  return I2CPower.endTransmission() == 0;
}

// Calculate percentage of filllevel depending on mounting-height (!), round to nearest multiple of 5
float calcPercentage(float distance, float mountingHeight, float roundingMultiple){
  float fillHeight = (mountingHeight-distance);
  float percent = (fillHeight / mountingHeight * 100);
  float result = percent + roundingMultiple/2;
  result -= (int(result)) % (int(roundingMultiple));
  fillLevel = result;
}

// Get n samples of distance in cm
void calcDistanceVals(int echoCount, int pauseMeasurement){
  SerialMon.println("Getting measurements...");
  float distance;
  int errorCount;
  for (int i = 0; i < echoCount;){
      distance = distanceSensor.measureDistanceCm();
      if (distance != -1){
          distanceVals[i] = distance;
          Serial.println(distance);
          i++;
      }
      else{
        SerialMon.println("CANNOT GET MEASUREMENT");
        errorCount++;
        if (errorCount > 10 ){  //TODO: THINK OF BETTER SOLUTION IN PRODUCTION
          TIME_TO_SLEEP /= 2;
          esp_deep_sleep_start();
        }
        
      }
      delay(pauseMeasurement);
  }
}

// Get median of n samples of distance-values
void calcDistance(float* distanceVals){
    int dValsLength = sizeof(distanceVals) / sizeof(distanceVals[0]); 
    distance = QuickMedian<float>::GetMedian(distanceVals, dValsLength);                
    SerialMon.print("MEDIAN: ");
    SerialMon.println(distance);
}

// Print filllevel if in valid range
void printLevel(int fillLevel){
  if (fillLevel <= 0 || fillLevel > 100){
      SerialMon.print("ERROR! percent value too big/small");
      return;
  }
  SerialMon.print("%: ");
  SerialMon.println(fillLevel);
}

// Send rounded filllevel to Blynk server (write to "virtual pin")
void sendData(int data, int VPin){
  Blynk.virtualWrite(VPin, data);
  String tmp = String(data);
  SerialMon.println("Sent " + tmp + " to Blynk!");
}

// Test modems connection to GPRS with n retries, restart modem,
// go back to deepsleep if no connection can be established
bool testModemConnection(TinyGsm modem, int connectionRetries, const char* apn, const char* gprsUser, const char* gprsPass){
  delay(500);
  if (!modem.isGprsConnected()){
    SerialMon.println("Modem couldn't connect...");
    for (int j = 0; j < connectionRetries; j++){
      SerialMon.println("Retrying...");
      delay(2000);
      if (j%2 == 0){
        SerialMon.println("Restarting modem...");
        modem.restart();
        modem.gprsConnect(apn, gprsUser, gprsPass);
        delay(5000);
      }
      if (modem.isGprsConnected()){
        SerialMon.println("Modem connected!");
        return true;
      }
    }
  }
  else{
    SerialMon.println("Modem connected!");
    return true;
  }
  return false;
}


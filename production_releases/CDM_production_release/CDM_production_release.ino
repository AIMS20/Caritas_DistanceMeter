/*
//TODO: Create License file
//TODO: Return Battery Status (!)
//TODO: Optimize measurements for minimal Error-readings
*/

// Your GPRS credentials (leave empty, if not needed)
                                  // APN (example: internet.vodafone.pt)
const char apn[]      = "webaut"; // use https://web.archive.org/web/20180119161650/http://wiki.apnchanger.org/Austria#Hofer_.28Hot.29
const char gprsUser[] = ""; // GPRS User
const char gprsPass[] = ""; // GPRS Password

// SIM card PIN (leave empty, if not defined)
//TODO: Remove //FOR TESTING PURPOSES, WILL NOT WORK IN PRODUCTION ENVIRONMENT
const char simPIN[]   = "7928"; 

// Blynk Server details //TODO: Ask if security risk >> read from textfile instead
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
const int SR04_triggerpin = 18; // MISO pin
const int SR04_echopin = 19;    // SCL pin

// Set serial for debug console (to Serial Monitor, default speed 115200)
#define BLYNK_PRINT Serial
#define SerialMon Serial
// Set serial for AT commands (to SIM800 module)
#define SerialAT Serial1

// Configure TinyGSM library
#define TINY_GSM_MODEM_SIM800      // Modem is SIM800
#define TINY_GSM_RX_BUFFER   1024  // Set RX buffer to 1Kb

// Libraries
#include <Wire.h> //For communication with I2C devices
#include <TinyGsmClient.h>
#include <HCSR04.h>
#include "QuickMedianLib.h" 
#include <BlynkSimpleSIM800.h>

// Create objects
TinyGsm modem(SerialAT);

// I2C for SIM800 (to keep it running when powered from battery) //TODO: Research if necessary
TwoWire I2CPower = TwoWire(0);


UltraSonicDistanceSensor distanceSensor(SR04_triggerpin, SR04_echopin);


// Vars of container and sensor
const float mountingHeight = 130;   //in cm
const int echoCount = 20;           //how often measurement will be taken before going back to sleep
const int pauseMeasurement = 1000;  //in miliseconds
float distanceVals[echoCount];      //in cm
float distance;                     //in cm
float fillLevel;                    //in percent


#define uS_TO_S_FACTOR 1000000   // Conversion factor for micro seconds to seconds 
#define TIME_TO_SLEEP  30        // Time ESP32 will go to sleep (in seconds) 3600 seconds = 1 hour //TODO: Test with 8h/12h

// For I2C connection to SIM800L
#define IP5306_ADDR          0x75
#define IP5306_REG_SYS_CTL0  0x00

void setup() { 
  // Set serial monitor debugging window baud rate to 115200
  SerialMon.begin(9600);

  Serial.println("Starting up...");

  // Start I2C communication
  I2CPower.begin(I2C_SDA, I2C_SCL, 400000);
  
  // Set SR04 pins
  pinMode(SR04_triggerpin, OUTPUT);
  pinMode(SR04_echopin, INPUT);

  // Keep power when running from battery
  bool isOk = setPowerBoostKeepOn(1);
  SerialMon.println(String("IP5306 KeepOn ") + (isOk ? "OK" : "FAIL"));

  // Set modem reset, enable, power pins
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  digitalWrite(MODEM_PWKEY, LOW);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_POWER_ON, HIGH);

  // Set GSM module baud rate and UART pins
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000); //TODO: See if needed, to minimize battery usage

  // Restart SIM800 module, it takes quite some time
  // To skip it, call init() instead of restart() //TODO: Test if init() is enough after deepsleep
  SerialMon.println("Initializing modem...");
  // modem.restart();
   modem.init(); //if you don't need the complete restart
   
   float battPercent = modem.getBattPercent();
   float battVolt = modem.getBattVoltage();

   Serial.println("Battery %: ");
   Serial.println(battPercent);
   Serial.println("Battery V: ");
   Serial.println(battVolt);

  // Unlock your SIM card with a PIN if needed
  if (strlen(simPIN) && modem.getSimStatus() != 3 ) {
    modem.simUnlock(simPIN);
    Serial.println("Unlocked SIM!");
  }
  
  // // You might need to change the BME280 I2C address, in our case it's 0x76
  // if (!sr04.begin(0x76, &I2CSR04)) { //TODO: check for connection in another way... distanceVals ==0? modem.get...?
  //   Serial.println("Could not find a valid SR04 sensor, check wiring!");
  //   while (1);
  // }

  Serial.println("Connecting to Blynk...");
  Blynk.begin(auth, modem, apn, gprsUser, gprsPass); //TODO: Add handling if connection doesn't work

  // Configure the wake up source as timer wake up  
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
}

// Every x miliseconds, do a measurement using the sensor,
// print the distance in centimeters and send the fillLevel
void loop() {
  Serial.println("Running Blynk...");
  Blynk.run();
  if (Blynk.connect() == false){
    Serial.println("Connection to Blynk LOST!"); //TODO: Add bool..
  }
  else
  {
    Serial.println("Connected to Blynk!");
  }
  
  Serial.println("Starting loop...");
	#pragma region SENSOR

	//get array of multiple distance-levels to calc median afterwards: prunes out false readings 
	getDistanceVals(echoCount, pauseMeasurement);

	//calculate median of distancevals
	int dValsLength = sizeof(distanceVals) / sizeof(distanceVals[0]); 
	distance = QuickMedian<float>::GetMedian(distanceVals, dValsLength);                
	Serial.print("MEDIAN: ");
	Serial.println(distance);

	//calculate fill-percentage depending on mounting-height of sensor (!)
	fillLevel = getPercentage(distance, mountingHeight);
	printLevel(fillLevel);

  Serial.println("Sending values to Blynk...");
  sendData(fillLevel);
  delay(3000); // Otherwise sending won't go through (!)

  // Put ESP32 into deep sleep mode (with timer wake up)
  Serial.println("Going back to sleep...");
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

float getPercentage(float distance, float mountingHeight){
  float fillHeight = (mountingHeight-distance);
  return (fillHeight/mountingHeight*100);
}

void getDistanceVals(int echoCount, int pauseMeasurement){
  Serial.println("Getting measurements...");
  float distance;
  for (int i = 0; i < echoCount;){
      distance = distanceSensor.measureDistanceCm(); //TODO: What to output when not reading e.g. distance too small?
      if (distance != -1){
          distanceVals[i] = distance;
          Serial.println(distance);
          i++;
      }
      else{
        Serial.println("CANNOT GET MEASUREMENT");
      }
      delay(pauseMeasurement);
  }
}

void printLevel(float fillLevel){
  if (fillLevel <= 0 || fillLevel > 100){
      Serial.print("ERROR! percent value too big/small");
      return;
  }

  Serial.print("%: ");
  Serial.println(fillLevel);
}

void sendData(float fillLevel){
  Blynk.virtualWrite(V5, fillLevel);
  Serial.println("Sent ");
  Serial.println(fillLevel);
  Serial.println(" to Blynk!");
}
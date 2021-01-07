/*

*/

// Your GPRS credentials (leave empty, if not needed)
const char apn[]      = "webaut"; // APN (example: internet.vodafone.pt) use https://wiki.apnchanger.org //TODO: use webarchive
const char gprsUser[] = ""; // GPRS User
const char gprsPass[] = ""; // GPRS Password

// SIM card PIN (leave empty, if not defined)
const char simPIN[]   = "7928";  //TODO: Remove //FOR TESTING PURPOSES, WILL NOT WORK IN PRODUCTION ENVIRONMENT

// Server details //TODO: Add Blynk credentials
// The server variable can be just a domain name or it can have a subdomain. It depends on the service you are using
const char server[] = "example.com"; // domain name: example.com, maker.ifttt.com, etc
const char resource[] = "/post-data.php";         // resource path, for example: /post-data.php
const int  port = 80;                             // server port number

// Keep this API Key value to be compatible with the PHP code provided in the project page. 
// If you change the apiKeyValue value, the PHP file /post-data.php also needs to have the same key 
String apiKeyValue = ""; //TODO: Add API key, read from textfile?

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
#define SerialMon Serial
// Set serial for AT commands (to SIM800 module)
#define SerialAT Serial1

// Configure TinyGSM library
#define TINY_GSM_MODEM_SIM800      // Modem is SIM800
#define TINY_GSM_RX_BUFFER   1024  // Set RX buffer to 1Kb

// Define the serial console for debug prints, if needed
//#define DUMP_AT_COMMANDS

#include <Wire.h> //For communication with I2C devices
#include <TinyGsmClient.h>
#include <Adafruit_Sensor.h> //TODO: needed?
#include <HCSR04.h>
#include "QuickMedianLib.h" //TODO: include Blynk library

#ifdef DUMP_AT_COMMANDS
  #include <StreamDebugger.h>
  StreamDebugger debugger(SerialAT, SerialMon);
  TinyGsm modem(debugger);
#else
  TinyGsm modem(SerialAT);
#endif


// I2C for SIM800 (to keep it running when powered from battery) //TODO: Research if necessary
TwoWire I2CPower = TwoWire(0);


UltraSonicDistanceSensor distanceSensor(SR04_triggerpin, SR04_echopin);

// Vars of container and sensor
const float mountingHeight = 130;   //in cm
const int echoCount = 10;           //how often measurement will be taken before going back to sleep
const int pauseMeasurement = 500;    //in miliseconds
float distanceVals[echoCount];      //in cm
float distance;                     //in cm
float fillLevel;                    //in percent

// TinyGSM Client for Internet connection
TinyGsmClient client(modem);

#define uS_TO_S_FACTOR 1000000     /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  120        /* Time ESP32 will go to sleep (in seconds) 3600 seconds = 1 hour */

#define IP5306_ADDR          0x75
#define IP5306_REG_SYS_CTL0  0x00


void setup() {  //TODO: Rewrite for SR04
  // Set serial monitor debugging window baud rate to 115200
  SerialMon.begin(115200);

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

	#pragma region MODEM

  // Restart SIM800 module, it takes quite some time
  // To skip it, call init() instead of restart() //TODO: Test if init() is enough after deepsleep
  SerialMon.println("Initializing modem...");
  // modem.restart();
   modem.init(); //if you don't need the complete restart

  // Unlock your SIM card with a PIN if needed
  if (strlen(simPIN) && modem.getSimStatus() != 3 ) {
    modem.simUnlock(simPIN);
  }
	#pragma endregion MODEM

  Serial.println("ENDREGION Modem...");
  
  // // You might need to change the BME280 I2C address, in our case it's 0x76
  // if (!sr04.begin(0x76, &I2CSR04)) { //TODO: check for connection in another way... distanceVals ==0?
  //   Serial.println("Could not find a valid SR04 sensor, check wiring!");
  //   while (1);
  // }

  // Configure the wake up source as timer wake up  
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
}

// Every x miliseconds, do a measurement using the sensor and print the distance in centimeters
// TODO: Send measurements via GSM to Blynk-server
void loop() {
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

	#pragma endregion SENSOR

  Serial.println("ENDREGION Sensor...");

  SerialMon.print("Connecting to APN: ");
  SerialMon.print(apn);
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println(" fail"); 
  }
//   else {
//     SerialMon.println(" OK");
    
//     SerialMon.print("Connecting to ");
//     SerialMon.print(server);
//     if (!client.connect(server, port)) {
//       SerialMon.println(" fail");
//     }
//     else {
//       SerialMon.println(" OK");
    
//       // Making an HTTP POST request
//       SerialMon.println("Performing HTTP POST request...");
//       // Prepare your HTTP POST request data (Temperature in Celsius degrees) //TODO: Rewrite for SR04, depending on Blynk
//       String httpRequestData = "api_key=" + apiKeyValue + "&value1=" + String(bme.readTemperature())
//                              + "&value2=" + String(bme.readHumidity()) + "&value3=" + String(bme.readPressure()/100.0F) + "";
//       // Prepare your HTTP POST request data (Temperature in Fahrenheit degrees)
//       //String httpRequestData = "api_key=" + apiKeyValue + "&value1=" + String(1.8 * bme.readTemperature() + 32)
//       //                       + "&value2=" + String(bme.readHumidity()) + "&value3=" + String(bme.readPressure()/100.0F) + "";
          
//       // You can comment the httpRequestData variable above
//       // then, use the httpRequestData variable below (for testing purposes without the BME280 sensor)
//       //String httpRequestData = "api_key=tPmAT5Ab3j7F9&value1=24.75&value2=49.54&value3=1005.14";
    
//       client.print(String("POST ") + resource + " HTTP/1.1\r\n");
//       client.print(String("Host: ") + server + "\r\n");
//       client.println("Connection: close");
//       client.println("Content-Type: application/x-www-form-urlencoded");
//       client.print("Content-Length: ");
//       client.println(httpRequestData.length());
//       client.println();
//       client.println(httpRequestData);

//       unsigned long timeout = millis();
//       while (client.connected() && millis() - timeout < 10000L) {
//         // Print available data (HTTP response from server) //TODO: Rewrite for SR04; if answer >> go to sleep
//         while (client.available()) {
//           char c = client.read();
//           SerialMon.print(c);
//           timeout = millis();
//         }
//       }
//       SerialMon.println();
    
      // Close client and disconnect
      client.stop();
      SerialMon.println(F("Server disconnected"));
      modem.gprsDisconnect();
      SerialMon.println(F("GPRS disconnected"));
//     }
//   }
  // Put ESP32 into deep sleep mode (with timer wake up)
  Serial.println("Going back to sleep...")
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
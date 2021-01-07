#include <HCSR04.h>
#include "QuickMedianLib.h"


// Initialize sensor that uses digital pins
const int triggerPin = 18;
const int echoPin = 19;
UltraSonicDistanceSensor distanceSensor(triggerPin, echoPin);

const float mountingHeight = 130;   //in cm
const int echoCount = 10;           //how often measurement will be taken before going back to sleep
const int pauseMeasurement = 500;    //in miliseconds
float distanceVals[echoCount];
float distance;                     //in cm
float fillLevel;                    //in percent

void setup () {
 Serial.begin(115200);  // We initialize serial connection so that we could print values from sensor

Serial.println("Setting Pins...");
  // Set SR04 pins
  pinMode(triggerPin, OUTPUT);
  pinMode(echoPin, INPUT);
}

void loop () {
    // Every 500 miliseconds, do a measurement using the sensor and print the distance in centimeters
        
        //get array of multiple distance-levels to calc median afterwards: prunes out false readings 
Serial.println("Starting Loop...");
        getDistanceVals(echoCount, pauseMeasurement);
        
        //calculate Median of distancevals
        int dValsLength = sizeof(distanceVals) / sizeof(distanceVals[0]); 
        
        distance = QuickMedian<float>::GetMedian(distanceVals, dValsLength);                
        Serial.print("MEDIAN: ");
        Serial.println(distance);

        //calculate fill-percentage depending on mounting-height of sensor (!)
        fillLevel = getPercentage(distance, mountingHeight);
        printLevel(fillLevel);


Serial.println("Ending Loop...");
        delay(5000); //TODO: Replace with deep sleep
}

float getPercentage(float distance, float mountingHeight){
    float fillHeight = (mountingHeight-distance);
    return (fillHeight/mountingHeight*100);
}

void getDistanceVals(int echoCount, int pauseMeasurement){
Serial.println("in Method...");
    float distance;
    // float distanceVals[echoCount];
    for (int i = 0; i < echoCount;){
        distance = distanceSensor.measureDistanceCm(); //TODO: What to output when not reading e.g. distance too small?
        if (distance != -1){
            distanceVals[i] = distance;
            Serial.println(distance);
            i++;
        }
        else
        {
            Serial.println("FUCKY WUCKY");
        }
        
        delay(pauseMeasurement);
    }
}

void printLevel(float fillLevel){
    if (fillLevel <= 0 || fillLevel > 100){
        Serial.print("ERROR! value too big/small");
        return;
    }

    Serial.print("%: ");
    Serial.println(fillLevel);
}
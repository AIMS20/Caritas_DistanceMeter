#include <HCSR04.h>
#include "QuickMedianLib.h"

// Initialize sensor that uses digital pins
const int triggerPin = 7;
const int echoPin = 8;
UltraSonicDistanceSensor distanceSensor(triggerPin, echoPin);

const float mountingHeight = 130;   //in cm
const int echoCount = 10;           //how often measurement will be taken before going back to sleep
float distanceVals[echoCount];
float distance;                     //in cm
float fillLevel;                    //in percent

void setup () {
 Serial.begin(9600);  // We initialize serial connection so that we could print values from sensor

}

void loop () {
    // Every 500 miliseconds, do a measurement using the sensor and print the distance in centimeters
        
        //get array of multiple distance-levels to calc median afterwards: prunes out false readings 
        getDistanceVals(echoCount);
        
        //calculate Median of distancevals
        int dValsLength = sizeof(distanceVals) / sizeof(distanceVals[0]); 
        
        distance = QuickMedian<float>::GetMedian(distanceVals, dValsLength);                
        Serial.print("MEDIAN: ");
        Serial.println(distance);

        //calculate fill-percentage depending on mounting-height of sensor (!)
        fillLevel = getPercentage(distance, mountingHeight);
        printLevel(fillLevel);


        delay(5000); //TODO: Replace with deep sleep
}

float getPercentage(float distance, float mountingHeight){
    float fillHeight = (mountingHeight-distance);
    return (fillHeight/mountingHeight*100);
}

void getDistanceVals(int echoCount){
    float distance;
    // float distanceVals[echoCount];
    for (int i = 0; i < echoCount;){
        distance = distanceSensor.measureDistanceCm(); //TODO: What to output when not reading e.g. distance too small?
        if (distance != -1){
            distanceVals[i] = distance;
            Serial.println(distance);
            i++;
        }
        delay(500); //x echoCount amounts to 5s
    }
}

void printLevel(float fillLevel){
    if (fillLevel <= 0 || fillLevel > 100){
        Serial.print("ERROR!");
        return;
    }

    Serial.print("%: ");
    Serial.println(fillLevel);
}
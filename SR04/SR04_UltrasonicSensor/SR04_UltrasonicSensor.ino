#include <HCSR04.h>

// Initialize sensor that uses digital pins 13 and 12.
const int triggerPin = 7;
const int echoPin = 8;
UltraSonicDistanceSensor distanceSensor(triggerPin, echoPin);

float distance;
float fillLevel;
float mountingHeight = 130;
int echoCount = 10;

void setup () {
    Serial.begin(9600);  // We initialize serial connection so that we could print values from sensor.
}

void loop () {
    // Every 500 miliseconds, do a measurement using the sensor and print the distance in centimeters.
        
            distance = distanceSensor.measureDistanceCm();

        
                

        fillLevel = getPercentage(distance, mountingHeight);
        Serial.print("%: ");
        Serial.println(fillLevel);
}

//calculate fill-percentage depending on mounting-height of sensor (!)
float getPercentage(float distance, float mountingHeight){
    float d = distance;
    float m = mountingHeight;
    float fillHeight = m-d;
    return (fillHeight/m*100);
}
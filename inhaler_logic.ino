#include <Arduino_LSM6DS3.h>
#include <ArduinoBLE.h>

// Create a BLE service
BLEService inhalerService("3b0d6406-ad62-49a0-aec3-ea8a17cc25fd");  // Custom UUID (can be anything, 180D is just for demo)

// Create a BLE characteristic to send accelerometer data
BLECharacteristic inhalerDataChar("3b0d6406-ad62-49a0-aec3-ea8a17cc25fe", BLERead | BLENotify, 100);  // Max 50 characters

//Create a BLE characteristic to read from Python
BLECharacteristic sessionControlChar("3b0d6406-ad62-49a0-aec3-ea8a17cc25ff", BLEWrite, 20);

//initialize constants
//shake step
float x, y, z;
int degreesX = 0, prevDegreesX = 0;
int degreesY = 0, prevDegreesY = 0;
const int shakeAngleThreshold = 30; // Adjust this value as needed
int shakeCounter = 0;
int noShakeCounter = 0;
const int shakePass = 20;
const int noShakePass = 10;

//exhale/inhale step
const int hallSensorPin0 = A0; //for actual device: A1
const int hallSensorPin1 = A1; //for actual device: A2
const int exhaleThreshold = 10;
const int inhaleThreshold = 10;
int exhaleCounter = 0;
int inhaleCounter = 0;

//fire step
const int pressureSensorPin = A3; //for actual device: A0
bool pressureOn = false;
unsigned long lastActivationTime = 0;
unsigned long pressureStartTime = 0;
const int pressureHoldThreshold = 2000;

//hold breath step
const unsigned long holdTime = 10000;  // 10 seconds
bool holding = false;

// enum StepState {
//  IDLE, SHAKING, EXHALE_BEFORE, FIRE, INHALE_AFTER, HOLD_BREATH, COMPLETE
// };

enum StepState {
  STEP_SHAKE, STEP_BREATHE, STEP_FIRE, STEP_HOLD, STEP_COMPLETE
};

StepState currentStep = STEP_SHAKE;
String data;
bool wasConnected = false;
bool sessionActive = false;

void setup() {
  delay(2000);
  Serial.begin(9600);
  Serial.println("Started");

  inhalerService.addCharacteristic(sessionControlChar);
  sessionControlChar.writeValue("IDLE");  // Initial state

  // Start BLE
  if (!BLE.begin()) {
    Serial.println("BLE failed to start!");
    while (1);
  }

  // Name of your BLE device
  BLE.setLocalName("Nano33IoT");
  BLE.setAdvertisedService(inhalerService);

  // Add the characteristic to the service
  inhalerService.addCharacteristic(inhalerDataChar);
  BLE.addService(inhalerService);
  inhalerDataChar.setValue("reading");

  // Start advertising
  BLE.advertise();
  Serial.println("BLE device is now advertising...");

  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

}

void loop() {
  BLE.poll();

  // listen for BLE peripherals to connect:
  BLEDevice central = BLE.central();

  if (central) {
    if (!wasConnected) {
      Serial.print("Connected to central: ");
      data = "Connected to Bluetooth!";
      inhalerDataChar.setValue(data.c_str());
      Serial.println(central.address());
      wasConnected = true;
    }

    // while the central is still connected to peripheral:
    while (central.connected()) {
      switch (currentStep) {
        case STEP_SHAKE:
          if (shakeInhaler()) {
            currentStep = STEP_BREATHE;
            Serial.println("Step 1 complete: Moving to BREATHE");
            data = "Shake Inhaler  - PASS\nNext Step: Exhale slowly and completely into the inhaler";
            inhalerDataChar.setValue(data.c_str());
            Serial.println("Data sent: Shake step pass");
          }
          break;

        case STEP_BREATHE:
          if (breathing()) {
            currentStep = STEP_FIRE;
            Serial.println("Step 2 complete: Moving to FIRE");
            data = "Exhale into Inhaler - PASS\nNext Step: Press and hold to fire the canister";
            inhalerDataChar.setValue(data.c_str());
            Serial.println("Data sent: Exhale step pass");
          }
          break;

        case STEP_FIRE:
          if (fire()) {
            currentStep = STEP_HOLD;
            Serial.println("Step 3 complete: Moving to HOLD");
            data = "Press Canister - PASS\nNext Step: Inhale slowly and hold your breath for 10 seconds";
            inhalerDataChar.setValue(data.c_str());
            Serial.println("Data sent: Firing step pass");
          }
          break;

        case STEP_HOLD:
          delay(1000);
          if (holdBreath()) {
            currentStep = STEP_COMPLETE;
            Serial.println("Step 4 complete: All steps done!");
            data = "Hold Breath - PASS";
            inhalerDataChar.setValue(data.c_str());
            Serial.println("Data sent: Breath hold step pass");
          }
          break;

        case STEP_COMPLETE:
          if (sessionActive) {
            Serial.println("All steps complete! Waiting for restart...");
            data = "All steps complete! Press 'Start New Session' on UI.";
            inhalerDataChar.setValue(data.c_str());
            sessionActive = false;
          }

          if (sessionControlChar.written()) {
            String controlCommand = sessionControlChar.value();
            if (controlCommand == "START") {
              Serial.println("Session restarted via UI!");
              currentStep = STEP_SHAKE;
              sessionActive = true;
              data = "Session restarted.";
              inhalerDataChar.setValue(data.c_str());
            }
          }
          break;
    }
    Serial.println("Disconnected");
    wasConnected = false;
  }
  delay(200);
}

//step 1: Shaking inhaler for 10 seconds - WORKS
bool shakeInhaler(){
  float x, y, z;
  //Serial.println("Starting Step 1 Check ");

  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(x, y, z);
  }

  prevDegreesX = degreesX;
  prevDegreesY = degreesY;

  if (x > 0.1) {
    degreesX = map(100 * x, 0, 97, 0, 90);
  } else if (x < -0.1) {
    degreesX = map(100 * x, 0, -100, 0, 90);
  } else {
    degreesX = 0;
  }

  if (y > 0.1) {
    degreesY = map(100 * y, 0, 97, 0, 90);
  } else if (y < -0.1) {
    degreesY = map(100 * y, 0, -100, 0, 90);
  } else {
    degreesY = 0;
  }

  if (abs(degreesX - prevDegreesX) > shakeAngleThreshold || abs(degreesY - prevDegreesY) > shakeAngleThreshold) {
    shakeCounter ++;
    Serial.println("shakeCounter: " + String(shakeCounter));
    data = String(shakeCounter);
    inhalerDataChar.setValue(data.c_str());
    if (shakeCounter >= shakePass){ 
      shakeCounter = 0;
      return true;
    }
  } else {
    noShakeCounter ++;
    Serial.println("noShakeCounter: " + String(noShakeCounter));
    if (noShakeCounter >= noShakePass){
      shakeCounter = 0;
      noShakeCounter = 0;
    }
  }
  delay(200);
  return false;
}

bool breathing (){
  Serial.println("Starting Step 2 Check ");
  int hall0 = analogRead(hallSensorPin0);
  int hall1 = analogRead(hallSensorPin1);
  bool hall0_state = false;
  bool hall1_state = false;

  while (exhaleCounter < exhaleThreshold) {
    if (hall1 > 900) {
      hall1_state = true;
      if (hall0_state) {
        exhaleCounter++;
        data = "Exhaling detected";
        inhalerDataChar.setValue(data.c_str());
        Serial.print("Exhale count: ");
        Serial.println(exhaleCounter);
        hall0_state = false;
        hall1_state = false;
      }
    }
    if (hall0 > 900) {
      hall0_state = true;
    }
    hall0 = analogRead(hallSensorPin0);
    hall1 = analogRead(hallSensorPin1);
  }

  if (exhaleCounter >= exhaleThreshold){ 
    exhaleCounter = 0;
    return true;
  }

  while (inhaleCounter < inhaleThreshold) {
    if (hall0 > 900) {
      hall0_state = true;
      if (hall1_state) {
        inhaleCounter++;
        Serial.print("Inhale count: ");
        Serial.println(inhaleCounter);
        hall0_state = false;
        hall1_state = false;
      }
    }
    if (hall1 > 900) {
      hall1_state = true;
    }
    hall0 = analogRead(hallSensorPin0);
    hall1 = analogRead(hallSensorPin1);
  }

  if (inhaleCounter >= inhaleThreshold){
    inhaleCounter = 0;
    return true;
  }
  return false;
}

bool fire(){
  int pressure = analogRead(pressureSensorPin);

  if (pressure > 400) {
    data = "Press down detected";
    inhalerDataChar.setValue(data.c_str());
    if (!pressureOn) {
      pressureStartTime = millis();
      pressureOn = true;
    }

    long pressureHoldTime = millis() - pressureStartTime;
    Serial.println(String(pressureHoldTime));
    if (pressureHoldTime >= pressureHoldThreshold) {
      return true;
    }
  } else {
    pressureOn = false;
  }
  return false;
}

bool holdBreath (){
  if (!holding) {
    Serial.println("Start holding breath...");
    data = "Start holding breath...";
    inhalerDataChar.setValue(data.c_str());
    lastActivationTime = millis();  // Reset starting point
    holding = true;
  }

  if (holding){
    long timeNow = millis();
  int hall0 = analogRead(hallSensorPin0);
  int hall1 = analogRead(hallSensorPin1);
  if (hall0 > 900 || hall1 > 900) {
    lastActivationTime = timeNow;
    Serial.println("  --> Activation detected!");
  }

  long timeElapsed = timeNow - lastActivationTime;
  Serial.println(String(timeElapsed));
  if (timeElapsed >= holdTime) { 
    Serial.println("Hold complete!");
    holding = false;
    return true;
  }
  }
  return false;
}

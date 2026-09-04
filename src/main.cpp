#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RF24.h>
#include <RF24Network.h>
#include <RF24Mesh.h>

#define CE_PIN  4
#define CSN_PIN 5
#define MPU_ADDR 0x68 // Change to 0x69 if your AD0 pin is HIGH
#define INT_PIN 25   // Connect MPU6050 INT pin to ESP32 GPIO 25

RF24 radio(CE_PIN, CSN_PIN);
RF24Network network(radio);
RF24Mesh mesh(radio, network);

struct __attribute__((packed)) TelemetryPayload {
  uint8_t nodeId;
  float tilt;
  float vibration;
  uint8_t alertFlag;
};

// --- SMART TIMING & THRESHOLDS ---
const uint32_t HEARTBEAT_INTERVAL_MS = 7000; // 7-second routine baseline
const uint32_t INTERRUPT_COOLDOWN_MS = 4000;  // 4-second cooldown on physical impacts

const float DELTA_TILT_THRESHOLD = 3.0f;      // Only interrupt if tilt changes by > 3.0°
const float DELTA_VIB_THRESHOLD  = 5.0f;      // Only interrupt if vibration changes by > 5.0 m/s²

uint32_t lastSendTime = 0;
uint32_t lastInterruptTime = 0;

float lastTransmittedTilt = 0.0f;
float lastTransmittedVib  = 9.81f; // Default baseline gravity

volatile bool motionInterruptFired = false;

// Hardware ISR flags when MPU6050 detects sudden motion
void IRAM_ATTR motionISR() {
  motionInterruptFired = true;
}

void setupMPU6050Interrupt() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00); // Wake up MPU6050
  Wire.endTransmission();

  // Set Motion Detection Threshold (1 LSB = 32mg; 15 = ~0.48g transient shock)
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1F); Wire.write(15); 
  Wire.endTransmission();

  // Enable Motion Interrupt output on INT pin
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x38); Wire.write(0x40); 
  Wire.endTransmission();
}

void clearMPUInterrupt() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3A); // Read Interrupt Status register to clear the INT pin
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)1);
  if (Wire.available()) Wire.read();
}

bool readMPUAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)6);
  if (Wire.available() < 6) return false;

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();

  // Convert raw values to m/s^2 based on default +/- 2g range
  ax = (float)rawX / 16384.0f * 9.80665f;
  ay = (float)rawY / 16384.0f * 9.80665f;
  az = (float)rawZ / 16384.0f * 9.80665f;
  return true;
}

void sendTelemetry(float currentTilt, float currentVib, const char* reason) {
  TelemetryPayload data;
  data.nodeId = 1;
  data.tilt = currentTilt;
  data.vibration = currentVib;
  // Dashboard determines CRITICAL state if absolute values are high
  data.alertFlag = (currentTilt > 5.0f || currentVib > 15.0f) ? 1 : 0; 

  Serial.printf("[%s] Tilt: %.2f deg | Accel: %.2f m/s2 | Sending... ", reason, data.tilt, data.vibration);

  if (mesh.write(&data, 'M', sizeof(data))) {
    Serial.println("SUCCESS");
    // Only update baseline memory on a successful network transmission
    lastTransmittedTilt = currentTilt; 
    lastTransmittedVib  = currentVib;
  } else {
    Serial.println("FAILED (Renewing address)");
    if (!mesh.checkConnection()) mesh.renewAddress();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n--- Starting Node 1 (Smart Edge Event Mode) ---");

  // 1. Initialize I2C Bus (SDA=21, SCL=22)
  Wire.begin(21, 22);
  Wire.setClock(100000);
  
  // 2. Configure MPU6050 and Interrupts
  setupMPU6050Interrupt();
  pinMode(INT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(INT_PIN), motionISR, RISING);

  // 3. Initialize VSPI Bus for nRF24 strictly after I2C
  SPI.begin(18, 19, 23, 5);
  mesh.setNodeID(1);
  mesh.begin(108, RF24_250KBPS);
  radio.setPALevel(RF24_PA_LOW);

  Serial.println("Online. 7s heartbeat active. 4s interrupt cooldown active.");
}

void loop() {
  mesh.update();
  uint32_t now = millis();

  // Trigger 1: Routine 7-Second Heartbeat
  bool isHeartbeat = (now - lastSendTime >= HEARTBEAT_INTERVAL_MS);
  
  // Trigger 2: Hardware Interrupt Fired (with 4-second cooldown to prevent spam)
  bool isInterrupt = motionInterruptFired && (now - lastInterruptTime >= INTERRUPT_COOLDOWN_MS);

  if (isHeartbeat || isInterrupt) {
    clearMPUInterrupt();
    motionInterruptFired = false;

    float ax, ay, az;
    if (readMPUAccel(ax, ay, az)) {
      float currentTilt = atan2(hypot(ax, ay), az) * 180.0f / M_PI;
      float currentVib  = sqrt((ax * ax) + (ay * ay) + (az * az));

      if (isInterrupt) {
        lastInterruptTime = now;
        
        // Calculate the physical delta since the last transmission
        float deltaTilt = fabs(currentTilt - lastTransmittedTilt);
        float deltaVib  = fabs(currentVib - lastTransmittedVib);
        
        // Only broadcast if the impact caused a genuinely large shift
        if (deltaTilt >= DELTA_TILT_THRESHOLD || deltaVib >= DELTA_VIB_THRESHOLD) {
          lastSendTime = now; // Reset heartbeat clock so we don't double-send
          sendTelemetry(currentTilt, currentVib, "SUDDEN SHIFT");
        }
      } 
      else if (isHeartbeat) {
        lastSendTime = now;
        sendTelemetry(currentTilt, currentVib, "7s HEARTBEAT");
      }
    }
  }
}
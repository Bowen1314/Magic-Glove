/*
 * XIAO ESP32-S3 — BLE Bluetooth Mouse + Keyboard Combo Device (Magic Glove)
 *
 * Uses NimBLE for stable BLE HID on ESP32 Core 3.
 * MPU-6050 (GY-521) Gyroscope on I2C for air-mouse control.
 *
 * Pin Mapping:
 *   D0 → Mouse Left Click   (Mode A) / 'a' key (Mode B)
 *   D1 → Mouse Right Click  (Mode A) / 's' key (Mode B)
 *   D2 → Gyro Mouse Move    (Mode A) / 'k' key (Mode B)
 *   D3 → 'l' key            (both modes)
 *   D10 → Hold 2s to switch mode
 *
 *   GY-521: SDA=D4, SCL=D5
 */

#include <NimBLECombo.h>
#include <Wire.h>

// ──────────────────── Pin Definitions ────────────────────
#define PIN_D0  D0
#define PIN_D1  D1
#define PIN_D2  D2
#define PIN_D3  D3
#define PIN_D10 D10
#define PIN_BUZZER D6  // MH-FMD Buzzer pin

// ──────────────────── Settings ───────────────────────────
#define DEBOUNCE_MS 30
#define LONG_PRESS_MS 2000

// Mode A bindings
#define A_D3  'l'
// Mode B bindings
#define B_D0  'h'
#define B_D1  'j'
#define B_D2  'k'
#define B_D3  'l'

// ──────────────────── MPU-6050 ───────────────────────────
#define MPU6050_ADDR   0x68
#define MPU6050_PWR    0x6B
#define MPU6050_GYRO   0x43  // Starting register for Gyro Data

// Tuning for State Machine
#define GYRO_DEADZONE     15.0  // Deadzone Clamping threshold
#define MOUSE_MAX_SPEED   100   // Max pixel movement per frame
#define LINEAR_FACTOR_X     0.25  // Base linear speed X (controls responsiveness during slow movements)
#define LINEAR_FACTOR_Y     0.25  // Base linear speed Y 
#define NONLINEAR_FACTOR_X  0.010 // Quadratic acceleration mapping X (controls burst feel during fast flicks)
#define NONLINEAR_FACTOR_Y  0.010 // Quadratic acceleration mapping Y

// D3 体感滚轮参数 (Scroll Wheel Tuning)
#define SCROLL_DEADZONE     20.0  // Scroll wheel deadzone (prevents accidental scrolling)
#define SCROLL_SENSITIVITY  0.06  // Scroll wheel sensitivity (higher = faster)

float gyroOffsetX = 0, gyroOffsetZ = 0;
bool  mpuReady = false;

void initMPU6050() {
  Wire.begin(); 
  Wire.setClock(100000); // 100kHz standard
  Wire.setTimeOut(20);   // Set 20ms I2C timeout to prevent hanging due to loose wires

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x75);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1, (uint8_t)true);
  
  if (Wire.available()) {
    uint8_t whoAmI = Wire.read();
    if (whoAmI == 0x68 || whoAmI == 0x98 || whoAmI == 0x70) {
      mpuReady = true;
      Serial.printf("[Glove] MPU-6050 detected (WHO_AM_I = 0x%02X)\n", whoAmI);
    } else {
      mpuReady = true; 
    }
  }

  if (mpuReady) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_PWR);
    Wire.write(0x00);
    Wire.endTransmission(true);
    delay(10);

    // Set Gyro Config to ±1000 deg/s for wide dynamic range
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x1B); // GYRO_CONFIG
    Wire.write(0x10); 
    Wire.endTransmission(true);

    // Set DLPF to 44Hz to naturally filter physical hand noise
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x1A);
    Wire.write(0x03);
    Wire.endTransmission(true);
  }
}

// Read raw angular velocity. Returns false if reading fails to avoid jitter.
bool readGyroXZ(float &gx, float &gz) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_GYRO);
  if (Wire.endTransmission(false) != 0) return false;
  
  uint8_t bytesRead = Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)6, (uint8_t)true);
  if (bytesRead < 6) {
    // I2C congestion or disconnection, clear bus to prevent deadlock
    return false;
  }

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read(); // Read to preserve stream but discard
  int16_t rawZ = (Wire.read() << 8) | Wire.read();
  (void)rawY;

  // LSB/°/s depends on range (±1000 is 32.8)
  gx = rawX / 32.8;
  gz = rawZ / 32.8;
  return true;
}

// State 2: Edge trigger and zero-point capture (Calibration)
void calibrateGyro() {
  float sumX = 0, sumZ = 0;
  for (int i = 0; i < 50; i++) { // Read 50 consecutive samples
    float gx, gz;
    readGyroXZ(gx, gz);
    sumX += gx;
    sumZ += gz;
    delay(1); 
  }
  gyroOffsetX = sumX / 50.0;
  gyroOffsetZ = sumZ / 50.0;
}

// ──────────────────── State ──────────────────────────────
struct PinState {
  bool pressed;
  bool lastRaw;
  unsigned long lastChange;
};

PinState pins[5]; // D0, D1, D2, D3, D10
bool modeB = false;

// Connection state tracking (detects the moment of connection)
bool wasConnected = false;

unsigned long d10PressStart = 0;
bool          d10LongFired  = false;

#define LED_PIN LED_BUILTIN

static void flashLED(int n) {
  for (int i = 0; i < n; i++) {
    digitalWrite(LED_PIN, LOW);  delay(80);
    digitalWrite(LED_PIN, HIGH); delay(80);
  }
  digitalWrite(LED_PIN, Keyboard.isConnected() ? LOW : HIGH);
}

// Buzzer function: beeps n times (Low-level trigger version)
void buzz(int n) {
  for (int i = 0; i < n; i++) {
    digitalWrite(PIN_BUZZER, LOW);  // Set LOW: emit sound
    delay(150);
    digitalWrite(PIN_BUZZER, HIGH); // Set HIGH: silence
    delay(150);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  uint8_t pinNums[5] = {PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D10};
  for (int i = 0; i < 5; i++) {
    pinMode(pinNums[i], INPUT_PULLUP);
    pins[i] = {false, false, 0};
  }

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH); // Default OFF (silent at HIGH state)

  initMPU6050();

  Keyboard.deviceName = "Magic Glove CCC";
  Keyboard.begin();
  Mouse.begin();

  Serial.println("[Glove] State Machine ready. Mode A default.");
}

bool debounce(int idx, uint8_t pinNum, unsigned long now) {
  bool raw = (digitalRead(pinNum) == LOW);
  if (raw != pins[idx].lastRaw) {
    pins[idx].lastRaw    = raw;
    pins[idx].lastChange = now;
  }
  if ((now - pins[idx].lastChange) < DEBOUNCE_MS) return false;
  if (raw == pins[idx].pressed) return false;
  pins[idx].pressed = raw;
  return true; 
}

void loop() {
  bool isConnected = Keyboard.isConnected();

  if (!isConnected) {
    if (wasConnected) {
      Serial.println("[Glove] BLE Disconnected.");
      wasConnected = false;
    }
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
      lastBlink = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    delay(100);
    return;
  }
  
  // Just connected moment (transition from disconnected to connected):
  if (!wasConnected) {
    wasConnected = true;
    digitalWrite(LED_PIN, LOW); // Connected LED
    Serial.println("[Glove] BLE Connected! Buzzing 3 times...");
    buzz(3); // Beep 3 times on connection
  }

  unsigned long now = millis();
  uint8_t pinNums[5] = {PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D10};

  for (int i = 0; i < 5; i++) {
    bool changed = debounce(i, pinNums[i], now);
    bool isPressed = pins[i].pressed;

    // D10 Mode Switch
    if (i == 4) {
      if (changed && isPressed) {
        modeB = !modeB;
        if (modeB) {
           Mouse.release(MOUSE_LEFT);
           Mouse.release(MOUSE_RIGHT);
           Serial.println("[Glove] *** Mode B: D0=a  D1=s  D2=k  D3=l ***");
           buzz(2); // Profile B: Beep 2 times
        } else {
           Keyboard.releaseAll();
           Serial.println("[Glove] *** Mode A: D0=MouseL  D1=MouseR  D2=GyroMouse  D3=l ***");
           buzz(1); // Profile A: Beep 1 time
        }
        flashLED(3);
      }
      continue;
    }

    if (!changed) continue;

    if (!modeB) {
      switch (i) {
        case 0:
          if (isPressed) { Mouse.press(MOUSE_LEFT);   Serial.println("[Glove] D0 pressed  → Mouse LEFT"); }
          else           { Mouse.release(MOUSE_LEFT); Serial.println("[Glove] D0 released → Mouse LEFT"); }
          break;
        case 1:
          if (isPressed) { Mouse.press(MOUSE_RIGHT);   Serial.println("[Glove] D1 pressed  → Mouse RIGHT"); }
          else           { Mouse.release(MOUSE_RIGHT); Serial.println("[Glove] D1 released → Mouse RIGHT"); }
          break;
        case 2:
          if (isPressed) {
            if (mpuReady) {
              calibrateGyro();
              Serial.println("[Glove] D2 pressed → Gyro mouse ON (calibrated)");
            } else {
              Serial.println("[Glove] D2 pressed → MPU not ready!");
            }
          } else {
            Serial.println("[Glove] D2 released → Gyro mouse OFF");
          }
          break;
        case 3: // D3 = Air Scroll Wheel (Mode A)
          if (isPressed) {
            if (mpuReady) {
              calibrateGyro();
              Serial.println("[Glove] D3 pressed → Gyro SCROLL ON (calibrated)");
            } else {
              Serial.println("[Glove] D3 pressed → MPU not ready!");
            }
          } else {
            Serial.println("[Glove] D3 released → Gyro SCROLL OFF");
          }
          break;
      }
    } else {
      uint8_t keys[4] = {B_D0, B_D1, B_D2, B_D3};
      if (isPressed) {
        Keyboard.press(keys[i]);
        Serial.printf("[Glove] D%d pressed  → '%c'\n", i, keys[i]);
      } else {
        Keyboard.release(keys[i]);
        Serial.printf("[Glove] D%d released → '%c'\n", i, keys[i]);
      }
    }
  }

  // State 3: Continuous operation and output processing (Data Pipeline)
  if (!modeB && pins[2].pressed && mpuReady) {
    float rawX, rawZ;
    if (!readGyroXZ(rawX, rawZ)) {
       // Skip this frame if I2C read fails to prevent lag
       delay(5);
       return;
    }

    // 1. 去偏置 (De-biasing)
    float netX = rawX - gyroOffsetX;
    float netZ = rawZ - gyroOffsetZ;

    // 2. 死区钳制 (Deadzone Clamping)
    if (fabs(netX) < GYRO_DEADZONE) netX = 0;
    if (fabs(netZ) < GYRO_DEADZONE) netZ = 0;

    static unsigned long lastDebug = 0;
    if (now - lastDebug > 100) {
       // Only print non-zero otherwise it spams too much when idle but held
       if (netX != 0 || netZ != 0) {
          Serial.printf("[Glove IMU] Gyro netX: %.2f  netZ: %.2f\n", netX, netZ);
       }
       lastDebug = now;
    }

    // 3. Hybrid Mapping: Base Linear + Quadratic Burst
    // Pure quadratic models are too sluggish at low speeds; we add linear base for precision.
    float speedX = 0, speedZ = 0;

    if (netX != 0) {
      // Gyro X controls vertical (my)
      float sign = netX > 0 ? 1.0 : -1.0;
      float absNet = fabs(netX);
      speedX = sign * ((absNet * LINEAR_FACTOR_Y) + (netX * netX * NONLINEAR_FACTOR_Y));
    }
    if (netZ != 0) {
      // Gyro Z controls horizontal (mx)
      float sign = netZ > 0 ? 1.0 : -1.0;
      float absNet = fabs(netZ);
      speedZ = sign * ((absNet * LINEAR_FACTOR_X) + (netZ * netZ * NONLINEAR_FACTOR_X));
    }

    // 4. Send HID report (Map hardware axes to mouse axes)
    // Adjust signs or swap axes based on physical sensor orientation.
    // Default: Gyro Z maps to horizontal (mx), Gyro X maps to vertical (my)
    int mx = constrain((int)(-speedZ), -MOUSE_MAX_SPEED, MOUSE_MAX_SPEED);
    int my = constrain((int)(-speedX), -MOUSE_MAX_SPEED, MOUSE_MAX_SPEED);

    if (mx != 0 || my != 0) {
      Mouse.move(mx, my);
    }
  }

  // ── D3 Air Scroll Wheel (Only in Mode A when D3 is held) ──
  if (!modeB && pins[3].pressed && mpuReady) {
    float rawX, rawZ;
    if (readGyroXZ(rawX, rawZ)) {
      float netX = rawX - gyroOffsetX; // vertical axis only

      if (fabs(netX) < SCROLL_DEADZONE) netX = 0;

      if (netX != 0) {
        float sign = netX > 0 ? 1.0 : -1.0;
        // Hand up -> scroll up (+), Hand down -> scroll down (-)
        int scrollVal = constrain((int)(sign * fabs(netX) * SCROLL_SENSITIVITY), -5, 5);
        if (scrollVal != 0) {
          Mouse.move(0, 0, scrollVal);
        }
      }
    }
  }

  // CRITICAL: Throttle loop and BLE reports to ~100Hz
  // Without this delay, the ESP32 would overflow the host's BLE buffer and lag.
  delay(10);
}

// =============================================================================
// Line Following Delivery Rover — Arduino Uno
// =============================================================================
// Hardware:
//   - 2× IR digital sensors (left D3, right D4)
//   - HC-SR04 ultrasonic sensor (trig D11, echo D12)
//   - L298N dual H-bridge motor driver (IN1-4: D7-D10, ENA D5, ENB D6)
//   - Passive buzzer (D13)
//
// Serial commands (9600 baud, sent from Raspberry Pi):
//   S — start line following (forward delivery)
//   F — emergency stop
//   R — return to home station
//
// Serial output (read by Raspberry Pi for email notifications):
//   STARTED | STOPPED | RETURN MODE ACTIVATED
//   ROVER LOADED | ROVER EMPTY
//   RETURNED TO STATION
// =============================================================================

// --- Motor driver pins ---
const int IN1 = 7;
const int IN2 = 8;
const int IN3 = 9;
const int IN4 = 10;
const int ENA = 5;
const int ENB = 6;

// --- IR sensor pins (digital) ---
const int NUM_SENSORS   = 2;
const int SENSOR_LEFT   = 3;
const int SENSOR_RIGHT  = 4;
const int sensorPins[NUM_SENSORS] = {SENSOR_LEFT, SENSOR_RIGHT};
int sensors[NUM_SENSORS];

// --- Ultrasonic sensor pins ---
const int TRIG_PIN = 11;
const int ECHO_PIN = 12;

// --- Buzzer ---
const int BUZZER_PIN = 13;

// --- Motor speeds (0–255 PWM) ---
const int MOTOR_SPEED      = 155;
const int MOTOR_TURN_SPEED = 190;

// --- Load detection ---
// Object closer than this threshold (cm) = rover is loaded
const int LOAD_THRESHOLD_CM  = 15;
// Debounce: state must be stable for this long before reporting
const unsigned long DEBOUNCE_MS = 300;

bool stableLoadState = false;

// --- State machine ---
bool lineFollowing = false;
bool returning     = false;

// --- Return-mode stop-line detection ---
// With only 2 sensors a full-black stop line cannot be detected by sensor
// coverage alone. Instead, once both sensors read black simultaneously while
// reversing we treat that as the home marker.
bool returnStopDetected = false;

// =============================================================================
// Setup
// =============================================================================
void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(sensorPins[i], INPUT);
  }

  Serial.begin(9600);
  Serial.println("ROVER READY");
}

// =============================================================================
// Main loop
// =============================================================================
void loop() {
  handleSerial();
  checkLoadStatus();

  if (lineFollowing) {
    followLineForward();
    delay(50);
  }

  if (returning) {
    readSensors();
    followLineReverse();
    delay(50);
  }
}

// =============================================================================
// Serial command handler
// =============================================================================
void handleSerial() {
  if (!Serial.available()) return;

  char cmd = Serial.read();

  if (cmd == 'S') {
    lineFollowing = true;
    returning     = false;
    beepBuzzer();
    Serial.println("STARTED");
  }
  else if (cmd == 'F') {
    lineFollowing = false;
    returning     = false;
    stopMotors();
    Serial.println("STOPPED");
  }
  else if (cmd == 'R') {
    returning          = true;
    lineFollowing      = false;
    returnStopDetected = false;
    beepBuzzer();
    Serial.println("RETURN MODE ACTIVATED");
  }
}

// =============================================================================
// Sensor read
// =============================================================================
void readSensors() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    sensors[i] = digitalRead(sensorPins[i]);
  }
}

// =============================================================================
// Forward line following
// =============================================================================
void followLineForward() {
  readSensors();

  if (sensors[0] == 0 && sensors[1] == 0) {
    // Both on line → straight ahead
    moveForward();
  } else if (sensors[0] == 0 && sensors[1] == 1) {
    // Drifting right — left sensor still on line → steer right
    turnRight();
  } else if (sensors[0] == 1 && sensors[1] == 0) {
    // Drifting left — right sensor still on line → steer left
    turnLeft();
  } else {
    // Both off line → stop and wait for reacquisition
    stopMotors();
  }
}

// =============================================================================
// Reverse line following (return journey)
// Both sensors black simultaneously = home station stop line.
// =============================================================================
void followLineReverse() {
  // Stop-line detection: both sensors on black while reversing = home station
  if (sensors[0] == 0 && sensors[1] == 0) {
    stopMotors();
    beepBuzzer();
    Serial.println("RETURNED TO STATION");
    returning = false;
    return;
  }

  if (sensors[1] == 0) {
    // Centre path — reverse straight
    moveBackward();
  } else if (sensors[0] == 0) {
    // Left sensor on line — steer right while reversing
    turnRight();
  } else if (sensors[1] == 0) {
    // Right sensor on line — steer left while reversing
    turnLeft();
  } else {
    // Lost line
    stopMotors();
  }
}

// =============================================================================
// Load detection (debounced)
// Reads HC-SR04; reports state changes over serial after DEBOUNCE_MS.
// =============================================================================
void checkLoadStatus() {
  static bool lastReading         = false;
  static unsigned long debounceStart = 0;

  int dist            = getDistance();
  bool currentReading = (dist > 0 && dist < LOAD_THRESHOLD_CM);

  if (currentReading != lastReading) {
    debounceStart = millis();
    lastReading   = currentReading;
  }

  if ((millis() - debounceStart) > DEBOUNCE_MS) {
    if (currentReading != stableLoadState) {
      stableLoadState = currentReading;
      Serial.println(stableLoadState ? "ROVER LOADED" : "ROVER EMPTY");
    }
  }
}

// =============================================================================
// HC-SR04 distance measurement
// Returns distance in cm; returns 0 on timeout.
// =============================================================================
int getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long dur = pulseIn(ECHO_PIN, HIGH, 30000);  // 30 ms timeout ≈ 510 cm max
  if (dur == 0) return 0;
  return (int)(dur * 0.034 / 2);
}

// =============================================================================
// Motor control
// =============================================================================
void moveForward() {
  analogWrite(ENA, MOTOR_SPEED);
  analogWrite(ENB, MOTOR_SPEED);
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN4, HIGH); digitalWrite(IN3, LOW);
}

void moveBackward() {
  analogWrite(ENA, MOTOR_SPEED);
  analogWrite(ENB, MOTOR_SPEED);
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
}

void turnLeft() {
  analogWrite(ENA, MOTOR_TURN_SPEED);
  analogWrite(ENB, MOTOR_TURN_SPEED);
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN4, HIGH); digitalWrite(IN3, LOW);
}

void turnRight() {
  analogWrite(ENA, MOTOR_TURN_SPEED);
  analogWrite(ENB, MOTOR_TURN_SPEED);
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN4, LOW);  digitalWrite(IN3, HIGH);
}

void turnAround() {
  analogWrite(ENA, MOTOR_TURN_SPEED);
  analogWrite(ENB, MOTOR_TURN_SPEED);
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}

void stopMotors() {
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

// =============================================================================
// Buzzer — single 300 ms beep
// =============================================================================
void beepBuzzer() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(300);
  digitalWrite(BUZZER_PIN, LOW);
}

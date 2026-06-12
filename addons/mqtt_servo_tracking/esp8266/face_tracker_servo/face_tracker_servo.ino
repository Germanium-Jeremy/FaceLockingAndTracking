/*
 * Face-tracking servo controller (ESP32 / ESP8266-compatible sketch).
 *
 * MQTT payloads from recognize_mqtt.py:
 *   IDLE         - hold current servo angle (2s settle windows, face aligned in frame)
 *   LEFT, RIGHT  - short pan burst for moderate offset from frame center
 *   LEFT_LONG, RIGHT_LONG - longer pan when the face is near the frame edge
 *   SEARCH       - sweep back/forth while the locked face is missing
 *   CENTER       - snap servo to SERVO_CENTER_ANGLE (manual/debug; not used in tracking)
 *
 * Tracking flow on the PC:
 *   1. Publish IDLE while observing face position (~movement-settle-sec, default 2s)
 *   2. Publish LEFT/RIGHT or LEFT_LONG/RIGHT_LONG if correction is needed
 *   3. Publish IDLE again and repeat
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// Wi-Fi settings
const char* WIFI_SSID = "watashi";
const char* WIFI_PASSWORD = "nzizaprince78";

// MQTT settings (broker IP must match recognize_mqtt.py --mqtt-broker)
const char* MQTT_SERVER = "192.168.1.194";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "vision/teamalpha/movement/Jeremie";
const char* MQTT_CLIENT_ID = "teamalpha-face-servo";

// Servo configuration
const uint8_t SERVO_PIN = 2;
const int SERVO_MIN_ANGLE = 0;
const int SERVO_MAX_ANGLE = 180;
const int SERVO_CENTER_ANGLE = 90;

// Short pan burst (moderate face offset)
const int TRACK_STEP = 1;
const unsigned long COMMAND_TIMEOUT_MS = 1000;

// Long pan burst (face near frame edge or large offset)
const int TRACK_STEP_LONG = 3;
const unsigned long COMMAND_TIMEOUT_LONG_MS = 1800;

// Search sweep when locked face is lost
const int SEARCH_STEP = 2;
const unsigned long SEARCH_INTERVAL_MS = 70;
const unsigned long TRACK_INTERVAL_MS = 55;

const bool REVERSE_SERVO = true;

enum MovementCommand {
  CMD_IDLE,
  CMD_LEFT,
  CMD_RIGHT,
  CMD_CENTER,
  CMD_SEARCH
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Servo panServo;

MovementCommand currentCommand = CMD_IDLE;
int servoAngle = SERVO_CENTER_ANGLE;
int sweepDirection = 1;
unsigned long lastMoveAt = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastCommandAt = 0;
int activeTrackStep = TRACK_STEP;
unsigned long activeCommandTimeout = COMMAND_TIMEOUT_MS;

void setServoAngle(int angle) {
  angle = constrain(angle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
  servoAngle = angle;
  panServo.write(servoAngle);
}

void applyTrackingStep(int logicalDirection) {
  int direction = REVERSE_SERVO ? -logicalDirection : logicalDirection;
  setServoAngle(servoAngle + (direction * activeTrackStep));
}

void applyPanCommand(MovementCommand command, int step, unsigned long timeoutMs) {
  currentCommand = command;
  activeTrackStep = step;
  activeCommandTimeout = timeoutMs;
  lastCommandAt = millis();
}

void holdCurrentPosition() {
  currentCommand = CMD_IDLE;
  activeTrackStep = TRACK_STEP;
  activeCommandTimeout = COMMAND_TIMEOUT_MS;
  lastCommandAt = millis();
}

bool dispatchMovementMessage(const String& rawMessage) {
  String message = rawMessage;
  message.trim();
  message.toUpperCase();

  if (message.startsWith("CMD_")) {
    message = message.substring(4);
  }

  if (message.length() == 0) {
    return false;
  }

  if (message == "IDLE") {
    holdCurrentPosition();
    return true;
  }
  if (message == "LEFT_LONG") {
    applyPanCommand(CMD_LEFT, TRACK_STEP_LONG, COMMAND_TIMEOUT_LONG_MS);
    return true;
  }
  if (message == "RIGHT_LONG") {
    applyPanCommand(CMD_RIGHT, TRACK_STEP_LONG, COMMAND_TIMEOUT_LONG_MS);
    return true;
  }
  if (message == "LEFT") {
    applyPanCommand(CMD_LEFT, TRACK_STEP, COMMAND_TIMEOUT_MS);
    return true;
  }
  if (message == "RIGHT") {
    applyPanCommand(CMD_RIGHT, TRACK_STEP, COMMAND_TIMEOUT_MS);
    return true;
  }
  if (message == "SEARCH") {
    currentCommand = CMD_SEARCH;
    activeTrackStep = TRACK_STEP;
    activeCommandTimeout = COMMAND_TIMEOUT_MS;
    lastCommandAt = millis();
    return true;
  }
  if (message == "CENTER") {
    currentCommand = CMD_CENTER;
    activeTrackStep = TRACK_STEP;
    activeCommandTimeout = COMMAND_TIMEOUT_MS;
    lastCommandAt = millis();
    return true;
  }

  return false;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (dispatchMovementMessage(message)) {
    Serial.print("[MQTT] Received: ");
    Serial.println(message);
  } else {
    Serial.print("[MQTT] Ignored: ");
    Serial.println(message);
  }
}

void handleSerial() {
  if (!Serial.available()) {
    return;
  }

  String input = Serial.readStringUntil('\n');
  if (dispatchMovementMessage(input)) {
    Serial.print("[SERIAL] Executing: ");
    Serial.println(input);
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("[WiFi] Connecting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Failed");
  }
}

bool connectMqtt() {
  if (mqttClient.connected()) return true;

  if (millis() - lastReconnectAttempt < 5000) return false;
  lastReconnectAttempt = millis();

  Serial.print("[MQTT] Connecting...");
  if (!mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.print(" Failed, rc=");
    Serial.println(mqttClient.state());
    return false;
  }

  Serial.println(" Connected");
  if (mqttClient.subscribe(MQTT_TOPIC)) {
    Serial.print("[MQTT] Subscribed to topic: ");
    Serial.println(MQTT_TOPIC);
  } else {
    Serial.println("[MQTT] Subscribe failed");
  }
  return true;
}

void handleServo() {
  unsigned long now = millis();

  if ((now - lastCommandAt) > activeCommandTimeout) {
    currentCommand = CMD_IDLE;
  }

  if (currentCommand == CMD_IDLE) {
    return;
  }

  if (currentCommand == CMD_CENTER) {
    setServoAngle(SERVO_CENTER_ANGLE);
    currentCommand = CMD_IDLE;
    return;
  }

  if (currentCommand == CMD_SEARCH) {
    if (now - lastMoveAt < SEARCH_INTERVAL_MS) return;
    lastMoveAt = now;

    setServoAngle(servoAngle + (sweepDirection * SEARCH_STEP));
    if (servoAngle >= SERVO_MAX_ANGLE) sweepDirection = -1;
    if (servoAngle <= SERVO_MIN_ANGLE) sweepDirection = 1;
    return;
  }

  if (now - lastMoveAt < TRACK_INTERVAL_MS) return;
  lastMoveAt = now;

  if (currentCommand == CMD_LEFT) applyTrackingStep(-1);
  else if (currentCommand == CMD_RIGHT) applyTrackingStep(1);
}

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("\n[SYS] Face-tracking servo controller starting...");
  Serial.println("[SYS] Commands: IDLE, LEFT, RIGHT, LEFT_LONG, RIGHT_LONG, SEARCH, CENTER");

  panServo.attach(SERVO_PIN);
  setServoAngle(SERVO_CENTER_ANGLE);

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  connectWiFi();
  lastCommandAt = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected()) connectMqtt();

  mqttClient.loop();
  handleSerial();
  handleServo();

  yield();
}

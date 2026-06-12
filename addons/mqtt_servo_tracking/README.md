# MQTT Servo Face Tracking Extension

This extension keeps your original `src/recognize.py` untouched and adds:
- A face-lock recognizer that publishes movement commands to MQTT.
- ESP8266 firmware that subscribes and rotates a servo-mounted camera.
- Automatic left/right sweep when the locked face is not found.

## Files
- `recognize_mqtt.py`: Face recognition + lock + MQTT movement publishing
- `esp8266/face_tracker_servo/face_tracker_servo.ino`: ESP8266 firmware
- `esp8266/upload.ps1`: Upload helper using `arduino-cli`

## MQTT Protocol
Topic:
- `vision/teamalpha/movement`

Payloads:
- `LEFT`: face is left of center, rotate camera left
- `RIGHT`: face is right of center, rotate camera right
- `CENTER`: face is centered, hold position
- `SEARCH`: locked face currently missing, sweep back/forth
- `IDLE`: no active lock, hold position

## Python Setup
Install dependencies from repo root:

```bash
pip install -r requirements.txt
```

Run the MQTT-enabled recognizer:

```bash
python addons/mqtt_servo_tracking/recognize_mqtt.py
```

Optional flags:

```bash
python addons/mqtt_servo_tracking/recognize_mqtt.py \
  --mqtt-broker 157.173.101.159 \
  --mqtt-topic vision/teamalpha/movement \
  --deadzone-px 80 \
  --center-exit-hysteresis-px 30 \
  --search-delay-sec 0.8 \
  --command-confirm-frames 2 \
  --mqtt-min-interval 0.15
```

Stability tuning tips:
- Increase `--deadzone-px` if the servo keeps moving while your face is already near center.
- Increase `--search-delay-sec` if brief recognition drops trigger SEARCH too aggressively.
- Increase `--command-confirm-frames` to reduce LEFT/RIGHT flicker (slower reaction).

## ESP8266 Setup
1. Open `esp8266/face_tracker_servo/face_tracker_servo.ino`.
2. Set `WIFI_SSID` and `WIFI_PASSWORD`.
3. Adjust `SERVO_PIN`, `SERVO_MIN_ANGLE`, `SERVO_MAX_ANGLE`, and `REVERSE_SERVO` for your hardware.
4. Install Arduino libraries:
   - `PubSubClient`
   - `Servo` (ESP8266 core)
5. Compile/upload with PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File addons/mqtt_servo_tracking/esp8266/upload.ps1 -Port COM5
```

If your board is not NodeMCU v2, pass `-Fqbn` (example: `esp8266:esp8266:d1_mini`).


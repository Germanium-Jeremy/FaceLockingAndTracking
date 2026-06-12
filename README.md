# AI-Powered Single-Speaker Face Recognition & Camera Tracking

A real-time face recognition system that **locks onto one enrolled speaker**, ignores other faces in the frame, and optionally drives a **servo-mounted camera** over **MQTT** so the speaker stays centered during live presentations, lectures, or video calls.

## System Overview

| Layer | Component | Role |
|-------|-----------|------|
| Vision | USB camera | Captures live video |
| AI (PC) | Python pipeline | Detect → recognize → lock → track → publish MQTT commands |
| Network | MQTT broker (Mosquitto on PC or cloud) | Relays movement commands |
| Embedded | ESP32 / ESP8266 | Subscribes to MQTT, drives the servo |
| Actuation | Servo motor | Pans the camera horizontally |

### Integrated architecture

```mermaid
flowchart TB
    subgraph PC["PC (Python)"]
        CAM_IN[USB Camera Input]
        DET[Haar Face Detection]
        LM[MediaPipe 5-pt Landmarks]
        EMB[ArcFace Embedding]
        MATCH[Speaker Lock Matcher]
        TRACK[Horizontal Error + Dwell Gate]
        MQTT_PUB[MQTT Publisher]
        LOG[Action Logs]

        CAM_IN --> DET --> LM --> EMB --> MATCH
        MATCH --> TRACK --> MQTT_PUB
        MATCH --> LOG
    end

    subgraph NET["Wi-Fi / MQTT"]
        BROKER[(MQTT Broker)]
    end

    subgraph EMBEDDED["ESP32 / ESP8266"]
        MQTT_SUB[MQTT Subscriber]
        SERVO_DRV[Servo Driver]
        MQTT_SUB --> SERVO_DRV
    end

    CAM[USB Camera] --> CAM_IN
    MQTT_PUB -->|LEFT / RIGHT / IDLE / SEARCH| BROKER
    BROKER --> MQTT_SUB
    SERVO_DRV --> SERVO[Servo Motor]
    SERVO --> MOUNT[Camera Mount]
    MOUNT -.-> CAM
```

## Core Features

- **Face enrollment** — capture 10–30 samples and store a reusable ArcFace embedding
- **Single-identity recognition (speaker lock)** — track only the enrolled speaker; ignore others
- **Face locking & action logging** — head movement, smiles, lock/unlock events with timestamps
- **MQTT motor control** — convert horizontal tracking error into pan commands for a servo
- **Settle-and-correct tracking** — observe for 2 s, pan once, hold, repeat (reduces jitter)
- **Re-acquisition** — brief occlusion holds position; longer loss triggers SEARCH sweep

---

## Setup

**Python:** 3.10 – 3.13 (3.14 is not supported by `onnxruntime` wheels).

```bash
pip install -r requirements.txt
```

**Models** (place in `models/`):

- `embedder_arcface.onnx`
- `face_landmarker.task`

---

## Usage

### 1. Enrollment

Capture face samples and build the speaker database.

```bash
python -m src.enroll
```

| Key | Action |
|-----|--------|
| `SPACE` | Capture one sample |
| `a` | Toggle auto-capture |
| `s` | Save to database |
| `q` | Quit |

Rebuild the database from existing crops:

```bash
python -m src.rebuild_db
```

### 2. Recognition (vision only)

```bash
python -m src.recognize
```

| Key | Action |
|-----|--------|
| `l` | Lock / unlock selected face |
| `←` / `→` | Select face when multiple detected |
| `+` / `-` | Adjust match threshold |
| `r` | Reload database |
| `d` | Debug overlay |
| `q` | Quit |

On startup, choose **DirectML** (recommended on Windows), **CUDA** (NVIDIA), or **CPU**.

### 3. Recognition + MQTT servo tracking (full system)

```bash
python addons/mqtt_servo_tracking/recognize_mqtt.py --mqtt-broker <PC_LAN_IP>
```

See [`addons/mqtt_servo_tracking/README.md`](addons/mqtt_servo_tracking/README.md) for broker setup, ESP flashing, tuning, and troubleshooting.

**Test MQTT without the camera:**

```bash
python addons/mqtt_servo_tracking/mqtt_test_publish.py --mqtt-broker <PC_LAN_IP> --command cycle
```

### 4. Evaluation & demos

```bash
python -m src.evaluate
python -m src.embed
python -m src.haar_5pt
```

---

## Sequence Diagrams

### Enrollment flow

```mermaid
sequenceDiagram
    actor User
    participant Cam as USB Camera
    participant Enroll as enroll.py
    participant DB as data/db/face_db.npz

    User->>Enroll: Start enrollment
  loop Until enough samples
        Cam->>Enroll: Frame
        Enroll->>Enroll: Detect face + 5-pt landmarks
        Enroll->>Enroll: Align crop + ArcFace embedding
        User->>Enroll: SPACE / auto-capture
        Enroll->>Enroll: Store sample embedding
    end
    User->>Enroll: Press s (save)
    Enroll->>DB: Mean embedding per identity
    Enroll-->>User: Enrollment complete
```

### Recognition & speaker lock

```mermaid
sequenceDiagram
    actor User
    participant Cam as USB Camera
    participant Rec as recognize.py / recognize_mqtt.py
    participant DB as Face Database
    participant Log as logs/

    User->>Rec: Start recognition
    loop Every frame
        Cam->>Rec: Frame
        Rec->>Rec: Detect all faces
        Rec->>Rec: Embed + match each face
        Rec->>DB: Cosine distance vs enrolled IDs
        alt Face matches enrolled speaker
            Rec-->>User: Show name + confidence
        else Unknown face
            Rec-->>User: Show Unknown (ignored when locked)
        end
    end
    User->>Rec: Press l (lock)
    Rec->>Rec: Lock target embedding
    Rec->>Log: FACE_LOCKED event
    loop While locked
        Rec->>Rec: Track locked face only
        Rec->>Log: HEAD_LEFT / HEAD_RIGHT / SMILE
    end
    User->>Rec: Press l (unlock) or timeout
    Rec->>Log: Save session history
```

### Recognize → Track → Command pipeline (MQTT)

This is the per-frame control path used by `recognize_mqtt.py` when a face is locked.

```mermaid
flowchart TD
    A[Camera frame] --> B[Detect faces]
    B --> C{Locked face<br/>in frame?}
    C -- No lock --> D[Publish IDLE]
    C -- Lock, face missing --> E{Lost longer than<br/>search-delay-sec?}
    E -- No --> F[Publish IDLE<br/>hold position]
    E -- Yes --> G[Publish SEARCH]
    C -- Lock, face found --> H[Compute horizontal error err_x]
    H --> I[Smooth error EMA]
    I --> J{Inside deadzone<br/>or below pan-trigger?}
    J -- Yes --> K[Target: aligned]
    J -- No --> L{Near frame edge<br/>or large offset?}
    L -- Yes --> M[Target: LEFT_LONG / RIGHT_LONG]
    L -- No --> N[Target: LEFT / RIGHT]
    K --> O{Dwell window<br/>movement-settle-sec}
    M --> O
    N --> O
    O -- Still settling --> P[Publish IDLE<br/>servo holds angle]
    O -- Dwell complete --> Q[Publish pan command]
    P --> R[ESP: hold position]
    Q --> S[ESP: fixed pulse<br/>4 or 7 steps]
    S --> T[ESP: auto IDLE]
    G --> U[ESP: sweep search]
```

### Movement settle cycle (one correction)

```mermaid
sequenceDiagram
    participant Rec as recognize_mqtt.py
    participant Broker as MQTT Broker
    participant ESP as ESP32 / ESP8266
    participant Servo as Servo Motor

    Note over Rec,Servo: Face locked and visible

    loop Settle window (~2 s default)
        Rec->>Broker: IDLE
        Broker->>ESP: IDLE
        ESP->>Servo: Hold current angle
        Rec->>Rec: Measure face position
    end

    alt Face aligned in frame
        Rec->>Broker: IDLE
        Broker->>ESP: IDLE
    else Face off-center (moderate)
        Rec->>Broker: LEFT or RIGHT
        Broker->>ESP: Pan pulse (~4 steps)
        ESP->>Servo: Short smooth rotation
        ESP->>Servo: Auto-hold
    else Face near edge / large offset
        Rec->>Broker: LEFT_LONG or RIGHT_LONG
        Broker->>ESP: Pan pulse (~7 steps)
        ESP->>Servo: Longer smooth rotation
        ESP->>Servo: Auto-hold
    end

    Note over Rec,Servo: Repeat settle → correct → settle
```

### End-to-end integrated session

```mermaid
sequenceDiagram
    actor Speaker
    participant Cam as Camera
    participant PC as recognize_mqtt.py
    participant MQTT as MQTT Broker
    participant ESP as ESP Board
    participant Servo as Servo

    Speaker->>PC: System start (enrolled identity in DB)
    PC->>Cam: Open stream
    Speaker->>PC: Press l to lock
    loop Tracking session
        Cam->>PC: Frame
        PC->>PC: Recognize + lock match
        alt Locked face centered
            PC->>MQTT: IDLE
        else Needs pan correction
            PC->>MQTT: LEFT / RIGHT / LEFT_LONG / RIGHT_LONG
        else Face temporarily lost
            PC->>MQTT: IDLE then SEARCH
        end
        MQTT->>ESP: Movement command
        ESP->>Servo: Pan or hold
        Servo->>Cam: Camera repositions
        Cam->>PC: Updated framing
    end
    PC->>PC: Write logs/*.txt
```

---

## MQTT + Servo Extension

Full details: [`addons/mqtt_servo_tracking/README.md`](addons/mqtt_servo_tracking/README.md)

| Item | Location / value |
|------|------------------|
| Python app | `addons/mqtt_servo_tracking/recognize_mqtt.py` |
| ESP firmware | `addons/mqtt_servo_tracking/esp8266/face_tracker_servo/face_tracker_servo.ino` |
| Upload helper | `addons/mqtt_servo_tracking/esp8266/upload.ps1` |
| MQTT test tool | `addons/mqtt_servo_tracking/mqtt_test_publish.py` |
| Broker example | PC LAN IP (e.g. `192.168.1.194`) or `broker.hivemq.com` |
| Topic example | `vision/teamalpha/movement/Jeremie` |

**MQTT payloads**

| Command | Meaning on ESP |
|---------|----------------|
| `IDLE` | Hold current servo angle |
| `LEFT` / `RIGHT` | Short pan pulse (~4°) |
| `LEFT_LONG` / `RIGHT_LONG` | Longer pan pulse (~14°) near frame edge |
| `SEARCH` | Sweep while locked face is missing |
| `CENTER` | Snap to home angle (debug only; not used in tracking) |

**Key tuning flags**

```bash
python addons/mqtt_servo_tracking/recognize_mqtt.py \
  --mqtt-broker 192.168.1.194 \
  --mqtt-topic vision/teamalpha/movement/Jeremie \
  --movement-settle-sec 2.0 \
  --deadzone-px 80 \
  --pan-trigger-px 110 \
  --edge-correction-px 120 \
  --search-delay-sec 2.0
```

---

## Face Locking & Logging

### How locking works

1. Press `l` when the enrolled speaker is recognized (use `←` / `→` if multiple faces).
2. The locked face gets an orange border; other faces are ignored for tracking.
3. Actions are logged to `logs/[Name]_history_[timestamp].txt`.
4. Lock releases on `l`, timeout (~40 s without sight), or quit.

### Log format

```
2026-06-12 15:16:45.443268 - FACE_LOCKED: Face locked: Jeremie
2026-06-12 15:16:48.640953 - HEAD_RIGHT: Moved right by 10.9px
2026-06-12 15:17:06.928153 - SMILE: Smile detected (ratio: 51.66)
```

---

## Project Structure

```
FaceLockingAndTracking/
├── src/                          # Core vision pipeline
│   ├── enroll.py                 # Speaker enrollment
│   ├── recognize.py              # Multi-face recognition + lock
│   ├── detect.py, landmarks.py, align.py
│   └── ...
├── addons/mqtt_servo_tracking/   # MQTT + servo extension
│   ├── recognize_mqtt.py         # Recognition + MQTT publishing
│   ├── mqtt_test_publish.py      # Broker / ESP test tool
│   └── esp8266/face_tracker_servo/
├── data/db/face_db.npz           # Enrolled embeddings
├── data/enroll/                  # Enrollment crops
├── logs/                         # Session action logs
└── models/                       # ONNX + MediaPipe models
```

---

## Technical Stack

| Area | Technology |
|------|------------|
| Detection | OpenCV Haar cascade (multi-face) |
| Landmarks | MediaPipe FaceMesh (5-point per ROI) |
| Recognition | ArcFace ONNX embeddings + cosine distance |
| Vision runtime | OpenCV, NumPy, ONNX Runtime (CPU / CUDA / DirectML) |
| MQTT | paho-mqtt + Mosquitto |
| Embedded | ESP32 / ESP8266, PubSubClient, Servo library |

---

## Hardware Notes

- **Servo power:** use a dedicated 5 V supply; share ground with the ESP.
- **Wi-Fi:** PC and ESP must be on the same network when using a local broker.
- **Broker binding:** Mosquitto on Windows must listen on `0.0.0.0:1883`, not only `127.0.0.1`.
- **Serial monitor:** 115200 baud for ESP debug output.

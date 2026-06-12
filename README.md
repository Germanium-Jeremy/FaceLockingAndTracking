# Face Recognition with Real-time Tracking

A real-time face recognition system with face locking capabilities, designed to identify and track specific individuals while monitoring their actions.

## Core Features
- **Face Recognition**: 
  - Real-time face detection and identification
  - Multi-person support
  - High accuracy with ArcFace embeddings

- **Face Locking**:
  - Track specific individuals in real-time
  - Monitor head movements (left/right)
  - Detect smiles and facial expressions
  - Automatic action logging with timestamps

## Setup

**Python version:** Use **Python 3.10, 3.11, 3.12, or 3.13**. Python 3.14 is not yet supported (e.g. `onnxruntime` has no wheels for 3.14).

1. **Install Dependencies**:
   ```bash
   pip install -r requirements.txt
   ```
   
   **Note for GPU acceleration:** The requirements include `onnxruntime-gpu` for NVIDIA GPU support. If you prefer CPU-only, you can replace it with `onnxruntime` in `requirements.txt`.

2. **Models**:
   Ensure the following models are in the `models/` directory:
   - `embedder_arcface.onnx` (ArcFace recognition)
   - `face_landmarker.task` (MediaPipe FaceMesh)

## Usage

### 1. Enrollment
Enroll new identities by capturing face samples from the camera.
```bash
python -m src.enroll
```
- **Controls**:
  - `SPACE`: Capture a single sample.
  - `a`: Toggle auto-capture.
  - `s`: Save enrollment to database.
  - `q`: Quit.

**Rebuilding Database from Existing Crops:**
If you have enrollment crops in `data/enroll/` but the database is missing or outdated, you can rebuild it:
```bash
python -m src.rebuild_db
```
This scans all person folders in `data/enroll/`, re-embeds their crops, and rebuilds the database. Useful if multiple people were enrolled but only some are in the database.

### 2. Recognition
Run real-time multi-face recognition with face locking and action tracking.
```bash
python -m src.recognize
```

**Execution Provider Selection:**
On startup, you'll be prompted to choose CPU or GPU acceleration:
- **GPU (DirectML)**: **Recommended** - Works with AMD/NVIDIA/Intel GPUs on Windows, no CUDA version conflicts
- **GPU (CUDA)**: Fastest for NVIDIA GPUs (requires CUDA 12.x - may have version conflicts)
- **CPU**: Compatible everywhere, slower but more stable

**Note:** If you see CUDA errors about missing DLLs (e.g., `cublasLt64_12.dll`), use DirectML instead. It works with your GPU without CUDA version issues.

**Camera Resolution:**
With GPU acceleration, the system uses 1280x720 by default (configurable in code). Higher resolution improves face detection quality. The GPU can handle 1920x1080 if your camera supports it.

**Controls**:
  - `+/-`: Adjust distance threshold live (default: 0.40, lower = stricter).
  - `r`: Reload database from disk.
  - `d`: Toggle debug overlay.
  - `l`: Lock/unlock the currently recognized face.
  - `q`: Quit.

**Accuracy Tips:**
- The system uses temporal smoothing (averaging recent embeddings) for more stable recognition.
- If faces aren't recognized, try increasing the threshold with `+` key.
- If you get false positives, decrease the threshold with `-` key.
- Default threshold (0.40) balances accuracy and recall - adjust based on your needs.

#### Face Locking Features
- Lock onto a specific person by pressing `l` when their face is detected
- The system will track the locked face and log their actions
- Actions include:
  - Head movements (left/right)
  - Smile detection
  - Face locking/unlocking events
- All actions are timestamped and saved to `logs/[Name]_history_[timestamp].txt`

### 3. Evaluation
Evaluate the model's performance on enrolled crops and find the optimal threshold.
```bash
python -m src.evaluate
```

### 4. Demos
Visualize embeddings or detection/landmarks:
```bash
python -m src.embed    # Embedding heatmap visualization
python -m src.haar_5pt  # Detection and landmark visualization
```

## Face Locking System

### How Face Locking Works
1. **Activation**: 
   - Press 'l' when a recognized face is on screen
   - The system will lock onto the closest recognized face
   - A visual indicator (orange border) shows the locked face

2. **Tracking**:
   - The system continues to track the locked face even if other faces appear
   - If the face is temporarily lost, the system will try to reacquire it for 2 seconds
   - The lock is automatically released if the face is not found after this period

3. **Actions Detected**:
   - **Head Movements**:
     - Left/Right: Tracks horizontal head rotation
     - Movement threshold: 10 pixels (adjustable)
   - **Facial Expressions**:
     - Smile detection: Measures mouth corner movement
     - Smile threshold: Ratio > 1.1 (configurable)
   - **System Events**:
     - Face locked/unlocked
     - Face lost/regained

### History Files
- **Location**: All logs are saved in the `logs/` directory
- **Naming Convention**: `[Name]_history_[timestamp].txt`
  - Example: `Bahati_history_20260131132049.txt`
- **Log Format**:
  ```
  [YYYY-MM-DD HH:MM:SS.microseconds] - ACTION_TYPE: Description
  ```
- **Example Entries**:
  ```
  2026-01-31 13:20:11.324267 - FACE_LOCKED: Face locked: Bahati
  2026-01-31 13:20:20.225528 - HEAD_RIGHT: Moved right by 31.9px
  2026-01-31 13:20:21.684933 - SMILE: Smile detected (ratio: 14.97)
  ```
- **Log Management**:
  - New log file created for each recognition session
  - Timestamp in filename helps track different usage sessions
  - Logs are automatically created when face locking is used

### Technical Details
- **Face Recognition**: Uses ArcFace for generating unique face embeddings
- **Landmark Tracking**: MediaPipe FaceMesh tracks 5 key facial points
- **Performance**: Optimized for real-time processing on CPU

## MQTT + ESP8266 Servo Extension

A separate extension was added in [`addons/mqtt_servo_tracking`](addons/mqtt_servo_tracking) so the original `src/recognize.py` stays unchanged.

- Python app: `addons/mqtt_servo_tracking/recognize_mqtt.py`
- Broker: `157.173.101.159`
- Topic: `vision/teamalpha/movement`
- Commands: `LEFT`, `RIGHT`, `CENTER`, `SEARCH`, `IDLE`
- ESP8266 firmware: `addons/mqtt_servo_tracking/esp8266/face_tracker_servo/face_tracker_servo.ino`
- Upload helper: `addons/mqtt_servo_tracking/esp8266/upload.ps1`

Run:
```bash
python addons/mqtt_servo_tracking/recognize_mqtt.py
```

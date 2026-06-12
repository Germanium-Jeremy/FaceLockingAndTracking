"""
Publish test movement commands to the MQTT topic used by the ESP8266 firmware.

Usage (from repo root):
  python addons/mqtt_servo_tracking/mqtt_test_publish.py
  python addons/mqtt_servo_tracking/mqtt_test_publish.py --command LEFT --repeat 5
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from addons.mqtt_servo_tracking.recognize_mqtt import (  # noqa: E402
    MOVEMENT_CENTER,
    MOVEMENT_IDLE,
    MOVEMENT_LEFT,
    MOVEMENT_RIGHT,
    MOVEMENT_SEARCH,
    MqttMovementPublisher,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Publish MQTT movement test commands.")
    parser.add_argument("--mqtt-broker", default="broker.hivemq.com")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--mqtt-topic", default="vision/teamalpha/movement/Jeremie")
    parser.add_argument(
        "--command",
        default="cycle",
        choices=["cycle", MOVEMENT_LEFT, MOVEMENT_RIGHT, MOVEMENT_CENTER, MOVEMENT_SEARCH, MOVEMENT_IDLE],
        help="Command to publish, or 'cycle' to rotate through all commands.",
    )
    parser.add_argument("--repeat", type=int, default=3, help="How many times to publish each command.")
    parser.add_argument("--interval", type=float, default=1.0, help="Seconds between publishes.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    commands = (
        [MOVEMENT_LEFT, MOVEMENT_RIGHT, MOVEMENT_CENTER, MOVEMENT_SEARCH, MOVEMENT_IDLE]
        if args.command == "cycle"
        else [args.command]
    )

    publisher = MqttMovementPublisher(
        broker_host=args.mqtt_broker,
        broker_port=args.mqtt_port,
        topic=args.mqtt_topic,
        client_id=f"mqtt-test-{int(time.time())}",
        min_publish_interval=0.0,
    )
    if not publisher.connected:
        print("[MQTT] Could not connect to broker. Check network/firewall and broker host.")
        publisher.close()
        return 1

    print(f"[MQTT] Publishing to {args.mqtt_topic} @ {args.mqtt_broker}:{args.mqtt_port}")
    for _ in range(max(1, args.repeat)):
        for command in commands:
            publisher.publish(command, force=True)
            print(f"[MQTT] Published: {command}")
            time.sleep(max(0.1, args.interval))

    publisher.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

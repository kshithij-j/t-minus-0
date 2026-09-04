import json
import asyncio
import serial
from typing import Set
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
import paho.mqtt.client as mqtt

# --- CONFIGURATION ---
SERIAL_PORT = 'COM6' # Update to your Gateway's COM port
BAUD_RATE = 115200

BROKER = "broker.emqx.io"
PORT = 1883
TOPIC = "terraguard/hackathon/panel4/telemetry/998877"

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

connected_clients: Set[WebSocket] = set()
main_loop = None

# AI/Math State variables
previous_tilts = {}
previous_tilt_velocities = {}
node_max_scores = {}

# MQTT Setup
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("[MQTT] Backend connected to EMQX Broker!")
    else:
        print(f"[MQTT WARN] Failed to connect, return code {rc}")

mqtt_client.on_connect = on_connect

def process_ai_logic(payload):
    """
    Implements CIMFR Saito's Method for Tertiary Creep.
    Calculates velocity (dθ/dt) and acceleration (d²θ/dt²).
    Filters high vibration (machinery noise) vs. accelerating tilt (tertiary creep).
    """
    global previous_tilts, previous_tilt_velocities, node_max_scores

    nodes = payload.get("nodes", {})
    state = payload.get("state", "NORMAL")
    processed_nodes = {}
    max_anomaly_score = 0

    if state == "NORMAL":
        node_max_scores.clear()

    for node_id, data in nodes.items():
        tilt = data.get("tilt", 0.0)
        vib = data.get("vibration", 0.0)

        # Velocity (dθ/dt)
        prev_tilt = previous_tilts.get(node_id, tilt)
        velocity = tilt - prev_tilt

        # Acceleration (d²θ/dt²)
        prev_vel = previous_tilt_velocities.get(node_id, velocity)
        acceleration = velocity - prev_vel

        # Update tracking values
        previous_tilts[node_id] = tilt
        previous_tilt_velocities[node_id] = velocity

        anomaly_score = 0
        abs_accel = abs(acceleration)

        # Machinery noise filter (high vibration, zero tilt acceleration)
        if vib > 5.0 and abs_accel < 0.001:
            anomaly_score = 5
        # Accelerating tilt indicates creep
        elif abs_accel >= 0.001:
            anomaly_score = min(100, int((abs_accel / 0.05) * 100))

        if state == "COLLAPSE":
            node_max_scores[node_id] = max(node_max_scores.get(node_id, 0), anomaly_score)
            anomaly_score = node_max_scores[node_id]

        max_anomaly_score = max(max_anomaly_score, anomaly_score)

        processed_nodes[node_id] = {
            "tilt": tilt,
            "vibration": vib,
            "acceleration": round(acceleration, 5),
            "anomaly_score": anomaly_score
        }

    return {
        "timestamp": payload.get("timestamp", 0),
        "simulator_state": state,
        "global_anomaly_score": max_anomaly_score,
        "nodes": processed_nodes
    }

async def broadcast_to_clients(data):
    """Broadcasts processed telemetry to all connected React clients."""
    if not connected_clients:
        return

    disconnected = set()
    for client in list(connected_clients):
        try:
            await client.send_json(data)
        except Exception:
            disconnected.add(client)

    for client in disconnected:
        connected_clients.discard(client)

async def read_serial_loop():
    """Reads USB Serial from Gateway, processes AI logic, publishes to MQTT, and updates React."""
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            print(f"[SERIAL] Connected to Gateway on {SERIAL_PORT}")
            
            while True:
                if ser.in_waiting > 0:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    # --- DEBUG CATCHER ---
                    if line:
                        print(f"[RAW SERIAL] {line}")
                    # ---------------------

                    if line.startswith('{') and line.endswith('}'):
                        try:
                            raw_payload = json.loads(line)
                            
                            # Publish raw data to MQTT cloud broker
                            mqtt_client.publish(TOPIC, line)
                            
                            # Process data through CIMFR Saito AI logic
                            processed_data = process_ai_logic(raw_payload)
                            
                            # Print live terminal log
                            print(f"[DATA INGEST] Score: {processed_data['global_anomaly_score']} | State: {processed_data['simulator_state']}")
                            
                            # Send processed payload to React WebSocket UI
                            await broadcast_to_clients(processed_data)

                        except json.JSONDecodeError:
                            print("[ERROR] Failed to parse JSON payload.")
                await asyncio.sleep(0.01)

        except serial.SerialException:
            print(f"[SERIAL WARN] Port {SERIAL_PORT} unavailable. Retrying in 3 seconds...")
            await asyncio.sleep(3)

@app.on_event("startup")
async def startup_event():
    global main_loop
    main_loop = asyncio.get_running_loop()

    # Connect to MQTT
    try:
        mqtt_client.connect(BROKER, PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"[MQTT WARN] Failed to connect to broker: {e}")

    # Launch USB Serial reader in background
    asyncio.create_task(read_serial_loop())

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.add(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        connected_clients.discard(websocket)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
TerraGuard: IoT Structural Health Monitoring System
An edge-to-cloud structural health monitoring (SHM) pipeline designed for real-time geotechnical anomaly detection. The system uses a wireless ESP32 sensor mesh to track structural tilt and vibration, processing data through the CIMFR Saito Method to predict tertiary creep and structural collapse.

System Architecture
Node 1 (Sensor Node): Equipped with an MPU6050 accelerometer/gyroscope and an nRF24L01 radio. It features smart edge-event triggering: a 7-second routine heartbeat combined with a hardware interrupt filter (4-second cooldown) to instantly capture sudden physical shocks.

Node 0 (Gateway Node): Receives wireless packets over the nRF24Mesh network and bridges them to the host machine via USB Serial (UART).

Backend (main.py): A FastAPI application that reads raw serial data, publishes payloads to the EMQX public cloud broker via MQTT, evaluates anomaly scores using CIMFR Saito’s tertiary creep logic, and broadcasts live data to clients over WebSockets.

Frontend: A React dashboard connecting via WebSockets for real-time visualization of structural telemetry and global risk metrics.
Tech Stack
Microcontrollers: ESP32 DevKit V1 (C++ / Arduino framework)

Sensors & Radio: MPU6050 6-axis IMU, nRF24L01+ transceiver (RF24, RF24Network, RF24Mesh libraries)

Backend: Python 3.10+, FastAPI, Uvicorn, Pyserial, Paho-MQTT

Cloud & Protocol: EMQX MQTT Broker (broker.emqx.io), WebSockets

Quick Start Guide
1. Hardware Setup
Connect the MPU6050 to Node 1 via I2C (SDA: GPIO 21, SCL: GPIO 22, INT: GPIO 25).

Connect the nRF24L01 radio via SPI (MOSI: GPIO 23, MISO: GPIO 19, SCK: GPIO 18, CSN: GPIO 5, CE: GPIO 4).

Flash the Node 1 firmware using PlatformIO or the Arduino IDE.

2. Backend Execution
Plug the Gateway (Node 0) into your machine and check your system's Device Manager for the active COM port (e.g., COM3 or /dev/ttyUSB0).

Install Python dependencies:

Bash
pip install fastapi uvicorn pyserial paho-mqtt
Update SERIAL_PORT in main.py to match your Gateway port.

Run the backend server:

Bash
python main.py
3. Dashboard Integration
Start your React frontend and open the WebSocket endpoint at ws://localhost:8000/ws to consume live processed telemetry streams

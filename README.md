# Iot Mine Subsidence Monitoring

An edge-to-cloud Structural Health Monitoring (SHM) system designed for real-time geotechnical anomaly detection.

The system uses a wireless ESP32 sensor mesh to monitor structural tilt and vibration. The collected data is processed using the CIMFR Saito Method to analyze tertiary creep and potential structural failure.

## Connections

**MPU6050**

* VCC → ESP32 3.3V
* GND → ESP32 GND
* SDA → GPIO 21
* SCL → GPIO 22
* INT → GPIO 27

**nRF24L01**

* VCC → ESP32 3.3V
* GND → ESP32 GND
* CE → GPIO 4
* CSN → GPIO 5
* SCK → GPIO 18
* MOSI → GPIO 23
* MISO → GPIO 19

Gateway ESP32 → Host PC through USB for Serial communication.

## System Architecture

### Node 1 - Sensor Node

* ESP32
* MPU6050 accelerometer/gyroscope
* nRF24L01 radio
* 7-second routine heartbeat
* Hardware interrupt for sudden physical events
* 4-second cooldown after an event

### Node 0 - Gateway Node

* Receives wireless packets through nRF24Mesh
* Sends data to the host machine through USB Serial (UART)

### Backend - main.py

* FastAPI application
* Reads data from the serial connection
* Publishes data to EMQX using MQTT
* Performs CIMFR Saito tertiary creep analysis
* Calculates anomaly scores
* Sends live data to the frontend using WebSockets

### Frontend

* React dashboard
* Real-time structural telemetry
* Tilt and vibration visualization
* Global risk metrics
* WebSocket-based live updates

## Data Flow

ESP32 Sensor Node
↓
nRF24Mesh
↓
Gateway Node
↓
USB Serial
↓
FastAPI Backend
↓
MQTT / EMQX
↓
CIMFR Saito Analysis
↓
WebSocket
↓
React Dashboard

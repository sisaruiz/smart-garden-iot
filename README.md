# Smart Garden IoT System

This project was developed for the **Internet of Things (IoT)** course of the *Master’s Degree in Artificial Intelligence and Data Engineering* at the University of Pisa.  
It implements a smart garden management system using IoT devices to monitor environmental parameters and control actuators, following the course’s solo project requirements.

## Overview

The system combines MQTT-based sensors, CoAP-based actuators, and a Java backend application with MySQL storage.  
It monitors soil and environmental data and controls irrigation, lighting, ventilation, and fertilizer dispensing.  
The list of devices is static and loaded from a configuration file.

## Technologies

- **Contiki-NG** – Operating system for IoT devices, used for the firmware of CoAP actuators and MQTT sensors.
- **nRF52840 USB Dongles** – Used as IoT nodes and as a border router.
- **Cooja Simulator** – For testing firmware before deployment.
- **Java** – Backend implementation of control logic, CLI, and integration with MQTT/CoAP.
- **Eclipse Californium** – CoAP framework for Java.
- **Eclipse Paho** – MQTT client for Java.
- **Eclipse Mosquitto** – MQTT broker.
- **MySQL** – Database for sensor data.
## Architecture

### 1. MQTT Sensor Node
- Runs on a dedicated nRF52840 dongle (or simulated in Cooja).
- Simulates:
  - Soil pH
  - Soil moisture
  - Light
  - Temperature
- Publishes JSON-formatted readings to Mosquitto.

### 2. CoAP Actuator Node
- Runs on a dedicated nRF52840 dongle.
- Actuators:
  - Irrigation
  - Grow light
  - Fertilizer dispenser
  - Ventilator
- Supports manual triggering from CLI and automatic control from backend logic.

## How to Run

**Prerequisites:**
- Contiki-NG installed and configured.
- Virtual Machine set up with USB pass-through enabled for nRF52840 dongles.
- Mosquitto MQTT broker running.
- MySQL database running and configured for the Java backend.

**Important:** Only plug in **one dongle at a time** during flashing and setup.

### 1. Border Router Setup
1. Plug in the first nRF52840 dongle.
2. Press the reset button until the red LED flickers (bootloader mode).
3. In the VM, select the dongle under USB devices.
4. From `contiki-ng/examples/rpl-border-router`, run:
   ```bash
   make TARGET=nrf52840 BOARD=dongle PORT=/dev/ttyACM0 border-router.dfu-upload
   make TARGET=nrf52840 BOARD=dongle PORT=/dev/ttyACM0 connect-router
5. If the device disconnects, re-select it in the VM’s USB device list.

### 2. MQTT Device Setup
1. Unplug the first dongle and insert the second one.
2. Enter bootloader mode (red LED flickering) and connect it to the VM.
3. From the Contiki project directory, run:
   ```bash
   make TARGET=nrf52840 BOARD=dongle mqtt-device.dfu-upload PORT=/dev/ttyACM1
   make login TARGET=nrf52840 BOARD=dongle PORT=/dev/ttyACM1
   
### 3. CoAP Device Setup
1. Unplug the second dongle and insert the third one.
2. Enter bootloader mode and connect it to the VM.
3. Run:
   ```bash
   make TARGET=nrf52840 BOARD=dongle coap-device.dfu-upload PORT=/dev/ttyACM2
   make login TARGET=nrf52840 BOARD=dongle PORT=/dev/ttyACM2
   
### 4. Run the Java Backend
1. Go to the `smart-garden` project folder.
2. Run the application:
   ```bash
   java -jar target/smartgarden-app-1.0-SNAPSHOT-jar-with-dependencies.jar

The system is now ready!

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

### 3. Java Backend Application
- Subscribes to MQTT sensor topics and stores data in MySQL.
- CLI to query sensors and control actuators.
- Sends CoAP requests to actuators using Californium.
- Automatic control logic based on sensor thresholds.

## Features

- Manual and automatic actuator control.
- Static configuration file for device definitions.
- Integration with real hardware (nRF52840 dongles).
- Sensor data persistence in MySQL.
- Command-line interface for operation.

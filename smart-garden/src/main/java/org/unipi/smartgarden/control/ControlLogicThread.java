package org.unipi.smartgarden.control;

import org.eclipse.californium.elements.exception.ConnectorException;
import org.unipi.smartgarden.coap.COAPNetworkController;
import org.unipi.smartgarden.mqtt.MQTTHandler;
import org.unipi.smartgarden.util.ConsoleUtils;

import java.io.IOException;

/**
 * ControlLogicThread - Periodically checks sensor values and sends commands
 * to actuators to maintain optimal environmental conditions in the smart garden.
 */
public class ControlLogicThread extends Thread {

    private final MQTTHandler mqttHandler;
    private final COAPNetworkController coapController;
    private volatile boolean running = true;

    private static final long SLEEP_INTERVAL_MS = 10_000; // 10 seconds

    // Thresholds (you may move these to config later)
    private static final float TEMP_LOWER = 18.0f;
    private static final float TEMP_UPPER = 26.0f;

    private static final float PH_LOWER = 6.0f;
    private static final float PH_UPPER = 7.5f;

    private static final float MOISTURE_LOWER = 35.0f;
    private static final float MOISTURE_UPPER = 70.0f;

    private static final float LIGHT_LOWER = 300.0f;  // e.g., lux
    private static final float LIGHT_UPPER = 1000.0f;

    public ControlLogicThread(MQTTHandler mqttHandler, COAPNetworkController coapController) {
        this.mqttHandler = mqttHandler;
        this.coapController = coapController;
    }

    @Override
    public void run() {
        while (running) {
            try {
                Thread.sleep(SLEEP_INTERVAL_MS);
            } catch (InterruptedException e) {
                ConsoleUtils.printError("[Control Logic] Sleep interrupted.");
                break;
            }

            try {
                checkTemperature();
            } catch (ConnectorException | IOException e) {
                throw new RuntimeException(e);
            }
            checkPH();
            checkSoilMoisture();
            checkLight();
        }

        ConsoleUtils.println("[Control Logic] Thread stopped.");
    }

    public void stopThread() {
        running = false;
    }

    private void checkTemperature() throws ConnectorException, IOException {
        Float temperature = mqttHandler.getLatestValue("temperature");
        if (temperature == null) return;

        String fanState = coapController.getActuatorState("fan");
        String heaterState = coapController.getActuatorState("heater");

        if (temperature < TEMP_LOWER) {
            ConsoleUtils.println("[Control Logic] Temperature too low: " + temperature);
            mqttHandler.simulateHeater("on");
            if (!"on".equalsIgnoreCase(heaterState)) {
                coapController.sendCommand("heater", "on");
            }
            if (!"off".equalsIgnoreCase(fanState)) {
                coapController.sendCommand("fan", "off");
            }
        } else if (temperature > TEMP_UPPER) {
            ConsoleUtils.println("[Control Logic] Temperature too high: " + temperature);
            mqttHandler.simulateFan("on");
            if (!"on".equalsIgnoreCase(fanState)) {
                coapController.sendCommand("fan", "on");
            }
            if (!"off".equalsIgnoreCase(heaterState)) {
                coapController.sendCommand("heater", "off");
            }
        } else {
            mqttHandler.simulateHeater("off");
            mqttHandler.simulateFan("off");
            if (!"off".equalsIgnoreCase(heaterState)) {
                coapController.sendCommand("heater", "off");
            }
            if (!"off".equalsIgnoreCase(fanState)) {
                coapController.sendCommand("fan", "off");
            }
        }
    }

    private void checkPH() {
        Float pH = mqttHandler.getLatestValue("pH");
        if (pH == null) return;

        try {
            if (pH < PH_LOWER) {
                ConsoleUtils.println("[Control Logic] pH too low: " + pH);
                mqttHandler.simulateFertilizer("sinc");
                coapController.sendCommand("fertilizer", "sinc");
            } else if (pH > PH_UPPER) {
                ConsoleUtils.println("[Control Logic] pH too high: " + pH);
                mqttHandler.simulateFertilizer("sdec");
                coapController.sendCommand("fertilizer", "sdec");
            } else {
                mqttHandler.simulateFertilizer("off");
                coapController.sendCommand("fertilizer", "off");
            }
        } catch (ConnectorException | IOException e) {
            throw new RuntimeException(e);
        }
    }

    private void checkSoilMoisture() {
        Float moisture = mqttHandler.getLatestValue("soilMoisture");
        if (moisture == null) return;

        try {
            String irrigationState = coapController.getActuatorState("irrigation");

            if (moisture < MOISTURE_LOWER) {
                ConsoleUtils.println("[Control Logic] Soil moisture too low: " + moisture);
                mqttHandler.simulateIrrigation("on");
                if (!"on".equalsIgnoreCase(irrigationState)) {
                    coapController.sendCommand("irrigation", "on");
                }
            } else if (moisture > MOISTURE_UPPER) {
                mqttHandler.simulateIrrigation("off");
                if (!"off".equalsIgnoreCase(irrigationState)) {
                    coapController.sendCommand("irrigation", "off");
                }
            }
        } catch (ConnectorException | IOException e) {
            throw new RuntimeException(e);
        }
    }

    private void checkLight() {
        Float light = mqttHandler.getLatestValue("light");
        if (light == null) return;

        try {
            String lightState = coapController.getActuatorState("grow_light");

            if (light < LIGHT_LOWER) {
                ConsoleUtils.println("[Control Logic] Light too low: " + light);
                mqttHandler.simulateGrowLight("on");
                if (!"on".equalsIgnoreCase(lightState)) {
                    coapController.sendCommand("grow_light", "on");
                }
            } else if (light > LIGHT_UPPER) {
                mqttHandler.simulateGrowLight("off");
                if (!"off".equalsIgnoreCase(lightState)) {
                    coapController.sendCommand("grow_light", "off");
                }
            }
        } catch (ConnectorException | IOException e) {
            throw new RuntimeException(e);
        }
    }
}


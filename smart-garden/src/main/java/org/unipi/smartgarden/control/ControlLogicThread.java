package org.unipi.smartgarden.control;

import org.eclipse.californium.elements.exception.ConnectorException;
import org.unipi.smartgarden.coap.COAPNetworkController;
import org.unipi.smartgarden.mqtt.MQTTHandler;

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
                e.printStackTrace();
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

        System.out.println("[Control Logic] Thread stopped.");
    }

    public void stopThread() {
        running = false;
    }

    private void checkTemperature() throws ConnectorException, IOException {
        Float temperature = mqttHandler.getLatestValue("temperature");
        if (temperature == null) return;

        if (temperature < TEMP_LOWER) {
            System.out.println("[Control Logic] Temperature too low: " + temperature);
            mqttHandler.simulateHeater("on");
            coapController.triggerHeater(true);
        } else if (temperature > TEMP_UPPER) {
            System.out.println("[Control Logic] Temperature too high: " + temperature);
            mqttHandler.simulateFan("on");
            coapController.triggerFan(true);
        } else {
            mqttHandler.simulateHeater("off");
            mqttHandler.simulateFan("off");
            coapController.triggerHeater(false);
            coapController.triggerFan(false);
        }
    }

    private void checkPH() {
        Float pH = mqttHandler.getLatestValue("pH");
        if (pH == null) return;

        if (pH < PH_LOWER) {
            System.out.println("[Control Logic] pH too low: " + pH);
            mqttHandler.simulateFertilizer("sinc");
            try {
                coapController.sendCommand("fertilizer", "sinc");
            } catch (ConnectorException | IOException e) {
                throw new RuntimeException(e);
            }
        } else if (pH > PH_UPPER) {
            System.out.println("[Control Logic] pH too high: " + pH);
            mqttHandler.simulateFertilizer("sdec");
            try {
                coapController.sendCommand("fertilizer", "sdec");
            } catch (ConnectorException | IOException e) {
                throw new RuntimeException(e);
            }
        } else {
            mqttHandler.simulateFertilizer("off");
            try {
                coapController.sendCommand("fertilizer", "off");
            } catch (ConnectorException | IOException e) {
                throw new RuntimeException(e);
            }
        }
    }

    private void checkSoilMoisture() {
        Float moisture = mqttHandler.getLatestValue("soilMoisture");
        if (moisture == null) return;

        if (moisture < MOISTURE_LOWER) {
            System.out.println("[Control Logic] Soil moisture too low: " + moisture);
            mqttHandler.simulateIrrigation("on");
            try {
                coapController.sendCommand("irrigation", "on");
            } catch (ConnectorException | IOException e) {
                throw new RuntimeException(e);
            }
        } else if (moisture > MOISTURE_UPPER) {
            mqttHandler.simulateIrrigation("off");
            try {
                coapController.sendCommand("irrigation", "off");
            } catch (ConnectorException | IOException e) {
                throw new RuntimeException(e);
            }
        }
    }

    private void checkLight() {
        Float light = mqttHandler.getLatestValue("light");
        if (light == null) return;

        if (light < LIGHT_LOWER) {
            System.out.println("[Control Logic] Light too low: " + light);
            mqttHandler.simulateGrowLight("on");
            try {
                coapController.sendCommand("grow_light", "on");
            } catch (ConnectorException | IOException e) {
                throw new RuntimeException(e);
            }
        } else if (light > LIGHT_UPPER) {
            mqttHandler.simulateGrowLight("off");
            try {
                coapController.sendCommand("grow_light", "off");
            } catch (ConnectorException | IOException e) {
                throw new RuntimeException(e);
            }
        }
    }
}


package org.unipi.smartgarden.control;

import org.eclipse.californium.elements.exception.ConnectorException;
import org.unipi.smartgarden.coap.COAPNetworkController;
import org.unipi.smartgarden.mqtt.MQTTHandler;
import org.unipi.smartgarden.util.ConsoleUtils;

import java.util.concurrent.ConcurrentHashMap;
import java.io.IOException;
import java.util.Map;

/**
 * ControlLogicThread - Periodically checks sensor values and sends commands
 * to actuators to maintain optimal environmental conditions in the smart garden.
 */
public class ControlLogicThread extends Thread {

    private final MQTTHandler mqttHandler;
    private final COAPNetworkController coapController;
    private final Map<String, String> actuatorAliasMap;
    private final Map<String, Boolean> manualOverride = new ConcurrentHashMap<>();
    private volatile boolean running = true;

    private static final long SLEEP_INTERVAL_MS = 10_000; // 10 seconds

    // Thresholds
    private static final float TEMP_LOWER = 18.0f;
    private static final float TEMP_UPPER = 26.0f;

    private static final float PH_LOWER = 6.0f;
    private static final float PH_UPPER = 7.5f;

    private static final float MOISTURE_LOWER = 35.0f;
    private static final float MOISTURE_UPPER = 70.0f;

    private static final float LIGHT_LOWER = 300.0f;  // lux
    private static final float LIGHT_UPPER = 1000.0f;

    public ControlLogicThread(MQTTHandler mqttHandler, COAPNetworkController coapController, Map<String, String> actuatorAliasMap) {
        this.mqttHandler = mqttHandler;
        this.coapController = coapController;
        this.actuatorAliasMap = actuatorAliasMap;
        this.manualOverride.put("fan", false);
	this.manualOverride.put("heater", false);
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
    
    public void setManualOverride(String actuator, boolean override) {
	 manualOverride.put(actuator, override);
    }

    private void checkTemperature() throws ConnectorException, IOException {
	    Float temperature = mqttHandler.getLatestValue("temperature");
	    if (temperature == null) return;

	    String fanPath = actuatorAliasMap.get("fan");
	    String heaterPath = actuatorAliasMap.get("heater");

	    String fanState = coapController.getActuatorState(fanPath);
	    String heaterState = coapController.getActuatorState(heaterPath);

	    boolean fanOverride = manualOverride.getOrDefault("fan", false);
	    boolean heaterOverride = manualOverride.getOrDefault("heater", false);

	    boolean withinRange = temperature >= TEMP_LOWER && temperature <= TEMP_UPPER;

	    // Auto mode only when not overridden
	    if (!fanOverride && !heaterOverride) {
		if (temperature < TEMP_LOWER) {
		    ConsoleUtils.println("[Control Logic] Temperature too low: " + temperature);
		    mqttHandler.simulateHeater("on");
		    if (!"on".equalsIgnoreCase(heaterState)) coapController.sendCommand(heaterPath, "on");
		    if (!"off".equalsIgnoreCase(fanState)) coapController.sendCommand(fanPath, "off");
		} else if (temperature > TEMP_UPPER) {
		    ConsoleUtils.println("[Control Logic] Temperature too high: " + temperature);
		    mqttHandler.simulateFan("on");
		    if (!"on".equalsIgnoreCase(fanState)) coapController.sendCommand(fanPath, "on");
		    if (!"off".equalsIgnoreCase(heaterState)) coapController.sendCommand(heaterPath, "off");
		} else {
    			ConsoleUtils.println("[Control Logic] Temperature within acceptable range: " + temperature);
    			// do nothing – keep current actuator states
		}
	    }

	    // Reset manual override if back in range
	    if (withinRange) {
		if (fanOverride) {
		    ConsoleUtils.println("[Control Logic] Resetting manual override for fan");
		    manualOverride.put("fan", false);
		}
		if (heaterOverride) {
		    ConsoleUtils.println("[Control Logic] Resetting manual override for heater");
		    manualOverride.put("heater", false);
		}
	    }
    }

    private void checkPH() {
        Float pH = mqttHandler.getLatestValue("pH");
        if (pH == null) return;

        String fertPath = actuatorAliasMap.get("fertilizer");

        try {
            if (pH < PH_LOWER) {
                ConsoleUtils.println("[Control Logic] pH too low: " + pH);
                mqttHandler.simulateFertilizer("sinc");
                coapController.sendCommand(fertPath, "sinc");
            } else if (pH > PH_UPPER) {
                ConsoleUtils.println("[Control Logic] pH too high: " + pH);
                mqttHandler.simulateFertilizer("sdec");
                coapController.sendCommand(fertPath, "sdec");
            } else {
                mqttHandler.simulateFertilizer("off");
                coapController.sendCommand(fertPath, "off");
            }
        } catch (ConnectorException | IOException e) {
            throw new RuntimeException(e);
        }
    }

    private void checkSoilMoisture() {
        Float moisture = mqttHandler.getLatestValue("soilMoisture");
        if (moisture == null) return;

        String irrigationPath = actuatorAliasMap.get("irrigation");

        try {
            String irrigationState = coapController.getActuatorState(irrigationPath);

            if (moisture < MOISTURE_LOWER) {
                ConsoleUtils.println("[Control Logic] Soil moisture too low: " + moisture);
                mqttHandler.simulateIrrigation("on");
                if (!"on".equalsIgnoreCase(irrigationState)) {
                    coapController.sendCommand(irrigationPath, "on");
                }
            } else if (moisture > MOISTURE_UPPER) {
                mqttHandler.simulateIrrigation("off");
                if (!"off".equalsIgnoreCase(irrigationState)) {
                    coapController.sendCommand(irrigationPath, "off");
                }
            }
        } catch (ConnectorException | IOException e) {
            throw new RuntimeException(e);
        }
    }

    private void checkLight() {
        Float light = mqttHandler.getLatestValue("light");
        if (light == null) return;

        String lightPath = actuatorAliasMap.get("grow_light");

        try {
            String lightState = coapController.getActuatorState(lightPath);

            if (light < LIGHT_LOWER) {
                ConsoleUtils.println("[Control Logic] Light too low: " + light);
                mqttHandler.simulateGrowLight("on");
                if (!"on".equalsIgnoreCase(lightState)) {
                    coapController.sendCommand(lightPath, "on");
                }
            } else if (light > LIGHT_UPPER) {
                mqttHandler.simulateGrowLight("off");
                if (!"off".equalsIgnoreCase(lightState)) {
                    coapController.sendCommand(lightPath, "off");
                }
            }
        } catch (ConnectorException | IOException e) {
            throw new RuntimeException(e);
        }
    }
    
    public boolean isFanOverride() {
    	return manualOverride.getOrDefault("fan", false);
    }

    public boolean isHeaterOverride() {
    	return manualOverride.getOrDefault("heater", false);
    }

}


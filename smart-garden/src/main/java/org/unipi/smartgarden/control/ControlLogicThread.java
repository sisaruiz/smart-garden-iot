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
    private final Map<String, Boolean> manualOverride = new ConcurrentHashMap<>();
    private volatile boolean running = true;
    private boolean growLightManualMode = false;
    private boolean growLightOn = false;

    private static final long SLEEP_INTERVAL_MS = 10_000; // 10 seconds

    // CoAP resource paths (bare names)
    private static final String FERTILIZER = "fertilizer";
    private static final String IRRIGATION = "irrigation";
    private static final String GROW_LIGHT = "grow_light";
    private static final String FAN = "fan";
    private static final String HEATER = "heater";

    // Thresholds
    private static final float TEMP_LOWER = 18.0f;
    private static final float TEMP_UPPER = 26.0f;

    private static final float PH_LOWER = 6.0f;
    private static final float PH_UPPER = 7.5f;

    private static final float MOISTURE_LOWER = 35.0f;
    private static final float MOISTURE_UPPER = 70.0f;

    private static final float LIGHT_LOWER = 30.0f;  // lux (arbitrary scale)

    // Constructor WITHOUT alias map
    public ControlLogicThread(MQTTHandler mqttHandler, COAPNetworkController coapController) {
        this.mqttHandler = mqttHandler;
        this.coapController = coapController;
        this.manualOverride.put(FAN, false);
        this.manualOverride.put(HEATER, false);
        this.manualOverride.put(FERTILIZER, false);
        this.manualOverride.put(IRRIGATION, false);
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

	    String fanState = coapController.getActuatorState(FAN);
	    String heaterState = coapController.getActuatorState(HEATER);

	    boolean fanOverride = manualOverride.getOrDefault(FAN, false);
	    boolean heaterOverride = manualOverride.getOrDefault(HEATER, false);

	    boolean withinRange = temperature >= TEMP_LOWER && temperature <= TEMP_UPPER;

	    // --- CASO 1: C'È MANUAL OVERRIDE ---
	    if (fanOverride || heaterOverride) {
		if (temperature < TEMP_LOWER) {
		    ConsoleUtils.println("[Control Logic] (override) Temp too low: " + temperature);
		    if (!"on".equalsIgnoreCase(heaterState)) {
		        mqttHandler.simulateHeater("on");
		        coapController.sendCommand(HEATER, "on");
		    }
		    if (!"off".equalsIgnoreCase(fanState)) {
		        coapController.sendCommand(FAN, "off");
		    }
		    // Reset override heater
		    if (heaterOverride) {
		        ConsoleUtils.println("[Control Logic] Resetting manual override for heater");
		        manualOverride.put(HEATER, false);
		    }
		} else if (temperature > TEMP_UPPER) {
		    ConsoleUtils.println("[Control Logic] (override) Temp too high: " + temperature);
		    if (!"on".equalsIgnoreCase(fanState)) {
		        mqttHandler.simulateFan("on");
		        coapController.sendCommand(FAN, "on");
		    }
		    if (!"off".equalsIgnoreCase(heaterState)) {
		        coapController.sendCommand(HEATER, "off");
		    }
		    // Reset override fan
		    if (fanOverride) {
		        ConsoleUtils.println("[Control Logic] Resetting manual override for fan");
		        manualOverride.put(FAN, false);
		    }
		} else {
		    ConsoleUtils.println("[Control Logic] (override) Temp within range: " + temperature);
		}
		return;
	    }

	    // --- CASO 2: NESSUN OVERRIDE → automatico sempre attivo ---
	    if (temperature < TEMP_LOWER) {
		ConsoleUtils.println("[Control Logic] Temp too low: " + temperature);
		if (!"on".equalsIgnoreCase(heaterState)) {
		    mqttHandler.simulateHeater("on");
		    coapController.sendCommand(HEATER, "on");
		}
		if (!"off".equalsIgnoreCase(fanState)) {
		    coapController.sendCommand(FAN, "off");
		}

	    } else if (temperature > TEMP_UPPER) {
		ConsoleUtils.println("[Control Logic] Temp too high: " + temperature);
		if (!"on".equalsIgnoreCase(fanState)) {
		    mqttHandler.simulateFan("on");
		    coapController.sendCommand(FAN, "on");
		}
		if (!"off".equalsIgnoreCase(heaterState)) {
		    coapController.sendCommand(HEATER, "off");
		}

	    } else {
		ConsoleUtils.println("[Control Logic] Temp within range: " + temperature);
		if (!"off".equalsIgnoreCase(fanState)) {
		    coapController.sendCommand(FAN, "off");
		}
		if (!"off".equalsIgnoreCase(heaterState)) {
		    coapController.sendCommand(HEATER, "off");
		}
	    }
	}

	private void checkPH() {
	    Float pH = mqttHandler.getLatestValue("pH");
	    if (pH == null) return;

	    boolean fertOverride = manualOverride.getOrDefault(FERTILIZER, false);
	    boolean withinRange = pH >= PH_LOWER && pH <= PH_UPPER;

	    try {
		String fertState = coapController.getActuatorState(FERTILIZER);

		// --- CASE 1: MANUAL OVERRIDE ---
		if (fertOverride) {
		    if (pH < PH_LOWER) {
		        ConsoleUtils.println("[Control Logic] (override) pH too low: " + pH);
		        if (!"acidic".equalsIgnoreCase(fertState)) {
		            mqttHandler.simulateFertilizer("sinc");
		            coapController.sendCommand(FERTILIZER, "sinc");
		        }
		        ConsoleUtils.println("[Control Logic] Resetting manual override for fertilizer");
		        manualOverride.put(FERTILIZER, false);

		    } else if (pH > PH_UPPER) {
		        ConsoleUtils.println("[Control Logic] (override) pH too high: " + pH);
		        if (!"alkaline".equalsIgnoreCase(fertState)) {
		            mqttHandler.simulateFertilizer("sdec");
		            coapController.sendCommand(FERTILIZER, "sdec");
		        }
		        ConsoleUtils.println("[Control Logic] Resetting manual override for fertilizer");
		        manualOverride.put(FERTILIZER, false);

		    } else {
		        ConsoleUtils.println("[Control Logic] (override) pH within range: " + pH);
		    }
		    return;
		}

		// --- CASE 2: NO OVERRIDE → automatic always active ---
		if (pH < PH_LOWER) {
		    ConsoleUtils.println("[Control Logic] pH too low: " + pH);
		    if (!"acidic".equalsIgnoreCase(fertState)) {
		        mqttHandler.simulateFertilizer("sinc");
		        coapController.sendCommand(FERTILIZER, "sinc");
		    }

		} else if (pH > PH_UPPER) {
		    ConsoleUtils.println("[Control Logic] pH too high: " + pH);
		    if (!"alkaline".equalsIgnoreCase(fertState)) {
		        mqttHandler.simulateFertilizer("sdec");
		        coapController.sendCommand(FERTILIZER, "sdec");
		    }

		} else {
		    ConsoleUtils.println("[Control Logic] pH within acceptable range: " + pH);
		    if (!"off".equalsIgnoreCase(fertState)) {
		        mqttHandler.simulateFertilizer("off");
		        coapController.sendCommand(FERTILIZER, "off");
		    }
		}

	    } catch (ConnectorException | IOException e) {
		throw new RuntimeException(e);
	    }
	}


    private void checkSoilMoisture() {
        Float moisture = mqttHandler.getLatestValue("soilMoisture");
        if (moisture == null) return;

        boolean irrigationOverride = manualOverride.getOrDefault(IRRIGATION, false);

        try {
            String irrigationState = coapController.getActuatorState(IRRIGATION);

            if (irrigationOverride) {
                if (moisture < MOISTURE_LOWER || moisture > MOISTURE_UPPER) {
                    ConsoleUtils.println("[Control Logic] (override) Soil moisture out of bounds: " + moisture);
                    String desiredState = (moisture < MOISTURE_LOWER) ? "on" : "off";
                    mqttHandler.simulateIrrigation(desiredState);
                    if (!desiredState.equalsIgnoreCase(irrigationState)) {
                        coapController.sendCommand(IRRIGATION, desiredState);
                    }
                    ConsoleUtils.println("[Control Logic] Resetting manual override for irrigation");
                    manualOverride.put(IRRIGATION, false);
                } else {
                    ConsoleUtils.println("[Control Logic] (override) Moisture in acceptable range: " + moisture);
                }
                return;
            }

            // No override → automatic mode always active
            if (moisture < MOISTURE_LOWER) {
                ConsoleUtils.println("[Control Logic] Soil moisture too low: " + moisture);
                mqttHandler.simulateIrrigation("on");
                if (!"on".equalsIgnoreCase(irrigationState)) {
                    coapController.sendCommand(IRRIGATION, "on");
                }
            } else if (moisture > MOISTURE_UPPER) {
                ConsoleUtils.println("[Control Logic] Soil moisture too high: " + moisture);
                mqttHandler.simulateIrrigation("off");
                if (!"off".equalsIgnoreCase(irrigationState)) {
                    coapController.sendCommand(IRRIGATION, "off");
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
		String lightState = coapController.getActuatorState(GROW_LIGHT);

		if (growLightManualMode) {
		    ConsoleUtils.println("[Control Logic] (manual) grow_light is " + (growLightOn ? "ON" : "OFF"));
		    mqttHandler.simulateGrowLight(growLightOn ? "on" : "off");
		    if (growLightOn && !"on".equalsIgnoreCase(lightState)) {
		        coapController.sendCommand(GROW_LIGHT, "on");
		    } else if (!growLightOn && !"off".equalsIgnoreCase(lightState)) {
		        coapController.sendCommand(GROW_LIGHT, "off");
		    }
		    return;
		}

		// --- Automatic mode logic ---
		if (light < LIGHT_LOWER) {
		    ConsoleUtils.println("[Control Logic] Light too low: " + light);
		    mqttHandler.simulateGrowLight("on");
		    if (!"on".equalsIgnoreCase(lightState)) {
		        coapController.sendCommand(GROW_LIGHT, "on");
		    }
		}
		// No OFF command here – high light values are ignored in auto mode
	    } catch (ConnectorException | IOException e) {
		throw new RuntimeException(e);
	    }
	}

	public void enableGrowLightAutoMode() {
	    this.growLightManualMode = false;

	    Float light = mqttHandler.getLatestValue("light");
	    if (light == null) return;

	    try {
		if (light < LIGHT_LOWER) {
		    ConsoleUtils.println("[Control Logic] (auto switch) Light low: " + light + " → ON");
		    mqttHandler.simulateGrowLight("on");
		    coapController.sendCommand(GROW_LIGHT, "on");
		} else if (light > 340.0f) { // "safe high" threshold for OFF at switch
		    ConsoleUtils.println("[Control Logic] (auto switch) Light high: " + light + " → OFF");
		    mqttHandler.simulateGrowLight("off");
		    coapController.sendCommand(GROW_LIGHT, "off");
		} else {
		    ConsoleUtils.println("[Control Logic] (auto switch) Light near threshold: keep state");
		}
	    } catch (ConnectorException | IOException e) {
		ConsoleUtils.printError("[Control Logic] Failed to apply auto-mode adjustment");
		e.printStackTrace();
	    }
	}

    public boolean isFanOverride() {
        return manualOverride.getOrDefault(FAN, false);
    }

    public boolean isHeaterOverride() {
        return manualOverride.getOrDefault(HEATER, false);
    }
    
    public void setGrowLightManualMode(boolean manual) {
        this.growLightManualMode = manual;
    }

    public void setGrowLightState(boolean on) {
        this.growLightOn = on;
    }
    
    public boolean isGrowLightManual() {
        return growLightManualMode;
    }
 }


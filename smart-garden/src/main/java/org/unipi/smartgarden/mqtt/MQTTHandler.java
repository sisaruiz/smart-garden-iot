package org.unipi.smartgarden.mqtt;

import org.eclipse.paho.client.mqttv3.*;
import org.unipi.smartgarden.db.DBDriver;
import org.unipi.smartgarden.util.ConsoleUtils;

import java.util.HashMap;
import java.util.Map;

public class MQTTHandler implements MqttCallback {

    private static final String LOG = "[MQTT Handler]";
    private static final String BROKER_URI = "tcp://localhost:1883";
    private static final String CLIENT_ID = "SmartGardenApp";

    private final DBDriver db;
    private final Map<String, String> sensorTopics; // sensorName -> topic
    private final Map<String, Float> latestValues;  // sensorName -> last value

    private MqttClient client;

    public MQTTHandler(Map<String, String> configuredSensors, DBDriver db) {
        this.db = db;
        this.sensorTopics = configuredSensors;
        this.latestValues = new HashMap<>();

        try {
            client = new MqttClient(BROKER_URI, CLIENT_ID);
            client.setCallback(this);
            client.connect();

            for (Map.Entry<String, String> entry : sensorTopics.entrySet()) {
                client.subscribe(entry.getValue());
                ConsoleUtils.println(LOG + " Subscribed to topic: " + entry.getValue());
                latestValues.put(entry.getKey(), null);
            }

        } catch (MqttException e) {
            ConsoleUtils.printError(LOG + " Failed to connect or subscribe: " + e.getMessage());
            e.printStackTrace();
        }
    }

    public void printSensorStatus() {
        for (String sensor : sensorTopics.keySet()) {
            Float value = latestValues.get(sensor);
            if (value != null) {
                ConsoleUtils.println(sensor + " - Latest value: " + String.format("%.2f", value));
            } else {
                ConsoleUtils.println(sensor + " - No data received yet.");
            }
        }
    }

    public Float getLatestValue(String sensorName) {
        return latestValues.get(sensorName);
    }

    @Override
    public void connectionLost(Throwable cause) {
        ConsoleUtils.printError(LOG + " Connection lost: " + cause.getMessage());
    }

	@Override
	public void messageArrived(String topic, MqttMessage message) {
	    String payload = new String(message.getPayload()).trim();

	    try {
		String sensorName = getSensorNameFromTopic(topic);

		if (sensorName != null) {
		    com.google.gson.Gson gson = new com.google.gson.Gson();
		    Map<?, ?> jsonMap = gson.fromJson(payload, Map.class);

		    if (jsonMap.containsKey(sensorName)) {
		        double doubleVal = (Double) jsonMap.get(sensorName);
		        float value = (float) doubleVal;

		        latestValues.put(sensorName, value);
		        db.insertSample(sensorName, value, null);
		        ConsoleUtils.println(LOG + " Inserted " + value + " for sensor: " + sensorName);
		    } else {
		        ConsoleUtils.printError(LOG + " JSON does not contain expected key: " + sensorName);
		    }
		} else {
		    ConsoleUtils.printError(LOG + " Received message from unknown topic: " + topic);
		}

	    } catch (Exception e) {
		ConsoleUtils.printError(LOG + " Failed to parse JSON payload: " + payload);
		e.printStackTrace();
	    }
	}

    private String getSensorNameFromTopic(String topic) {
        for (Map.Entry<String, String> entry : sensorTopics.entrySet()) {
            if (entry.getValue().equals(topic)) {
                return entry.getKey();
            }
        }
        return null;
    }

    @Override
    public void deliveryComplete(IMqttDeliveryToken token) {
        // Not needed for subscriptions
    }

    public void close() {
        try {
            ConsoleUtils.println(LOG + " Disconnecting...");
            client.disconnect();
            client.close();
        } catch (MqttException e) {
            ConsoleUtils.printError(LOG + " Error during MQTT client shutdown: " + e.getMessage());
        }
    }

    // ---------------------- PUBLISHING METHODS FOR ACTUATOR CONTROL ----------------------

    public void sendCommand(String topic, String command) {
        try {
            client.publish(topic, new MqttMessage(command.getBytes()));
            ConsoleUtils.println(LOG + " Published command to " + topic + ": " + command);
        } catch (MqttException e) {
            ConsoleUtils.printError(LOG + " Failed to publish command to " + topic + ": " + e.getMessage());
        }
    }

    public void simulateFan(String state) {
        sendCommand("fan", state);  // "on" or "off"
    }

    public void simulateHeater(String state) {
        sendCommand("heater", state);  // "on" or "off"
    }

    public void simulateGrowLight(String mode) {
        sendCommand("grow_light", mode);  // ""on" or "off"
    }

    public void simulateIrrigation(String state) {
        sendCommand("irrigation", state);  // "on" or "off"
    }

    public void simulateFertilizer(String mode) {
        sendCommand("fertilizer", mode);  // "off", "sinc", "sdec", "acidic", "alkaline"
    }

    // ---------------------- SENSOR SIMULATION METHOD ----------------------

    public void simulateSensor(String sensorTopic, float value) {
        try {
            String json = "{\"" + sensorTopic + "\":" + value + "}";
            client.publish(sensorTopic, new MqttMessage(json.getBytes()));
            ConsoleUtils.println(LOG + " Simulated sensor value for " + sensorTopic + ": " + json);
        } catch (MqttException e) {
            ConsoleUtils.printError(LOG + " Failed to simulate sensor data for " + sensorTopic + ": " + e.getMessage());
        }
    }
}



package org.unipi.smartgarden.mqtt;

import org.eclipse.paho.client.mqttv3.*;
import org.unipi.smartgarden.db.DBDriver;

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
                System.out.println(LOG + " Subscribed to topic: " + entry.getValue());
                latestValues.put(entry.getKey(), null);
            }

        } catch (MqttException e) {
            System.err.println(LOG + " Failed to connect or subscribe: " + e.getMessage());
            e.printStackTrace();
        }
    }

    public void printSensorStatus() {
        for (String sensor : sensorTopics.keySet()) {
            Float value = latestValues.get(sensor);
            if (value != null) {
                System.out.printf("%s - Latest value: %.2f\n", sensor, value);
            } else {
                System.out.printf("%s - No data received yet.\n", sensor);
            }
        }
    }

    public Float getLatestValue(String sensorName) {
        return latestValues.get(sensorName);
    }

    @Override
    public void connectionLost(Throwable cause) {
        System.err.println(LOG + " Connection lost: " + cause.getMessage());
    }

    @Override
    public void messageArrived(String topic, MqttMessage message) {
        String payload = new String(message.getPayload()).trim();
        try {
            String sensorName = getSensorNameFromTopic(topic);

            if (sensorName != null) {
                // Use Gson to parse the JSON payload like {"temperature": 22.4}
                com.google.gson.Gson gson = new com.google.gson.Gson();
                Map<?, ?> jsonMap = gson.fromJson(payload, Map.class);

                if (jsonMap.containsKey(sensorName)) {
                    double doubleVal = (Double) jsonMap.get(sensorName);  // Gson returns Double by default
                    float value = (float) doubleVal;

                    latestValues.put(sensorName, value);
                    db.insertSample(sensorName, value, null);
                    System.out.println(LOG + " Inserted " + value + " for sensor: " + sensorName);
                } else {
                    System.out.println(LOG + " JSON does not contain expected key: " + sensorName);
                }
            } else {
                System.out.println(LOG + " Received message from unknown topic: " + topic);
            }

        } catch (Exception e) {
            System.err.println(LOG + " Failed to parse JSON payload: " + payload);
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
            System.out.println(LOG + " Disconnecting...");
            client.disconnect();
            client.close();
        } catch (MqttException e) {
            System.err.println(LOG + " Error during MQTT client shutdown: " + e.getMessage());
        }
    }

    // ---------------------- PUBLISHING METHODS FOR ACTUATOR CONTROL ----------------------

    public void sendCommand(String topic, String command) {
        try {
            client.publish(topic, new MqttMessage(command.getBytes()));
            System.out.println(LOG + " Published command to " + topic + ": " + command);
        } catch (MqttException e) {
            System.err.println(LOG + " Failed to publish command to " + topic + ": " + e.getMessage());
        }
    }

    public void simulateFan(String state) {
        sendCommand("fan", state);  // "on" or "off"
    }

    public void simulateHeater(String state) {
        sendCommand("heater", state);  // "on" or "off"
    }

    public void simulateGrowLight(String mode) {
        sendCommand("growLight", mode);  // "ON", "OFF", "DIM"
    }

    public void simulateIrrigation(String state) {
        sendCommand("irrigation", state);  // "ON" or "OFF"
    }

    public void simulateFertilizerDispenser(String command) {
        sendCommand("fertilizerDispenser", command);  // "OFF", "SINC", "SDEC", "INC", "DEC"
    }

    public void simulateFertilizer(String mode) {
        // Used by ControlLogicThread when pH is too low or high.
        // Maps to the "fertilizerDispenser" topic.
        sendCommand("fertilizerDispenser", mode);  // e.g., "acidic", "alkaline"
    }

    // ---------------------- SENSOR SIMULATION METHOD ----------------------

    public void simulateSensor(String sensorTopic, float value) {
        try {
            String json = "{\"" + sensorTopic + "\":" + value + "}";
            client.publish(sensorTopic, new MqttMessage(json.getBytes()));
            System.out.println(LOG + " Simulated sensor value for " + sensorTopic + ": " + json);
        } catch (MqttException e) {
            System.err.println(LOG + " Failed to simulate sensor data for " + sensorTopic + ": " + e.getMessage());
        }
    }
}

package org.unipi.smartgarden.configuration;

public class SensorConfig {

    private String id;
    private String topic;

    public SensorConfig() {
        // Default constructor for Gson
    }

    public String getId() {
        return id;
    }

    public String getTopic() {
        return topic;
    }

    public void setId(String id) {
        this.id = id;
    }

    public void setTopic(String topic) {
        this.topic = topic;
    }

    @Override
    public String toString() {
        return id + " → " + topic;
    }
}

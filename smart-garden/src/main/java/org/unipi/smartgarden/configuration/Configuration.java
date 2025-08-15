package org.unipi.smartgarden.configuration;

import java.util.List;

public class Configuration {

    private List<SensorConfig> sensors;
    private List<String> actuators;

    public Configuration() {
        // required by Gson
    }

    public List<SensorConfig> getSensors() {
        return sensors;
    }

    public void setSensors(List<SensorConfig> sensors) {
        this.sensors = sensors;
    }

    public List<String> getActuators() {
        return actuators;
    }

    public void setActuators(List<String> actuators) {
        this.actuators = actuators;
    }

    @Override
    public String toString() {
        return "Configuration {\n" +
                "  sensors=" + sensors + ",\n" +
                "  actuators=" + actuators + "\n" +
                '}';
    }
}

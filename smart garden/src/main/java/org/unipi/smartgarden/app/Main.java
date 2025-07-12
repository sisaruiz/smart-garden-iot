package org.unipi.smartgarden.app;

import org.eclipse.californium.elements.exception.ConnectorException;
import org.unipi.smartgarden.configuration.Configuration;
import org.unipi.smartgarden.control.ControlLogicThread;
import org.unipi.smartgarden.db.DBDriver;
import org.unipi.smartgarden.mqtt.MQTTHandler;
import org.unipi.smartgarden.coap.COAPNetworkController;

import com.google.gson.Gson;

import java.io.FileReader;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.Scanner;

public class Main {

    private static final String LOG = "[Smart Garden]";

    private static final String[] possibleCommands = {
            ":get status",
            ":trigger irrigation",
            ":trigger grow_light",
            ":trigger fertilizer",
            ":trigger fan",
            ":trigger heater",
            ":get configuration",
            ":help",
            ":quit"
    };

    public static void main(String[] args) throws ConnectorException, IOException {

        System.out.println(LOG + " Welcome to the Smart Garden System!");

        System.out.println(LOG + " Loading configuration...");
        Configuration configuration = null;
        try (FileReader reader = new FileReader(
                Main.class.getClassLoader().getResource("config/devices.json").getFile())) {
            configuration = new Gson().fromJson(reader, Configuration.class);
        } catch (Exception e) {
            System.err.println(LOG + " Failed to load configuration: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }

        System.out.println(configuration);

        // Initialize DB
        DBDriver db = new DBDriver();

        // Initialize MQTT handler
        Map<String, String> sensorTopicMap = new HashMap<>();
        for (var sensor : configuration.getSensors()) {
            sensorTopicMap.put(sensor.getId(), sensor.getTopic());
        }
        MQTTHandler mqttHandler = new MQTTHandler(sensorTopicMap, db);

        // Initialize CoAP actuator controller
        COAPNetworkController coapController = new COAPNetworkController(configuration.getActuators(), db);

        // Start control logic thread
        ControlLogicThread controlLogic = new ControlLogicThread(mqttHandler, coapController);
        controlLogic.start();

        // Start user input loop
        Scanner scanner = new Scanner(System.in);
        printPossibleCommands();

        while (true) {
            System.out.print("> ");
            String userInput = scanner.nextLine().trim().toLowerCase();

            if (isValidCommand(userInput)) {
                System.out.println(LOG + " Executing command: " + userInput);

                switch (userInput) {
                    case ":quit":
                        System.out.println(LOG + " Shutting down...");
                        controlLogic.stopThread();
                        try {
                            controlLogic.join();
                        } catch (InterruptedException e) {
                            System.err.println(LOG + " Error while stopping control logic thread.");
                        }
                        mqttHandler.close();
                        coapController.close();
                        db.close();
                        scanner.close();
                        System.out.println(LOG + " Bye!");
                        return;

                    case ":get status":
                        System.out.println(LOG + " Current sensor readings:");
                        mqttHandler.printSensorStatus();
                        break;

                    case ":trigger irrigation":
                        coapController.toggleActuator("irrigation");
                        break;

                    case ":trigger grow_light":
                        coapController.toggleActuator("grow_light");
                        break;

                    case ":trigger fertilizer":
                        coapController.toggleActuator("fertilizer");
                        break;

                    case ":trigger fan":
                        coapController.toggleActuator("fan");
                        break;

                    case ":trigger heater":
                        coapController.toggleActuator("heater");
                        break;

                    case ":get configuration":
                        System.out.println(configuration);
                        break;

                    case ":help":
                        printPossibleCommands();
                        break;
                }

            } else {
                System.out.println(LOG + " Invalid command. Type ':help' to see the list of available commands.");
            }
        }
    }

    private static void printPossibleCommands() {
        System.out.println(LOG + " Available commands:");
        for (String command : possibleCommands) {
            System.out.println(LOG + " - " + command);
        }
    }

    private static boolean isValidCommand(String userInput) {
        for (String command : possibleCommands) {
            if (command.equals(userInput)) return true;
        }
        return false;
    }
}

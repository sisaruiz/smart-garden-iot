package org.unipi.smartgarden.app;

import org.eclipse.californium.elements.exception.ConnectorException;
import org.unipi.smartgarden.configuration.Configuration;
import org.unipi.smartgarden.control.ControlLogicThread;
import org.unipi.smartgarden.db.DBDriver;
import org.unipi.smartgarden.mqtt.MQTTHandler;
import org.unipi.smartgarden.coap.COAPNetworkController;
import org.unipi.smartgarden.util.ConsoleUtils;

import com.google.gson.Gson;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.Scanner;

public class Main {

    private static final String LOG = "[Smart Garden]";

    private static final String[] possibleCommands = {
            ":get status",
            ":get actuators",
            ":trigger irrigation",
            ":trigger grow_light",
            ":trigger fertilizer",
            ":trigger fan",
            ":trigger heater",
            ":get configuration",
            ":set grow_light auto",
            ":help",
            ":quit"
    };


    public static void main(String[] args) throws ConnectorException, IOException {

        ConsoleUtils.println(LOG + " Welcome to the Smart Garden System!");

        ConsoleUtils.println(LOG + " Loading configuration...");
        Configuration configuration = null;
        try (var reader = new java.io.InputStreamReader(
                Main.class.getClassLoader().getResourceAsStream("config/devices.json"))) {
            configuration = new Gson().fromJson(reader, Configuration.class);
        } catch (Exception e) {
            ConsoleUtils.printError(LOG + " Failed to load configuration: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }

        ConsoleUtils.println(configuration.toString());

        DBDriver db = new DBDriver();

        Map<String, String> sensorTopicMap = new HashMap<>();
        for (var sensor : configuration.getSensors()) {
            sensorTopicMap.put(sensor.getId(), sensor.getTopic());
        }
        MQTTHandler mqttHandler = new MQTTHandler(sensorTopicMap, db);

        COAPNetworkController coapController = new COAPNetworkController(configuration.getActuators(), db, mqttHandler);

        ConsoleUtils.println(LOG + " Waiting 15 seconds for CoAP device registration...");
        try {
            Thread.sleep(15000);
        } catch (InterruptedException e) {
            ConsoleUtils.printError(LOG + " Sleep interrupted.");
        }

        ControlLogicThread controlLogic = new ControlLogicThread(mqttHandler, coapController);
        controlLogic.start();

        Map<String, Boolean> actuatorState = new HashMap<>();

        Scanner scanner = new Scanner(System.in);
        printPossibleCommands();

        while (true) {
            ConsoleUtils.print("> ");
            ConsoleUtils.setTyping(true);
            String userInput = scanner.nextLine().trim().toLowerCase();
            ConsoleUtils.setTyping(false);

            if (isValidCommand(userInput)) {
                ConsoleUtils.println(LOG + " Executing command: " + userInput);
                
                if (userInput.equals(":set grow_light auto")) {
		    controlLogic.enableGrowLightAutoMode();
		    continue;
		}

                if (userInput.startsWith(":trigger ")) {
                    String shortName = userInput.replace(":trigger ", "").trim();

                    if (!configuration.getActuators().contains(shortName)) {
                        ConsoleUtils.printError(LOG + " Unknown actuator: " + shortName);
                        continue;
                    }

                    if (shortName.equals("fertilizer")) {
			    ConsoleUtils.println(LOG + " Select fertilizer mode:");
			    ConsoleUtils.println(LOG + "   1 - sinc (acidic)");
			    ConsoleUtils.println(LOG + "   2 - sdec (alkaline)");
			    ConsoleUtils.println(LOG + "   3 - off");
			    ConsoleUtils.print("> ");
			    ConsoleUtils.setTyping(true);
			    String choice = scanner.nextLine().trim();
			    ConsoleUtils.setTyping(false);

			    String mode = switch (choice) {
				case "1" -> "sinc";
				case "2" -> "sdec";
				case "3" -> "off";
				default -> null;
			    };

			    if (mode == null) {
				ConsoleUtils.printError(LOG + " Invalid fertilizer mode.");
				continue;
			    }

			    try {
				coapController.sendCommand(shortName, mode);
				mqttHandler.simulateFertilizer(mode);
				
				// Manual override logic for fertilizer
				if (!"off".equalsIgnoreCase(mode)) {
				    controlLogic.setManualOverride(shortName, true);
				    ConsoleUtils.println(LOG + " Manual override activated for " + shortName);
				} else {
				    controlLogic.setManualOverride(shortName, false);
				    ConsoleUtils.println(LOG + " Manual override cleared for " + shortName);
			        }  
			    } catch (Exception e) {
				ConsoleUtils.printError(LOG + " Failed to send fertilizer command.");
				e.printStackTrace();
			    }
			} else {
			    boolean currentState;
		    	    try {
				    String state = coapController.getActuatorState(shortName);
				    currentState = "on".equalsIgnoreCase(state);
			    } catch (Exception e) {
				    ConsoleUtils.printError(LOG + " Could not fetch current state for " + shortName + ". Assuming OFF.");
				    currentState = false;
			    }
			    String nextCommand = currentState ? "off" : "on";

			    try {
				coapController.sendCommand(shortName, nextCommand);
				actuatorState.put(shortName, !currentState);
				
				  if ("grow_light".equals(shortName)) {
				      controlLogic.setGrowLightManualMode(true);
				      controlLogic.setGrowLightState(!currentState);
				      ConsoleUtils.println(LOG + " Manual override activated for " + shortName);
				  } else if ("irrigation".equals(shortName)) {
				      if ("on".equalsIgnoreCase(nextCommand)) {
					  controlLogic.setManualOverride(shortName, true);
					  ConsoleUtils.println(LOG + " Manual override activated for " + shortName);
				      } else {
					  controlLogic.setManualOverride(shortName, false);
					  ConsoleUtils.println(LOG + " Manual override cleared for " + shortName);
				      }

				  } else if ("fan".equals(shortName) || "heater".equals(shortName)) {
				      controlLogic.setManualOverride(shortName, true);
				      ConsoleUtils.println(LOG + " Manual override activated for " + shortName);
				  }
			    } catch (Exception e) {
				ConsoleUtils.printError(LOG + " Failed to send toggle command to " + shortName);
				e.printStackTrace();
			    }
			}

                    continue;
                }

                switch (userInput) {
                    case ":quit":
                        ConsoleUtils.println(LOG + " Shutting down...");
                        controlLogic.stopThread();
                        try {
                            controlLogic.join();
                        } catch (InterruptedException e) {
                            ConsoleUtils.printError(LOG + " Error while stopping control logic thread.");
                        }
                        mqttHandler.close();
                        coapController.close();
                        db.close();
                        scanner.close();
                        ConsoleUtils.closeLogger();
                        ConsoleUtils.println(LOG + " Bye!");
                        return;

                    case ":get status":
                        ConsoleUtils.println(LOG + " Current sensor readings:");
                        mqttHandler.printSensorStatus();
                        break;

                    case ":get actuators":
		        ConsoleUtils.println(LOG + " Current actuator states:");
		        for (String shortName : configuration.getActuators()) {
			    try {
			        String state = coapController.getActuatorState(shortName);
			        String line = "  - " + shortName + ": " + state;
  
			        if (shortName.equals("grow_light") && controlLogic.isGrowLightManual()) {
				    line += " [manual mode]";
			        } 

			        ConsoleUtils.println(line);
			    } catch (Exception e) {
			        ConsoleUtils.printError(LOG + " Could not retrieve state for " + shortName);
			    }
		        }
		        break;

                    case ":get configuration":
                        ConsoleUtils.println(configuration.toString());
                        break;

                    case ":help":
                        printPossibleCommands();
                        break;
                }

            } else {
                ConsoleUtils.println(LOG + " Invalid command. Type ':help' to see the list of available commands.");
            }
        }
    }

    private static void printPossibleCommands() {
        ConsoleUtils.println(LOG + " Available commands:");
        for (String command : possibleCommands) {
            ConsoleUtils.println(LOG + " - " + command);
        }
    }

    private static boolean isValidCommand(String userInput) {
        for (String command : possibleCommands) {
            if (command.equals(userInput)) return true;
        }
        return false;
    }
}


package org.unipi.smartgarden.coap;

import org.eclipse.californium.core.*;
import org.eclipse.californium.core.coap.CoAP;
import org.eclipse.californium.core.coap.MediaTypeRegistry;
import org.eclipse.californium.core.server.resources.CoapExchange;
import org.eclipse.californium.core.CoapResource;
import org.eclipse.californium.elements.exception.ConnectorException;
import org.unipi.smartgarden.db.DBDriver;
import org.unipi.smartgarden.util.ConsoleUtils;

import java.nio.charset.StandardCharsets;
import org.json.JSONObject;
import org.json.JSONArray;

import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class COAPNetworkController extends CoapServer {

    private static final String LOG = "[CoAP Controller]";
    private static final int COAP_PORT = 5683;

    private final Map<String, String> actuatorEndpoints = new HashMap<>();
    private final DBDriver db;

    public COAPNetworkController(List<String> actuatorList, DBDriver db) {
        super(COAP_PORT);
        this.db = db;

        add(new RegistrationResource("registration"));

        ConsoleUtils.println(LOG + " CoAP server started on port " + COAP_PORT);
        start();
    }

    public void sendCommand(String actuatorName, String command) throws ConnectorException, IOException {
        String endpoint = actuatorEndpoints.get(actuatorName);
        if (endpoint == null) {
            ConsoleUtils.printError(LOG + " Unknown or unregistered actuator: " + actuatorName);
            return;
        }

        CoapClient client = new CoapClient(endpoint);
        CoapResponse response = client.put(command.toLowerCase(), MediaTypeRegistry.TEXT_PLAIN);

        if (response != null && response.isSuccess()) {
            ConsoleUtils.println(LOG + " Command sent to " + actuatorName + ": " + command);
        } else {
            ConsoleUtils.printError(LOG + " Failed to send command to " + actuatorName);
        }
    }

    public String getActuatorState(String actuatorName) throws ConnectorException, IOException {
        String endpoint = actuatorEndpoints.get(actuatorName);
        if (endpoint == null) {
            ConsoleUtils.printError(LOG + " Unknown or unregistered actuator: " + actuatorName);
            return null;
        }

        CoapClient client = new CoapClient(endpoint);
        CoapResponse response = client.get();

        if (response != null && response.isSuccess()) {
            String payload = response.getResponseText();
            try {
                JSONObject json = new JSONObject(payload);
                if (json.has("mode")) {
                    return json.getString("mode");
                } else if (json.has("state")) {
                    return json.getString("state");
                } else {
                    ConsoleUtils.printError(LOG + " Unexpected JSON format in GET response: " + payload);
                    return null;
                }
            } catch (Exception e) {
                ConsoleUtils.printError(LOG + " Failed to parse actuator GET response: " + payload);
                e.printStackTrace();
                return null;
            }
        } else {
            ConsoleUtils.printError(LOG + " Failed to GET from actuator: " + actuatorName);
            return null;
        }
    }

    public void triggerHeater(boolean on) throws ConnectorException, IOException {
        sendCommand("heater", on ? "on" : "off");
    }

    public void triggerFan(boolean on) throws ConnectorException, IOException {
        sendCommand("fan", on ? "on" : "off");
    }

    public void close() {
        this.stop();
        this.destroy();
        ConsoleUtils.println(LOG + " CoAP server shut down.");
    }

    private class RegistrationResource extends CoapResource {

        public RegistrationResource(String name) {
            super(name);
            getAttributes().setTitle("Actuator Registration");
        }

        @Override
        public void handlePOST(CoapExchange exchange) {
            String sourceIP = exchange.getSourceAddress().getHostAddress();
            String payload = new String(exchange.getRequestPayload(), StandardCharsets.UTF_8);

            ConsoleUtils.println(LOG + " Registration received from " + sourceIP + ": " + payload);

            try {
                JSONObject json = new JSONObject(payload);

                if (!json.has("device") || !json.has("resources")) {
                    exchange.respond(CoAP.ResponseCode.BAD_REQUEST, "Invalid registration payload.");
                    return;
                }

                JSONArray resources = json.getJSONArray("resources");
                for (int i = 0; i < resources.length(); i++) {
                    String path = resources.getString(i);
                    String fullUri = "coap://[" + sourceIP + "]:5683/" + path;
		    actuatorEndpoints.put(path, fullUri);
		    ConsoleUtils.println(LOG + " Registered actuator: " + path + " at " + fullUri);
                }

                exchange.respond(CoAP.ResponseCode.CREATED, "Success");

            } catch (Exception e) {
                ConsoleUtils.printError(LOG + " Error while registering: " + e.getMessage());
                exchange.respond(CoAP.ResponseCode.INTERNAL_SERVER_ERROR, "Registration failed.");
            }
        }
    }
}


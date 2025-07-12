package org.unipi.smartgarden.coap;

import org.eclipse.californium.core.*;
import org.eclipse.californium.core.coap.CoAP;
import org.eclipse.californium.core.coap.MediaTypeRegistry;
import org.eclipse.californium.core.server.resources.CoapExchange;
import org.eclipse.californium.core.CoapResource;
import org.eclipse.californium.elements.exception.ConnectorException;
import org.unipi.smartgarden.db.DBDriver;

import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class COAPNetworkController extends CoapServer {

    private static final String LOG = "[CoAP Controller]";
    private static final int COAP_PORT = 5683;

    // Mapping: actuator name → full CoAP URI (e.g., coap://[fd00::abcd]/actuators/fertilizer)
    private final Map<String, String> actuatorEndpoints = new HashMap<>();
    private final DBDriver db;

    public COAPNetworkController(List<String> actuatorList, DBDriver db) {
        super(COAP_PORT);
        this.db = db;

        // Add registration endpoint for CoAP device
        add(new RegistrationResource("registration"));

        System.out.println(LOG + " CoAP server started on port " + COAP_PORT);
        start();
    }

    /**
     * Called from Main.java to send toggle command to the given actuator
     */
    public void toggleActuator(String actuatorName) throws ConnectorException, IOException {
        String endpoint = actuatorEndpoints.get(actuatorName);
        if (endpoint == null) {
            System.err.println(LOG + " Unknown or unregistered actuator: " + actuatorName);
            return;
        }

        CoapClient client = new CoapClient(endpoint);
        String payload = "toggle";

        CoapResponse response = client.put(payload, MediaTypeRegistry.TEXT_PLAIN);
        if (response != null) {
            System.out.println(LOG + " PUT to " + actuatorName + ": " + response.getCode() +
                    " - " + response.getResponseText());
        } else {
            System.err.println(LOG + " No response from actuator: " + actuatorName);
        }
    }

    /**
     * Shutdown logic (called from Main)
     */
    public void close() {
        this.stop();
        this.destroy();
        System.out.println(LOG + " CoAP server shut down.");
    }

    /**
     * Inner class to handle POST /registration from the CoAP device
     */
    private class RegistrationResource extends CoapResource {

        public RegistrationResource(String name) {
            super(name);
            getAttributes().setTitle("Actuator Registration");
        }

        @Override
        public void handlePOST(CoapExchange exchange) {
            String sourceIP = exchange.getSourceAddress().getHostAddress();
            String payload = exchange.getRequestText();

            System.out.println(LOG + " Registration received from " + sourceIP + ": " + payload);

            try {
                if (payload.contains("\"device\"") && payload.contains("\"resources\"")) {
                    for (String token : payload.split("\"")) {
                        if (token.contains("/") && !token.contains("device")) {
                            String path = token;
                            String actuatorName = extractNameFromPath(path);
                            String fullUri = "coap://[" + sourceIP + "]:5683/" + path;
                            actuatorEndpoints.put(actuatorName, fullUri);
                            System.out.println(LOG + " Registered actuator: " + actuatorName + " at " + fullUri);
                        }
                    }
                    exchange.respond(CoAP.ResponseCode.CREATED, "Success");
                } else {
                    exchange.respond(CoAP.ResponseCode.BAD_REQUEST, "Invalid registration payload.");
                }
            } catch (Exception e) {
                System.err.println(LOG + " Error while registering: " + e.getMessage());
                exchange.respond(CoAP.ResponseCode.INTERNAL_SERVER_ERROR, "Registration failed.");
            }
        }

        private String extractNameFromPath(String path) {
            String[] parts = path.split("/");
            return parts[parts.length - 1];
        }
    }

    /**
     * Sends a generic command to an actuator (used for value-based control like "on"/"off" or modes)
     */
    public void sendCommand(String actuatorName, String command) throws ConnectorException, IOException {
        String endpoint = actuatorEndpoints.get(actuatorName);
        if (endpoint == null) {
            System.err.println(LOG + " Unknown or unregistered actuator: " + actuatorName);
            return;
        }

        CoapClient client = new CoapClient(endpoint);
        CoapResponse response = client.put(command, MediaTypeRegistry.TEXT_PLAIN);

        if (response != null && response.isSuccess()) {
            System.out.println(LOG + " Command sent to " + actuatorName + ": " + command);
        } else {
            System.err.println(LOG + " Failed to send command to " + actuatorName);
        }
    }

    /**
     * Wrapper method to activate/deactivate the heater actuator
     */
    public void triggerHeater(boolean on) throws ConnectorException, IOException {
        sendCommand("heater", on ? "on" : "off");
    }

    /**
     * Wrapper method to activate/deactivate the fan actuator
     */
    public void triggerFan(boolean on) throws ConnectorException, IOException {
        sendCommand("fan", on ? "on" : "off");
    }

}

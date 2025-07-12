package org.unipi.smartgarden.db;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.Map;

public class DBDriver {

    private static final String LOG = "[DBDriver]";

    private static final String DB_URL = "jdbc:mysql://localhost:3306/smart_garden";
    private static final String DB_USER = "root";
    private static final String DB_PASS = "password"; // Replace with your actual password

    private Connection connection;
    private Map<String, PreparedStatement> insertStatements;

    public DBDriver() {
        insertStatements = new HashMap<>();

        try {
            connection = DriverManager.getConnection(DB_URL, DB_USER, DB_PASS);

            // Prepared statements for sensors
            insertStatements.put("light", connection.prepareStatement("INSERT INTO light (value) VALUES (?)"));
            insertStatements.put("soil_moisture", connection.prepareStatement("INSERT INTO soil_moisture (value) VALUES (?)"));
            insertStatements.put("temperature", connection.prepareStatement("INSERT INTO temperature (value) VALUES (?)"));
            insertStatements.put("pH", connection.prepareStatement("INSERT INTO pH (value) VALUES (?)"));

            // Prepared statements for actuators (as boolean active/inactive)
            insertStatements.put("irrigation", connection.prepareStatement("INSERT INTO irrigation (active) VALUES (?)"));
            insertStatements.put("fertilizer", connection.prepareStatement("INSERT INTO fertilizer (active) VALUES (?)"));
            insertStatements.put("grow_light", connection.prepareStatement("INSERT INTO grow_light (active) VALUES (?)"));
            insertStatements.put("fan", connection.prepareStatement("INSERT INTO fan (active) VALUES (?)"));
            insertStatements.put("heater", connection.prepareStatement("INSERT INTO heater (active) VALUES (?)"));

        } catch (SQLException e) {
            System.err.println(LOG + " Error connecting to DB: " + e.getMessage());
            e.printStackTrace();
        }
    }

    public boolean insertSample(String type, float value, Float level) {
        try {
            if (connection == null || connection.isClosed()) {
                System.err.println(LOG + " Cannot insert sample: DB connection is closed.");
                return false;
            }

            PreparedStatement stmt = insertStatements.get(type);
            if (stmt == null) {
                System.err.println(LOG + " Unknown data type: " + type);
                return false;
            }

            if (type.equals("irrigation") || type.equals("fertilizer") || type.equals("grow_light")
                    || type.equals("fan") || type.equals("heater")) {
                stmt.setBoolean(1, value != 0);
            } else {
                stmt.setFloat(1, value);
            }

            stmt.executeUpdate();
            return true;

        } catch (SQLException e) {
            System.err.println(LOG + " Failed to insert sample: " + e.getMessage());
            e.printStackTrace();
            return false;
        }
    }

    public void close() {
        try {
            for (PreparedStatement stmt : insertStatements.values()) {
                if (stmt != null) stmt.close();
            }
            if (connection != null) connection.close();
        } catch (SQLException e) {
            System.err.println(LOG + " Error closing DB connection: " + e.getMessage());
        }
    }
}

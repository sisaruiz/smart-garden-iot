package org.unipi.smartgarden.util;

import java.io.PrintStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;

public class ConsoleUtils {

    private static volatile boolean typing = false;
    private static final Object lock = new Object();

    private static boolean fileLoggingEnabled = false;
    private static PrintStream fileOut;

    static {
        try {
            fileOut = new PrintStream(new FileOutputStream("smartgarden.log", true));
            fileLoggingEnabled = true;
        } catch (FileNotFoundException e) {
            System.err.println("Warning: Unable to enable file logging.");
            fileLoggingEnabled = false;
        }
    }

    public static void setTyping(boolean state) {
        synchronized (lock) {
            typing = state;
        }
    }

    public static void println(String msg) {
        synchronized (lock) {
            if (!typing) {
                System.out.println(msg);
            }
            if (fileLoggingEnabled) {
                logToFile(msg);
            }
        }
    }

    public static void print(String msg) {
        synchronized (lock) {
            if (!typing) {
                System.out.print(msg);
            }
            if (fileLoggingEnabled) {
                logToFile(msg);
            }
        }
    }

    public static void printError(String msg) {
        println("\u001B[31m" + msg + "\u001B[0m");
    }

    private static void logToFile(String msg) {
        String timestamp = LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss"));
        fileOut.println("[" + timestamp + "] " + msg);
    }

    public static void closeLogger() {
        if (fileOut != null) {
            fileOut.close();
        }
    }
}



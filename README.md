# Smart Home Notification System

This project combines an Arduino-based security system with an Android application that communicates over Bluetooth. The Arduino sketch controls various sensors and a buzzer while the Android app provides a simple interface to monitor and control the system.

## Features

### Arduino
- **Sensors**: PIR motion, MQ2 gas detection, SW-420 vibration (earthquake), HX711 load cell for package detection.
- **Access control**: MFRC522 RFID reader and keypad with password authentication.
- **Feedback**: 16x2 LCD display and buzzer for alarms and status messages.
- **Bluetooth**: Uses `Serial1` to exchange status updates and commands with the Android app.
- **Commands**: Supports commands such as `STATUS`, `ARM`, `DISARM`, `LOCK`, `UNLOCK`, `STOP`, `PASSWORD:<code>` and more (see the `processBluetoothCommand` function in `Smart Home Notification System.ino`).

### Android App
- Implemented in Java (Compose templates are present but unused).
- Connects to paired Bluetooth devices and exchanges text commands.
- Displays connection status, alarm state, door state and incoming alerts.
- Buttons for arming/disarming the alarm, locking/unlocking the door, stopping alarms and sending passwords.
- Lists incoming alert messages from the Arduino.

## Repository Structure

```
Smart Home Notification System.ino   # Arduino sketch
app/                                 # Android application
gradle/                               # Gradle wrapper files
build.gradle.kts                     # Root Gradle config
settings.gradle.kts                  # Gradle settings
```

## Building the Android App
1. Install [Android Studio](https://developer.android.com/studio).
2. Open the project and let Gradle download the required dependencies.
3. Connect a device or start an emulator and run the `app` module.

Alternatively, you can use the Gradle wrapper:

```bash
./gradlew assembleDebug
```

## Uploading the Arduino Sketch
Open `Smart Home Notification System.ino` in the Arduino IDE. Ensure the required libraries (`SPI`, `MFRC522`, `LiquidCrystal`, `Keypad`, `HX711`) are installed. Select the correct board and port, then upload the sketch.

## Running Tests
Unit and instrumented test stubs are located under `app/src/test` and `app/src/androidTest`. To run them:

```bash
./gradlew test
```

(Instrumented tests require an Android device or emulator.)

## License
This repository does not include an explicit license. If you plan to use or distribute this code, please add an appropriate `LICENSE` file.


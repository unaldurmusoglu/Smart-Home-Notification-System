#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal.h>
#include <Keypad.h>
#include <HX711.h>

// RFID pins
#define RST_PIN         9          
#define SS_PIN          53         

// LCD pins: rs, en, d4, d5, d6, d7
LiquidCrystal lcd(12, 11, 4, 6, 7, 5);

// Buzzer and sensor pins
const int buzzerPin = 10;    
const int pirPin = 8;        
const int mq2pin = A8;
int gasThreshold = 700;
const int sw420Pin = 13;

// SIMPLIFIED Load Cell - just basic detection
const int LOADCELL_DOUT_PIN = 2;
const int LOADCELL_SCK_PIN = 3;
HX711 scale;
float weightThreshold = 200.0;
bool packageDetected = false;
unsigned long lastPackageCheck = 0;
const unsigned long PACKAGE_CHECK_INTERVAL = 2000; // Check every 2 seconds - SLOWER

// Gas sensor - VERY simple
const int GAS_CHECK_INTERVAL = 2000; // Check every 2 seconds
unsigned long lastGasCheck = 0;
bool gasAlarmActive = false;

// Keypad setup
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

byte rowPins[ROWS] = {A6, A5, A4, A3};
byte colPins[COLS] = {A2, A1, A0};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// MFRC522 instance
MFRC522 rfid(SS_PIN, RST_PIN);  

// Authorized card UID
byte authorizedUID[4] = {0x90, 0x9A, 0xDD, 0xA4};

// System variables
String password = "1234";     
String inputPassword = "";
bool doorLocked = true;
bool alarmActive = false;
bool alarmTriggered = false;
bool waitingForPassword = false;
bool passwordTimedOut = false;
bool exitModeActive = false;
bool systemStoppedByCommand = false;
unsigned long lastBuzzerToggle = 0;
unsigned long exitCountdownStart = 0;
unsigned long passwordEntryStartTime = 0;
unsigned long systemStopTime = 0;

const unsigned long PASSWORD_TIMEOUT = 30000;
const unsigned long EXIT_COUNTDOWN = 30000;

unsigned long earthquakeAlarmStartTime = 0; // Still used for timestamp tracking

// Earthquake detection - LESS SENSITIVE
bool earthquakeDetected = false;
unsigned long lastVibrationTime = 0;
int vibrationCount = 0;
const int vibrationThreshold = 10;          // Increased from 5 to 10 - need more vibrations
const unsigned long vibrationWindow = 5000; // Increased from 3000 to 5000ms - longer window

// Bluetooth command processing
String bluetoothCommand = "";

// Function declarations
void handleBluetoothCommands();
void processBluetoothCommand(String cmd);
void sendBluetoothStatus();
void sendBluetoothNotification(String message);
void updateDisplay();
void displayTimeRemaining();
void displayExitTimeRemaining();
void processRFIDCard();
void processKeypadInput(char key);
void checkPackageSimple(); // SIMPLIFIED
void playKeyTone();
void soundAlarm();
void soundContinuousAlarm();
void playSuccessTone();
void playErrorTone();
void playStartupSound();
void playPackageBeep(); // SIMPLE BEEP
bool compareUID(byte *uid1, byte *uid2);
void checkGasSimple();
void checkEarthquake();

void setup() {
  pinMode(mq2pin, INPUT);
  Serial.begin(9600);
  Serial1.begin(9600);
  
  Serial.println("=== SECURITY SYSTEM STARTING ===");
  Serial1.println("Security System Starting");
  
  SPI.begin();         
  rfid.PCD_Init();     
  
  lcd.begin(16, 2);
  pinMode(buzzerPin, OUTPUT);
  pinMode(pirPin, INPUT);
  pinMode(sw420Pin, INPUT);
  
  // SIMPLE Load Cell Setup - NO COMPLEX INITIALIZATION
  Serial.println("Starting Load Cell...");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  if (scale.is_ready()) {
    scale.set_scale(420.0983);
    scale.tare(); // Simple tare
    Serial.println("Load cell ready");
  } else {
    Serial.println("Load cell not found");
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Security System");
  lcd.setCursor(0, 1);
  lcd.print("Ready!");
  
  playStartupSound();
  delay(1000); // ONLY short delay here
  updateDisplay();
  
  Serial1.println("System Ready");
  sendBluetoothStatus();
}

void loop() {
  // *** RFID GETS ABSOLUTE PRIORITY - PROPER HANDLING ***
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    noTone(buzzerPin); // STOP ALL SOUNDS for RFID processing
    Serial.println("*** RFID CARD DETECTED AND READ - PROCESSING NOW ***");
    processRFIDCard();
  }
  
  // Handle Bluetooth commands
  handleBluetoothCommands();
  
  // Check gas
  checkGasSimple();
  
  // Check exit mode countdown
  if (exitModeActive && (millis() - exitCountdownStart > EXIT_COUNTDOWN)) {
    exitModeActive = false;
    alarmActive = true;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("System ARMED");
    
    playSuccessTone();
    sendBluetoothStatus();
    sendBluetoothNotification("System Armed");
    delay(1000); // Short delay only
    updateDisplay();
  }
  
  // Check password timeout
  if (waitingForPassword && !passwordTimedOut && (millis() - passwordEntryStartTime > PASSWORD_TIMEOUT)) {
    passwordTimedOut = true;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Password Timeout");
    lcd.setCursor(0, 1);
    lcd.print("Enter Password!");
    
    sendBluetoothNotification("PASSWORD TIMEOUT");
  }
  
  // MOTION DETECTION
  if (alarmActive && !exitModeActive && digitalRead(pirPin) == HIGH && !alarmTriggered && !passwordTimedOut && !systemStoppedByCommand) {
    alarmTriggered = true;
    
    Serial.println("MOTION DETECTED");
    Serial.println("*** RFID SHOULD NOW WORK TO UNLOCK DOOR ***");
    
    if (!doorLocked && !waitingForPassword) {
      waitingForPassword = true;
      passwordTimedOut = false;
      inputPassword = "";
      passwordEntryStartTime = millis();
    }
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("!!! INTRUDER !!!");
    lcd.setCursor(0, 1);
    lcd.print("Enter password!");
    
    sendBluetoothNotification("INTRUDER DETECTED!");
  }
  
  // Reset systemStoppedByCommand flag after 10 seconds
  if (systemStoppedByCommand && (millis() - systemStopTime > 10000)) {
    systemStoppedByCommand = false;
  }

  // Process keypad input
  char key = keypad.getKey();
  if (key != NO_KEY) {
    processKeypadInput(key);
  }
  
  // Update displays every second
  static unsigned long lastSecondUpdate = 0;
  if (millis() - lastSecondUpdate > 1000) {
    if (waitingForPassword && !passwordTimedOut) {
      displayTimeRemaining();
    }
    if (exitModeActive) {
      displayExitTimeRemaining();
    }
    lastSecondUpdate = millis();
  }

  // SIMPLE package check - NO DELAYS
  checkPackageSimple();
  
  // Earthquake detection
  checkEarthquake();
  
  // *** ALARM SOUNDS LAST - AFTER ALL OTHER PROCESSING ***
  // Earthquake alarm has HIGHEST priority
  if (earthquakeDetected) {
    // Earthquake alarm sound (alternating high/low tones)
    unsigned long currentMillis = millis();
    if (currentMillis - lastBuzzerToggle > 100) {
      static bool buzzerState = false;
      buzzerState = !buzzerState;
      if (buzzerState) {
        tone(buzzerPin, 1500);
      } else {
        tone(buzzerPin, 800);
      }
      lastBuzzerToggle = currentMillis;
    }
    
    // Debug: Show earthquake alarm is active
    static unsigned long lastEarthquakeDebug = 0;
    if (millis() - lastEarthquakeDebug > 5000) { // Every 5 seconds
      Serial.println("*** EARTHQUAKE ALARM ACTIVE - BEEPING ***");
      lastEarthquakeDebug = millis();
    }
  }
  // Generate continuous alarm if password timed out
  else if (passwordTimedOut) {
    soundContinuousAlarm();
  }
  // Generate alarm sound when triggered (motion OR gas)
  else if (alarmTriggered && !earthquakeDetected) {
    soundAlarm();
    
    // Debug: Show what's causing alarm
    static unsigned long lastAlarmDebug = 0;
    if (millis() - lastAlarmDebug > 5000) { // Every 5 seconds
      if (gasAlarmActive) {
        Serial.println("*** GAS ALARM ACTIVE - BEEPING ***");
      } else {
        Serial.println("*** MOTION ALARM ACTIVE - BEEPING ***");
      }
      lastAlarmDebug = millis();
    }
  }
}

// SIMPLIFIED PACKAGE DETECTION - JUST BEEP WHEN DETECTED
void checkPackageSimple() {
  unsigned long currentTime = millis();
  
  // Check every 2 seconds only
  if (currentTime - lastPackageCheck >= PACKAGE_CHECK_INTERVAL) {
    lastPackageCheck = currentTime;
    
    if (scale.is_ready()) {
      float weight = scale.get_units(1); // Single reading - FAST
      
      // LOAD CELL DEBUG - Simple output to Serial Monitor
      Serial.print("Load Cell: ");
      Serial.print(weight, 1);
      Serial.print("g (Threshold: ");
      Serial.print(weightThreshold, 0);
      Serial.print("g) Status: ");
      
      // Simple detection logic
      if (weight > weightThreshold && !packageDetected) {
        packageDetected = true;
        
        Serial.println("PACKAGE DETECTED!");
        
        // SIMPLE BEEP - NO DELAYS
        playPackageBeep();
        
        // Simple notification
        sendBluetoothNotification("Package detected: " + String(weight, 0) + "g");
        Serial.println("*** Package notification sent ***");
        
      } else if (weight < (weightThreshold - 50) && packageDetected) {
        packageDetected = false;
        Serial.println("PACKAGE REMOVED");
      } else if (packageDetected) {
        Serial.println("PACKAGE PRESENT");
      } else {
        Serial.println("NO PACKAGE");
      }
    } else {
      Serial.println("Load Cell: ERROR - HX711 not ready");
    }
  }
}

// SIMPLE PACKAGE BEEP - NO DELAYS
void playPackageBeep() {
  tone(buzzerPin, 1500, 200); // Single beep, non-blocking
}

void checkGasSimple() {
  unsigned long currentTime = millis();
  
  // Check gas every 2 seconds
  if (currentTime - lastGasCheck >= GAS_CHECK_INTERVAL) {
    lastGasCheck = currentTime;
    
    int gasValue = analogRead(mq2pin);
    
    // Only TRIGGER alarm when gas high - NEVER auto-clear
    if (gasValue > gasThreshold && !gasAlarmActive) {
      gasAlarmActive = true;
      alarmTriggered = true;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("!! GAS LEAK !!");
      lcd.setCursor(0, 1);
      lcd.print("Level: ");
      lcd.print(gasValue);
      
      sendBluetoothNotification("GAS DETECTED! Level: " + String(gasValue));
      Serial.println("Gas alarm triggered!");
      Serial.println("*** GAS ALARM WILL CONTINUE UNTIL MANUALLY STOPPED ***");
    }
    
    // NEVER AUTO-CLEAR GAS ALARM - only manual stop allowed
    // Gas alarm continues until password entered or STOP command sent
  }
}

void checkEarthquake() {
  if (digitalRead(sw420Pin) == HIGH) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastVibrationTime > vibrationWindow) {
      vibrationCount = 0;
    }
    
    vibrationCount++;
    lastVibrationTime = currentTime;
    
    // Debug output to serial monitor
    Serial.print("Vibration detected! Count: ");
    Serial.print(vibrationCount);
    Serial.print("/");
    Serial.print(vibrationThreshold);
    Serial.print(" within ");
    Serial.print(vibrationWindow / 1000);
    Serial.println(" seconds");
    
    if (vibrationCount >= vibrationThreshold && !earthquakeDetected) {
      earthquakeDetected = true;
      earthquakeAlarmStartTime = currentTime;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("!! EARTHQUAKE !!");
      lcd.setCursor(0, 1);
      lcd.print("Take Cover NOW!");
      
      sendBluetoothNotification("EARTHQUAKE DETECTED!");
      Serial.println("*** EARTHQUAKE ALARM TRIGGERED ***");
      Serial.println("*** EARTHQUAKE ALARM WILL CONTINUE UNTIL MANUALLY STOPPED ***");
    }
  }
  
  // REMOVED AUTO-TIMEOUT - Earthquake alarm continues until manually stopped
  // Only stops when password entered or STOP command sent
  // Sound generation moved to main loop for better control
}

// RFID Processing - FIXED FOR ALARM SITUATIONS
void processRFIDCard() {
  Serial.println("RFID CARD DETECTED - Processing...");
  
  if (compareUID(rfid.uid.uidByte, authorizedUID)) {
    Serial.println("AUTHORIZED CARD - Door toggle");
    
    doorLocked = !doorLocked;
    
    if (!doorLocked && alarmActive && !waitingForPassword) {
      waitingForPassword = true;
      passwordTimedOut = false;
      inputPassword = "";
      passwordEntryStartTime = millis();
    }
    
    String notification = doorLocked ? "Door LOCKED" : "Door UNLOCKED";
    sendBluetoothNotification(notification);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(notification);
    
    playSuccessTone();
    delay(500); // Very short delay
    updateDisplay();
    
    Serial.println("RFID processing complete - SUCCESS");
  } else {
    Serial.println("UNAUTHORIZED CARD");
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Invalid Card");
    
    playErrorTone();
    delay(500); // Very short delay
    updateDisplay();
  }
  
  // IMPORTANT: Properly halt and reinitialize RFID
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  
  // Small delay to ensure RFID is ready for next read
  delay(100);
  
  Serial.println("RFID ready for next card");
}

void processKeypadInput(char key) {
  // REMOVED KEYPAD BLOCKING - keypad should ALWAYS work for emergencies
  // Old blocking code was preventing password entry during alarms!
  
  playKeyTone();
  
  Serial.print("Keypad: Key '");
  Serial.print(key);
  Serial.println("' pressed");
  
  if (key == '#') {
    if (inputPassword == password) {
      Serial.println("Keypad: Correct password entered");
      
      if (earthquakeDetected) {
        earthquakeDetected = false;
        noTone(buzzerPin);
        sendBluetoothNotification("Earthquake alarm silenced");
      }
      
      if (waitingForPassword || passwordTimedOut || alarmTriggered) {
        alarmActive = false;
        alarmTriggered = false;
        gasAlarmActive = false;
        waitingForPassword = false;
        passwordTimedOut = false;
        systemStoppedByCommand = false;
        noTone(buzzerPin);
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("System DISARMED");
        
        sendBluetoothNotification("System DISARMED");
        playSuccessTone();
        delay(1000); // Short delay
        Serial.println("*** ALL ALARMS CLEARED BY KEYPAD ***");
      }
      else if (!alarmActive && !exitModeActive && !alarmTriggered) {
        exitModeActive = true;
        exitCountdownStart = millis();
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Exit Mode");
        lcd.setCursor(0, 1);
        lcd.print("30 sec to leave");
        
        sendBluetoothNotification("Exit mode started");
        playSuccessTone();
        delay(1000); // Short delay
      }
      else if (exitModeActive) {
        exitModeActive = false;
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Exit Cancelled");
        
        sendBluetoothNotification("Exit cancelled");
        playSuccessTone();
        delay(1000); // Short delay
      }
      
      inputPassword = "";
      updateDisplay();
    } else {
      Serial.println("Keypad: Wrong password entered");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Wrong Password!");
      
      playErrorTone();
      delay(1000); // Short delay
      
      inputPassword = "";
      updateDisplay();
    }
  } else if (key == '*') {
    inputPassword = "";
    Serial.println("Keypad: Password cleared");
    updateDisplay();
  } else {
    inputPassword += key;
    Serial.print("Keypad: Password now: ");
    for (int i = 0; i < inputPassword.length(); i++) {
      Serial.print("*");
    }
    Serial.println();
    updateDisplay();
  }
}

void updateDisplay() {
  lcd.clear();
  
  if (earthquakeDetected) {
    lcd.setCursor(0, 0);
    lcd.print("!! EARTHQUAKE !!");
    lcd.setCursor(0, 1);
    lcd.print("Take Cover NOW!");
    return;
  }
  
  if (passwordTimedOut) {
    lcd.setCursor(0, 0);
    lcd.print("!TIMEOUT ALARM!");
    lcd.setCursor(0, 1);
    if (inputPassword.length() > 0) {
      lcd.print("Pass: ");
      for (int i = 0; i < inputPassword.length(); i++) {
        lcd.print("*");
      }
    } else {
      lcd.print("Enter password!");
    }
  }
  else if (waitingForPassword) {
    lcd.setCursor(0, 0);
    lcd.print("Password:");
    lcd.setCursor(0, 1);
    for (int i = 0; i < inputPassword.length(); i++) {
      lcd.print("*");
    }
  }
  else if (alarmTriggered && doorLocked && !waitingForPassword) {
    lcd.setCursor(0, 0);
    if (earthquakeDetected) {
      lcd.print("!! EARTHQUAKE !!");
    } else if (gasAlarmActive) {
      lcd.print("!! GAS LEAK !!");
    } else {
      lcd.print("!!! INTRUDER !!!");
    }
    lcd.setCursor(0, 1);
    lcd.print("Use RFID card!");
  }
  else if (alarmTriggered) {
    lcd.setCursor(0, 0);
    if (earthquakeDetected) {
      lcd.print("!! EARTHQUAKE !!");
    } else if (gasAlarmActive) {
      lcd.print("!! GAS LEAK !!");
    } else {
      lcd.print("!!! INTRUDER !!!");
    }
    lcd.setCursor(0, 1);
    if (inputPassword.length() > 0) {
      lcd.print("Pass: ");
      for (int i = 0; i < inputPassword.length(); i++) {
        lcd.print("*");
      }
    } else {
      lcd.print("Enter password!");
    }
  }
  else if (exitModeActive) {
    lcd.setCursor(0, 0);
    lcd.print("Exit Mode:");
  }
  else if (inputPassword.length() > 0) {
    lcd.setCursor(0, 0);
    lcd.print("Enter Password:");
    lcd.setCursor(0, 1);
    for (int i = 0; i < inputPassword.length(); i++) {
      lcd.print("*");
    }
  }
  else {
    lcd.setCursor(0, 0);
    lcd.print("Alarm:");
    lcd.setCursor(7, 0);
    lcd.print(alarmActive ? "ARMED" : "OFF");
    
    lcd.setCursor(0, 1);
    lcd.print("Door:");
    lcd.setCursor(6, 1);
    lcd.print(doorLocked ? "LOCKED" : "UNLOCKED");
    
    // Simple package indicator
    if (packageDetected) {
      lcd.setCursor(14, 1);
      lcd.print("PK");
    }
  }
}

void displayTimeRemaining() {
  if (waitingForPassword && !passwordTimedOut) {
    unsigned long elapsedTime = millis() - passwordEntryStartTime;
    unsigned long remainingTime = (elapsedTime < PASSWORD_TIMEOUT) ? (PASSWORD_TIMEOUT - elapsedTime) / 1000 : 0;
    
    lcd.setCursor(11, 0);
    lcd.print("[");
    if (remainingTime < 10) {
      lcd.print(" ");
    }
    lcd.print(remainingTime);
    lcd.print("]");
  }
}

void displayExitTimeRemaining() {
  if (exitModeActive) {
    unsigned long elapsedTime = millis() - exitCountdownStart;
    unsigned long remainingTime = (elapsedTime < EXIT_COUNTDOWN) ? (EXIT_COUNTDOWN - elapsedTime) / 1000 : 0;
    
    lcd.setCursor(11, 0);
    lcd.print("[");
    if (remainingTime < 10) {
      lcd.print(" ");
    }
    lcd.print(remainingTime);
    lcd.print("]");
    
    if (remainingTime <= 10 && remainingTime > 0) {
      tone(buzzerPin, 2000, 50);
    }
  }
}

void handleBluetoothCommands() {
  while (Serial1.available()) {
    char c = Serial1.read();
    
    if (c == '\n') {
      // Log received command to Serial Monitor
      Serial.print("Bluetooth received: '");
      Serial.print(bluetoothCommand);
      Serial.println("'");
      
      processBluetoothCommand(bluetoothCommand);
      bluetoothCommand = "";
    } else if (c != '\r') { // Ignore carriage return
      bluetoothCommand += c;
    }
  }
}

void processBluetoothCommand(String cmd) {
  cmd.trim();
  
  if (cmd == "STATUS") {
    sendBluetoothStatus();
  }
  else if (cmd == "ARM") {
    if (!alarmActive && !exitModeActive) {
      exitModeActive = true;
      exitCountdownStart = millis();
      Serial1.println("Exit mode started");
      updateDisplay();
    }
  }
  else if (cmd == "DISARM") {
    Serial.println("→ Processing DISARM command");
    Serial.print("  → Current states: alarmActive=");
    Serial.print(alarmActive);
    Serial.print(", alarmTriggered=");
    Serial.print(alarmTriggered);
    Serial.print(", earthquakeDetected=");
    Serial.print(earthquakeDetected);
    Serial.print(", gasAlarmActive=");
    Serial.println(gasAlarmActive);
    
    // FIXED: Include earthquakeDetected in condition
    if (alarmActive || alarmTriggered || passwordTimedOut || waitingForPassword || earthquakeDetected || gasAlarmActive) {
      Serial.println("  → Condition met - disarming all alarms");
      
      alarmActive = false;
      alarmTriggered = false;
      gasAlarmActive = false;
      waitingForPassword = false;
      passwordTimedOut = false;
      earthquakeDetected = false;  // This should stop earthquake alarm
      noTone(buzzerPin);           // Stop all sounds
      
      Serial.println("  → All flags cleared, noTone() called");
      
      sendBluetoothNotification("System DISARMED");
      updateDisplay();
      Serial.println("  → All alarms disarmed via Bluetooth");
    } else {
      Serial1.println("No active alarms to disarm");
      Serial.println("  → No active alarms found - condition not met");
    }
  }
  else if (cmd.startsWith("PASSWORD:")) {
    String enteredPassword = cmd.substring(9);
    if (enteredPassword == password) {
      alarmActive = false;
      alarmTriggered = false;
      gasAlarmActive = false;
      waitingForPassword = false;
      passwordTimedOut = false;
      earthquakeDetected = false;
      noTone(buzzerPin);
      Serial1.println("Password accepted");
      updateDisplay();
    } else {
      Serial1.println("Wrong Password!");
    }
  }
  else if (cmd == "UNLOCK") {
    doorLocked = false;
    Serial1.println("Door UNLOCKED");
    updateDisplay();
  }
  else if (cmd == "LOCK") {
    doorLocked = true;
    Serial1.println("Door LOCKED");
    updateDisplay();
  }
  else if (cmd == "STOP") {
    Serial.println("→ Processing STOP command");
    if (alarmTriggered || passwordTimedOut || earthquakeDetected || gasAlarmActive) {
      Serial.print("  → Stopping alarms: ");
      if (earthquakeDetected) Serial.print("EARTHQUAKE ");
      if (gasAlarmActive) Serial.print("GAS ");
      if (alarmTriggered) Serial.print("MOTION ");
      if (passwordTimedOut) Serial.print("TIMEOUT ");
      Serial.println();
      
      alarmActive = false;
      alarmTriggered = false;
      gasAlarmActive = false;
      passwordTimedOut = false;
      waitingForPassword = false;
      earthquakeDetected = false;
      systemStoppedByCommand = true;
      systemStopTime = millis();
      noTone(buzzerPin);
      
      Serial1.println("All alarms STOPPED");
      updateDisplay();
      Serial.println("  → All alarms successfully stopped");
    } else {
      Serial1.println("No active alarms to stop");
      Serial.println("  → No active alarms found");
    }
  }
  else if (cmd == "CHECK_PACKAGE") {
    if (scale.is_ready()) {
      float weight = scale.get_units(1);
      String status = packageDetected ? "Package detected" : "No package";
      sendBluetoothNotification(status + ": " + String(weight, 0) + "g");
    } else {
      sendBluetoothNotification("Load cell not ready");
    }
  }
  else if (cmd == "GAS_STATUS") {
    int gasValue = analogRead(mq2pin);
    Serial1.print("Gas reading: ");
    Serial1.print(gasValue);
    Serial1.print(", Threshold: ");
    Serial1.print(gasThreshold);
    Serial1.print(", Alarm: ");
    Serial1.println(gasAlarmActive ? "ACTIVE" : "OFF");
  }
  else if (cmd == "EARTHQUAKE_STATUS") {
    Serial1.print("Earthquake sensitivity: ");
    Serial1.print(vibrationThreshold);
    Serial1.print(" vibrations in ");
    Serial1.print(vibrationWindow / 1000);
    Serial1.print(" seconds. Current count: ");
    Serial1.println(vibrationCount);
  }
  else if (cmd == "SILENCE_EARTHQUAKE") {
    Serial.println("→ Processing SILENCE_EARTHQUAKE command");
    if (earthquakeDetected) {
      earthquakeDetected = false;
      noTone(buzzerPin);
      sendBluetoothNotification("Earthquake alarm silenced");
      updateDisplay();
      Serial.println("  → Earthquake alarm silenced");
    } else {
      Serial1.println("No earthquake alarm active");
      Serial.println("  → No earthquake alarm to silence");
    }
  }
  else if (cmd == "RFID_STATUS") {
    Serial.println("→ Processing RFID_STATUS command");
    Serial1.println("RFID Module Status:");
    Serial1.print("- Door locked: ");
    Serial1.println(doorLocked ? "YES" : "NO");
    Serial1.println("- RFID should be scanning for cards");
    Serial1.println("- Check Serial Monitor for RFID debug messages");
    Serial1.println("- Try RFID_RESET if not working");
  }
  else if (cmd == "RFID_RESET") {
    Serial.println("→ Processing RFID_RESET command");
    Serial1.println("Resetting RFID module...");
    
    // Reinitialize RFID
    rfid.PCD_Reset();
    delay(100);
    rfid.PCD_Init();
    delay(100);
    
    Serial1.println("RFID module reset complete");
    Serial.println("  → RFID module reinitialized");
  }
  else if (cmd == "HELP") {
    Serial.println("→ Processing HELP command");
    Serial1.println("Available commands:");
    Serial1.println("STATUS - Get system status");
    Serial1.println("ARM - Arm the alarm");
    Serial1.println("DISARM - Disarm all alarms");
    Serial1.println("PASSWORD:xxxx - Enter password");
    Serial1.println("UNLOCK - Unlock door");
    Serial1.println("LOCK - Lock door");
    Serial1.println("STOP - Stop all alarms (GAS/MOTION/etc)");
    Serial1.println("CHECK_PACKAGE - Check package status");
    Serial1.println("GAS_STATUS - Check gas sensor reading");
    Serial1.println("EARTHQUAKE_STATUS - Check earthquake sensitivity");
    Serial1.println("SILENCE_EARTHQUAKE - Silence earthquake alarm only");
    Serial1.println("RFID_STATUS - Check RFID module status");
    Serial1.println("RFID_RESET - Reset RFID module");
  }
  else {
    Serial.println("→ UNKNOWN command: " + cmd);
    Serial1.println("Unknown command. Type HELP for available commands.");
  }
}

void sendBluetoothStatus() { // status istendiğinde 
  Serial1.println("=== STATUS ===");
  Serial1.print("Alarm: ");
  if (exitModeActive) {
    Serial1.println("EXIT MODE");
  } else {
    Serial1.println(alarmActive ? "ARMED" : "DISARMED");
  }
  Serial1.print("Door: ");
  Serial1.println(doorLocked ? "LOCKED" : "UNLOCKED");
  Serial1.print("Gas: ");
  Serial1.println(gasAlarmActive ? "ALARM" : "NORMAL");
  Serial1.print("Earthquake: ");
  Serial1.println(earthquakeDetected ? "ALARM" : "NORMAL");
  Serial1.print("Package: ");
  Serial1.println(packageDetected ? "DETECTED" : "NONE");
  Serial1.println("=============");
}

void sendBluetoothNotification(String message) {
  Serial1.print("[ALERT] ");
  Serial1.println(message);
}

void playKeyTone() {
  tone(buzzerPin, 1500, 50);
}

void soundAlarm() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastBuzzerToggle > 250) {
    static bool buzzerState = false;
    buzzerState = !buzzerState;
    if (buzzerState) {
      tone(buzzerPin, 900);
    } else {
      tone(buzzerPin, 600);
    }
    lastBuzzerToggle = currentMillis;
  }
}

void soundContinuousAlarm() {
  tone(buzzerPin, 1000);
}

void playSuccessTone() {
  tone(buzzerPin, 1000, 150);
  delay(150);
  tone(buzzerPin, 1500, 150);
  delay(150);
  noTone(buzzerPin);
}

void playErrorTone() {
  tone(buzzerPin, 200, 500);
  delay(600);
  noTone(buzzerPin);
}

void playStartupSound() {
  tone(buzzerPin, 440, 100);
  delay(120);
  tone(buzzerPin, 554, 100);
  delay(120);
  tone(buzzerPin, 659, 100);
  delay(120);
  tone(buzzerPin, 880, 150);
  delay(200);
  noTone(buzzerPin);
}

bool compareUID(byte *uid1, byte *uid2) {
  for (int i = 0; i < 4; i++) {
    if (uid1[i] != uid2[i]) {
      return false;
    }
  }
  return true;
}
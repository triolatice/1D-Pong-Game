/*******************************************************************************************************
  1D_PONG_GAME_v2.2
  Copyright (C) 2026  Srecko Nagy

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://gnu.org>.
*******************************************************************************************************/

/*******************************************************************************************************

Written by Srecko Nagy , 10/2026. 

- Short press (< 1.5s): Singleplayer mode (vs AI)
- Long hold (> 2.0s): Multiplayer mode (2P)
- Added full 2-Player (Multiplayer) game mode logic with symmetrical button handling.
- Added dynamic speed acceleration with minimum limits and Perfect Timing Bonus (last 3 LEDs).
- Fixed mode selection logic (Left button short press for 1P, long hold >2s for 2P).
- Added dynamic speed scaling based on LED_COUNT (adapts speeds and steps for shorter/longer strips).
- Added Smash
- Tracks successful hits in the current point
- Tracks the highest hits in the current match
- Permanent high score saved in ESP32 memory

*******************************************************************************************************/

// Internal LEDs on the ESP32S3 module
#define LED_PLAVA 45
#define LED_CRVENA 48
#define BUZZER_PIN 47     // Buzzer pin

// RGB LED Strip definitions (150 LEDs)
#include "Freenove_WS2812_Lib_for_ESP32.h"
#define LED_PIN       38
#define LED_COUNT     150 // Total LEDs on track
//#define LED_COUNT     120 // Total LEDs on track
//#define LED_COUNT     60 // Total LEDs on track
#define LED_CHANNEL   0   // RMT channel (0-3 on S3)
Freenove_ESP32_WS2812 strip(LED_COUNT, LED_PIN, LED_CHANNEL, TYPE_GRB);


#include <Adafruit_NeoPixel.h>

#define BUTTONS_LED_PIN   14  // Pin where your chained button LEDs are connected
#define BUTTONS_LED_COUNT 16  // 8 LEDs for P1 + 8 LEDs for P2

// Initialize the buttons strip (no channels needed here!)
Adafruit_NeoPixel buttonsStrip(BUTTONS_LED_COUNT, BUTTONS_LED_PIN, NEO_GRB + NEO_KHZ800);


// I2C Display (SH1106 OLED 128x64)
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#define SDA_PIN 11
#define SCL_PIN 12 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Data storage memory
#include <Preferences.h> 
Preferences preferences; // Object for permanent flash memory storage
// ---- RALLY / HIT COUNTER ----
int currentRallyCount = 0; // Tracks successful hits in the current point
int maxRallyRecord = 0;    // Tracks the highest hits in the current match
int highRallyAllTime = 0;  // Permanent high score saved in ESP32 memory




// ---- ALL 4 PONG BUTTONS ----          
#define PIN_BUT_RS    5   // Right primary start/hit button
#define PIN_BUT_RP    6   // Right power-up button (P2 Smash)
#define PIN_BUT_LS    7   // Left primary start/hit button (Primary control & mode selection)
#define PIN_BUT_LP    8   // Left power-up button (P1 Smash)

// ---- GAME CONFIGURATION & ZONES ----
const int MIN_ZONE = 5;
const int MAX_ZONE = 25;
int zoneSize = constrain((LED_COUNT * 15) / 100, MIN_ZONE, MAX_ZONE); // ~15% of track length
const int PERFECT_ZONE_LEDS = 5; // Last 3 LEDs for perfect timing bonus


// Game States
enum GameState {
    STATE_IDLE,
    STATE_SINGLEPLAYER,
    STATE_MULTIPLAYER,
    STATE_GAME_OVER
};
GameState currentGameState = STATE_IDLE;

// Hold timer variables for Left button selection
unsigned long lsPressStartTime = 0;
bool lsIsBeingHeld = false;
bool hold2PTriggered = false;
const unsigned long HOLD_TIME_2P = 2000; // 2 seconds hold for 2P mode

// ---- DEBOUNCE & NOISE PROTECTION ----
unsigned long lastDebounceTimeLS = 0;
unsigned long lastDebounceTimeRS = 0;
unsigned long lastDebounceTimeLP = 0;
unsigned long lastDebounceTimeRP = 0;
const unsigned long DEBOUNCE_DELAY = 30; // 30ms debounce filter

bool p1SmashArmed = false; // Did P1 call "Smash" while the ball was traveling toward him?
bool p2SmashArmed = false; // Did P2 call "Smash" while the ball was traveling toward him?


// ---- DYNAMIC SPEED & ACCELERATION CONFIGURATION ----

int SP_START_SPEED = 100;  
int MP_START_SPEED = 100; 

int SP_MIN_SPEED = 8;    
int MP_MIN_SPEED = 25;    

int SPEED_STEP = 10;               
const int PERFECT_BONUS_STEPS = 2; 
const int SMASH_SPEED = 0.3;         

int currentSpeed = SP_START_SPEED;
unsigned long lastBallMoveTime = 0;

// ---- POWER-UP (SMASH) TRACKING ----
bool p1SmashUsed = false; 
bool p2SmashUsed = false; 
bool isSmashActive = false; 

// ---- PONG GAME VARIABLES ----
int ballPosition = LED_COUNT / 2;
//int ballDirection = 1;     // 1 = moving right, -1 = moving left
int ballDirection = (random(0, 2) == 0) ? -1 : 1;

int scoreLeft = 0;
int scoreRight = 0;
const int WINNING_SCORE = 5;

// ---- DEBUG CONFIGURATION ----
const bool DEBUG = true;  
int colorStep = 0;
int colorStep1 = 0;
int colorStep2 = 0;

// ---- GAME CONFIGURATION OPTIONS ----
const bool PENALIZE_MISSED_SMASH = true; // true = loses Smash on miss, false = keeps Smash if ball passes by

// ---- AUDIO TONES CONFIGURATION ----
#define TONE_HIT    1000  // 1000 Hz tone for regular hits
#define TONE_SMASH  1800  // 1800 Hz high pitch tone for Smash hits
#define TONE_SCORE   400  // 400 Hz lower tone for scoring events

void setup() {
    if (DEBUG) {
        Serial.begin(115200);
        Serial.println("1D PONG v2.2 Initializing...");
    }
    randomSeed(analogRead(34)); 
    pinMode(LED_PLAVA, OUTPUT);
    pinMode(LED_CRVENA, OUTPUT);     
    digitalWrite(LED_PLAVA, LOW);
    digitalWrite(LED_CRVENA, LOW);
    
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(PIN_BUT_RS, INPUT_PULLUP);
    pinMode(PIN_BUT_RP, INPUT_PULLUP);
    pinMode(PIN_BUT_LS, INPUT_PULLUP);
    pinMode(PIN_BUT_LP, INPUT_PULLUP);

    Wire.begin(SDA_PIN, SCL_PIN);
    if(!display.begin(0x3C, true)) { 
        if(DEBUG) Serial.println("SH1106 allocation failed!");
        while(1);
    }

    strip.begin();
    /*
    150 LED ->> 100% Brightness >> max 300 mA
    150 LED ->> 20% Brightness >> max 140 mA

    */
    strip.setBrightness(20); 

  buttonsStrip.begin();
  buttonsStrip.setBrightness(30);
  buttonsStrip.show(); // Initialize all pixels to 'off'

    if (LED_COUNT != 150) {
        float scaleFactor = 150.0 / (float)LED_COUNT;
        SP_START_SPEED = (int)(SP_START_SPEED * scaleFactor);
        MP_START_SPEED = (int)(MP_START_SPEED * scaleFactor);
        SP_MIN_SPEED = (int)(SP_MIN_SPEED * scaleFactor);
        MP_MIN_SPEED = (int)(MP_MIN_SPEED * scaleFactor);
        SPEED_STEP = (int)(SPEED_STEP * scaleFactor);
    }

// Open Preferences with namespace "pong" in read-only mode (false means read/write)
  preferences.begin("pong", false);
  highRallyAllTime = preferences.getInt("highRally", 0); // Default to 0 if not found
  preferences.end();
  
  if (DEBUG) {
    Serial.print("Loaded All-Time High Rally Record: ");
    Serial.println(highRallyAllTime);
  }

    kratkiBip();
    showIdleScreen();

}

void loop() {
    switch (currentGameState) {
        case STATE_IDLE:
            runColorChaser();
            checkModeSelection();
            break;

        case STATE_SINGLEPLAYER:
            updateSingleplayerGame();
            break;

        case STATE_MULTIPLAYER:
            updateMultiplayerGame();
            break;

        default:
            runColorChaser();
            break;
    }
}

// Check ONLY Left button (Pin 7) for mode selection
void checkModeSelection() {
    bool lsPressed = (digitalRead(PIN_BUT_LS) == LOW);

    if (lsPressed && !lsIsBeingHeld) {
        lsIsBeingHeld = true;
        hold2PTriggered = false;
        lsPressStartTime = millis();
        prikaziNaEkranu("1D PONG - MODE SELECT", "Left button pressed...", "Rel. for 1P mode", "Hold 2s for 2P mode");
    } 
    else if (lsPressed && lsIsBeingHeld) {
        unsigned long duration = millis() - lsPressStartTime;
        
        if (duration >= HOLD_TIME_2P && !hold2PTriggered) {
            hold2PTriggered = true; 
            currentGameState = STATE_MULTIPLAYER;
            scoreLeft = 0; scoreRight = 0;
            p1SmashUsed = false; p2SmashUsed = false;
            currentSpeed = MP_START_SPEED; 
            randomSeed(millis()); 
            dugiBip();
            if(DEBUG) Serial.println("-> Activated: MULTIPLAYER (2P) MODE!");
            prikaziNaEkranu("1D PONG", "Mode Selected:", "MULTIPLAYER (2P)", "Get Ready!");
            delay(2500);
            runCountdownAnimation(); 
            ballDirection = (random(0, 2) == 0) ? -1 : 1;
            resetBall();
        }
    } 
    else if (!lsPressed && lsIsBeingHeld) {
        lsIsBeingHeld = false;
        unsigned long duration = millis() - lsPressStartTime;

        if (!hold2PTriggered && duration < HOLD_TIME_2P) {
            currentGameState = STATE_SINGLEPLAYER;
            scoreLeft = 0; scoreRight = 0;
            p1SmashUsed = false; p2SmashUsed = false;
            currentSpeed = SP_START_SPEED; 
            kratkiBip();
            if(DEBUG) Serial.println("-> Activated: SINGLEPLAYER (vs AI) MODE!");
            prikaziNaEkranu("1D PONG", "Mode Selected:", "SINGLEPLAYER (vs AI)", "Get Ready!");
            delay(1500);
            resetBall();
            clearButtonLEDs();
        }
    }
}

void runCountdownAnimation() {
  if (DEBUG) Serial.println("Starting countdown animation...");

  // 3-step countdown (Red -> Yellow -> Green) with synchronized buzzer clicks
  for (int step = 3; step >= 1; step--) {
    
    // ---- STEP 1: Clear everything first ----
    // Freenove main track clear loop
    for (int i = 0; i < LED_COUNT; i++) {
      strip.setLedColorData(i, 0, 0, 0);
    }
    buttonsStrip.clear();   // Adafruit buttons (this works fine)
    
    // ---- STEP 2: Set colors based on the countdown step ----
    int r = 0, g = 0, b = 0;
    uint32_t buttonColor = 0;
    
    if (step == 3) {
      r = 40;  // Dim Red for the main track middle
      buttonColor = buttonsStrip.Color(80, 0, 0); // Red for buttons
      prikaziNaEkranu("    1D PONG", "    GET READY", "      ", "");
    } 
    else if (step == 2) {
      r = 30; g = 30; // Orange/Yellow for the main track middle
      buttonColor = buttonsStrip.Color(60, 60, 0); // Yellow for buttons
      prikaziNaEkranu("    1D PONG", "    GET READY", "      ", "");
    } 
    else if (step == 1) {
      g = 40;  // Dim Green for the main track middle
      buttonColor = buttonsStrip.Color(0, 80, 0); // Green for buttons
      prikaziNaEkranu("    1D PONG", "    GET READY", "      ", "");
    }

    // ---- STEP 3: Apply colors to the hardware ----
    // Fill the middle part of the main track (between the player zones)
    for (int i = zoneSize; i < LED_COUNT - zoneSize; i++) {
      strip.setLedColorData(i, r, g, b);
    }
    
    // Fill ALL 16 LEDs on the button strip
    for (int i = 0; i < 16; i++) {
      buttonsStrip.setPixelColor(i, buttonColor);
    }
    
    // Show on hardware
    strip.show();
    buttonsStrip.show();
    
    // Short sharp beep for each second
    tone(BUZZER_PIN, 800, 100); 
    
    delay(500); // Hold the visual for half a second
  }

  // ---- STEP 4: THE "GO!" MOMENT ----
  // Clear main track before GO! flash
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setLedColorData(i, 0, 0, 0);
  }
  buttonsStrip.clear();
  
  // Flash the entire main track and buttons brightly in Cyan for a brief moment
  for (int i = 0; i < LED_COUNT; i++) strip.setLedColorData(i, 0, 150, 150);
  for (int i = 0; i < 16; i++) buttonsStrip.setPixelColor(i, buttonsStrip.Color(0, 150, 150));
  
  strip.show();
  buttonsStrip.show();
  prikaziNaEkranu("    1D PONG", "    GET READY", "      -> GO! <-", "");
  
  // Higher pitch explosive tone for GO!
  tone(BUZZER_PIN, 1500, 200); 
  delay(300);
  
  // Clean up and instantly paint the correct gaming states for button lights
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setLedColorData(i, 0, 0, 0);
  }
  strip.show();
  updateGameButtonLEDs(); // Puts Green/Red steady states back if Smash is armed
}


// Singleplayer game loop with button debounce protection
void updateSingleplayerGame() {
    bool currentLsState = (digitalRead(PIN_BUT_LS) == LOW);
    bool currentLpState = (digitalRead(PIN_BUT_LP) == LOW);
    
    static bool lastLsPinState = false;
    static bool lastLpPinState = false;

    unsigned long currentTime = millis();

    // 1. Check Player 1 SMASH Hit (Pin 8) with Debounce
    if (currentLpState && !lastLpPinState && (currentTime - lastDebounceTimeLP > DEBOUNCE_DELAY)) {
        lastDebounceTimeLP = currentTime;
        if (!p1SmashUsed && ballDirection == -1 && ballPosition <= zoneSize) {
            p1SmashUsed = true;
            isSmashActive = true;
            ballDirection = 1;
            currentSpeed = SMASH_SPEED;
            smashBip();
            if(DEBUG) Serial.println("P1 ULTIMATE SMASH ACTIVATED!");
        }
    }
    lastLpPinState = currentLpState;

    // 2. Check Player 1 Normal Hit (Pin 7) with Debounce
    if (currentLsState && !lastLsPinState && (currentTime - lastDebounceTimeLS > DEBOUNCE_DELAY)) {
        lastDebounceTimeLS = currentTime;
        if (ballDirection == -1 && ballPosition <= zoneSize) {
            ballDirection = 1;
            isSmashActive = false;
            
            if (ballPosition < PERFECT_ZONE_LEDS) {
                int totalReduction = SPEED_STEP + (SPEED_STEP * PERFECT_BONUS_STEPS);
                currentSpeed = max(SP_MIN_SPEED, currentSpeed - totalReduction);
                dvostrukiBip();
                if(DEBUG) Serial.println("PERFECT HIT (Left)! Bonus speed applied.");
            } else {
                currentSpeed = max(SP_MIN_SPEED, currentSpeed - SPEED_STEP);
                kratkiBip();
                if(DEBUG) Serial.println("Normal Hit (Left).");
            }
        } else if (ballDirection == -1) {
            scoreRight++;
            dugiBip();
            handleScoreEvent(2, "Early Hit! AI Scores!");
            return;
        }
    }
    lastLsPinState = currentLsState;

    // 3. AI Hit (Right side)
    if (ballDirection == 1 && ballPosition >= (LED_COUNT - 1 - zoneSize)) {
        ballDirection = -1;
        isSmashActive = false;
        currentSpeed = max(SP_MIN_SPEED, currentSpeed - SPEED_STEP);
        if(DEBUG) Serial.println("AI Hit! Returned to Left.");
    }

    // 4. Move Ball
    if (millis() - lastBallMoveTime >= currentSpeed) {
        lastBallMoveTime = millis();
        ballPosition += ballDirection;

        if (ballPosition < 0) {
            scoreRight++;
            dugiBip();
            handleScoreEvent(2, "You Missed! AI Scores!");
            return;
        } 
        else if (ballPosition >= LED_COUNT) {
            scoreLeft++;
            dugiBip();
            handleScoreEvent(1, "You Score!");
            return;
        }
    }

    renderGameTrack();
}
void updateMultiplayerGame() {
  unsigned long currentMillis = millis();

  // ---- 1. BALL MOVEMENT LOGIC ----
  if (currentMillis - lastBallMoveTime >= currentSpeed) {
    lastBallMoveTime = currentMillis;

    if (ballDirection == 1) {
      ballPosition++;
      if (ballPosition >= LED_COUNT) {
        // Ball passed Right Player (P2) - Left Player (P1) scores
        scoreLeft++;
        
        // Apply penalty for missed smash if configured
        if (PENALIZE_MISSED_SMASH && p2SmashArmed) {
          p2SmashUsed = true;
        }
        
        // Reset armed and smash states for the next round
        p1SmashArmed = false;
        p2SmashArmed = false;
        isSmashActive = false;
        
        handleScoreEvent(1, "Ply. 1 scored a point!");
        return;
      }
    } else {
      ballPosition--;
      if (ballPosition < 0) {
        // Ball passed Left Player (P1) - Right Player (P2) scores
        scoreRight++;
        
        // Apply penalty for missed smash if configured
        if (PENALIZE_MISSED_SMASH && p1SmashArmed) {
          p1SmashUsed = true;
        }
        
        // Reset armed and smash states for the next round
        p1SmashArmed = false;
        p2SmashArmed = false;
        isSmashActive = false;
        
        handleScoreEvent(2, "Ply. 2 scored a point!");
        return;
      }
    }
    renderGameTrack();
  }

  // ---- 2. READ BUTTON INPUTS (Active LOW) ----
  bool leftHitPressed = (digitalRead(PIN_BUT_LS) == LOW);// Left primary start/hit button (Primary control & mode selection) PIN 7 Plava
  bool rightHitPressed = (digitalRead(PIN_BUT_RS) == LOW); // Right primary start/hit button    PIN 5
  bool leftPowerPressed = (digitalRead(PIN_BUT_LP) == LOW); // Left power-up button (P1 Smash)  PIN 8  Naranđasta
  bool rightPowerPressed = (digitalRead(PIN_BUT_RP) == LOW);// Right power-up button (P2 Smash) PIN 6
  
  // int ballDirection = 1;     // 1 = moving right, -1 = moving left
  // ---- 3. SMASH ARMING LOGIC (Outside Hit Zone) ----
  // P1 arms Smash while ball moves towards them but is still outside the hit zone
      if (ballDirection == -1 && ballPosition >= zoneSize) {
    if (leftPowerPressed && !p1SmashUsed) {
      p1SmashArmed = true;
    }
  }
  // P2 arms Smash while ball moves towards them but is still outside the hit zone
      if (ballDirection == 1 && ballPosition < (LED_COUNT - zoneSize)) {
    if (rightPowerPressed && !p2SmashUsed) {
      p2SmashArmed = true;
    }
  }

  // ---- 4. LEFT PLAYER (P1) HIT ZONE LOGIC ----
  if (ballDirection == -1 && ballPosition < zoneSize) {
    if (leftHitPressed) {
      currentRallyCount++; // Add this whenever P1 or P2 successfully hits the ball

      ballDirection = 1; // Reverse direction
      currentRallyCount++; // <-- NEW: Increment rally counter on successful hit
      if (p1SmashArmed) {
        // Executing the Smash!
        p1SmashUsed = true;
        p1SmashArmed = false;
        isSmashActive = true;
        currentSpeed = SMASH_SPEED; // Set to ultra-fast Smash speed
        triggerTone(TONE_SMASH);
      } else {
        // Executing regular hit
        isSmashActive = false;
        if (ballPosition < PERFECT_ZONE_LEDS) {
          // Perfect timing bonus
          currentSpeed = currentSpeed - (SPEED_STEP * PERFECT_BONUS_STEPS);
        } else {
          // Normal hit
          currentSpeed = currentSpeed - SPEED_STEP;
        }
        triggerTone(TONE_HIT);
      }
      
      if (currentSpeed < 20) currentSpeed = 20; // Safety speed floor
      return;
    }
  }

  // ---- 5. RIGHT PLAYER (P2) HIT ZONE LOGIC ----
  if (ballDirection == 1 && ballPosition >= (LED_COUNT - zoneSize)) {
    if (rightHitPressed) {
      currentRallyCount++; // Add this whenever P1 or P2 successfully hits the ball

      ballDirection = -1; // Reverse direction
      currentRallyCount++; // <-- NEW: Increment rally counter on successful hit
      if (p2SmashArmed) {
        // Executing the Smash!
        p2SmashUsed = true;
        p2SmashArmed = false;
        isSmashActive = true;
        currentSpeed = SMASH_SPEED; // Set to ultra-fast Smash speed
        triggerTone(TONE_SMASH);
      } else {
        // Executing regular hit
        isSmashActive = false;
        if (ballPosition >= (LED_COUNT - PERFECT_ZONE_LEDS)) {
          // Perfect timing bonus
          currentSpeed = currentSpeed - (SPEED_STEP * PERFECT_BONUS_STEPS);
        } else {
          // Normal hit
          currentSpeed = currentSpeed - SPEED_STEP;
        }
        triggerTone(TONE_HIT);
      }
      
      if (currentSpeed < 20) currentSpeed = 20; // Safety speed floor
      return;
    }
  }

  // ---- 6. EARLY HIT / FOUL LOGIC (Punish instant Smash loss) ----
  // P1 early hit foul
  if (ballDirection == -1 && leftHitPressed && ballPosition >= zoneSize) { // original 1
    scoreRight++;
    if (p1SmashArmed) p1SmashUsed = true; // Loses Smash due to early foul abuse
    p1SmashArmed = false;
    p2SmashArmed = false;
    isSmashActive = false;
    handleScoreEvent(2, "P1 Foul! Early hit outside zone.");
    return;
  }
  // P2 early hit foul
  if (ballDirection == 1 && rightHitPressed && ballPosition < (LED_COUNT - zoneSize)) { // original 
    scoreLeft++;
    if (p2SmashArmed) p2SmashUsed = true; // Loses Smash due to early foul abuse
    p1SmashArmed = false;
    p2SmashArmed = false;
    isSmashActive = false;
    handleScoreEvent(1, "P2 Foul! Early hit outside zone.");
    return;
  }
}

void handleScoreEvent(int winner, String message) {
  isSmashActive = false;
  triggerTone(TONE_SCORE);
  
  // Track if a new all-time high score was broken in this round
  bool newRecordBroken = false;
  
  // Check match record
  if (currentRallyCount > maxRallyRecord) {
    maxRallyRecord = currentRallyCount;
  }
  
  // Check permanent all-time flash memory record
  if (currentRallyCount > highRallyAllTime) {
    highRallyAllTime = currentRallyCount;
    newRecordBroken = true;
    
    // Save to permanent ESP32 flash memory
    preferences.begin("pong", false);
    preferences.putInt("highRally", highRallyAllTime);
    preferences.end();
  }
  
  // If a record was broken, intercept the display message to congratulate the players
  if (newRecordBroken) {
    message = "NEW ALL-TIME RECORD: " + String(highRallyAllTime) + "!";
    dvostrukiBip(); // Extra audio feedback for breaking the record
  }
  
  // Reset the rally counter for the next point
  currentRallyCount = 0;

  if (DEBUG) {
    Serial.printf("Rally hits this round: %d | Max Record: %d\n", currentRallyCount, maxRallyRecord);
  }

  // Reset the counter for the next round
  currentRallyCount = 0; 
  
  if (DEBUG) {
    Serial.println("--- SCORE EVENT ---");
    Serial.println(message);
    Serial.printf("Current Score -> P1: %d | P2/AI: %d\n", scoreLeft, scoreRight);
  }

  // 2. Visual feedback: Flash the middle track with the winner's color
  unsigned long flashStart = millis();
  while (millis() - flashStart < 1500) {
    for (int i = 0; i < LED_COUNT; i++) {
      int r = 0, g = 0, b = 0;
      
      // Keep the background zones lit according to your design
      if (i < zoneSize) g = 10;
      else if (i >= LED_COUNT - zoneSize) r = 10;
      
      // Fill the middle area with the winner's solid color
      if (i >= zoneSize && i < LED_COUNT - zoneSize) {
        if (winner == 1) { 
          g = 50; // Green / Blue hue for Player 1 score depending on your strip wiring
        } else { 
          r = 50; // Red for Player 2 / AI score
        }
      }
      strip.setLedColorData(i, r, g, b);
    }
    strip.show();
    delay(50); // Short freeze delay for the pulsing animation effect
  }

  // 3. Check if the match is completely over, otherwise show score screen and reset
  if (scoreLeft >= WINNING_SCORE) {
    endGame(currentGameState == STATE_MULTIPLAYER ? " PLAYER 1 WINS!" : " YOU WIN THE MATCH!");
  } else if (scoreRight >= WINNING_SCORE) {
    endGame(currentGameState == STATE_MULTIPLAYER ? " PLAYER 2 WINS!" : " AI WINS THE MATCH!");
  } else {
    // Dynamically update the OLED display with the current status and points
    showScoreScreen(message);
    resetBall();
  }
  int ballDirection = (random(0, 2) == 0) ? -1 : 1;
}

void renderGameTrack() {
    for (int i = 0; i < LED_COUNT; i++) {
        int r = 0, g = 0, b = 0;

        if (i < zoneSize) {
            g = 15; 
        }
        else if (i >= LED_COUNT - zoneSize) {
            r = 15; 
        }

        if (i == ballPosition) {
            if (isSmashActive) {
                r = 255; g = 0; b = 255; 
            } else {
                r = 0; g = 200; b = 255; 
            }
        }

        strip.setLedColorData(i, r, g, b);
    }
    strip.show();
}

void resetBall() {
  isSmashActive = false;
  ballPosition = LED_COUNT / 2;
  ballDirection = random(0, 2) == 0 ? 1 : -1;
  currentSpeed = (currentGameState == STATE_MULTIPLAYER) ? MP_START_SPEED : SP_START_SPEED;

  updateGameButtonLEDs(); // <-- NEW: Setup steady button colors for the round
}

void showScoreScreen(String message) {
    String title = (currentGameState == STATE_MULTIPLAYER) ? "1D PONG - 2P MODE" : "1D PONG - SINGLEPLAYER";
    prikaziNaEkranu(title, message, "Score - P1: " + String(scoreLeft), "P2/AI: " + String(scoreRight));
    delay(1000);
}

void endGame(String winnerMsg) {
  prikaziNaEkranu(" GAME OVER", winnerMsg, " Final Score:", " P1: " + String(scoreLeft) + " | P2: " + String(scoreRight));
  clearButtonLEDs();
    currentRallyCount = 0;
  maxRallyRecord = 0;
    delay(5000);
  currentGameState = STATE_IDLE;
  showIdleScreen();
}

void showIdleScreen() {
  prikaziNaEkranu(" 1D PONG GAME v2.2", " IDLE | Record: " + String(highRallyAllTime), " 1P: Click Left ", " 2P: Hold Left (2s)");
}


void prikaziNaEkranu(String linija1, String linija2, String linija3, String linija4) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE); 
    display.setCursor(0, 0);   display.println(linija1);
    display.setCursor(0, 16);  display.println(linija2);
    display.setCursor(0, 32);  display.println(linija3);
    display.setCursor(0, 48);  display.println(linija4);
    display.display();
}

void runColorChaser() {
  // --- Your existing Freenove code for the main track remains here ---
  for (int i = 0; i < LED_COUNT; i++) {
    int red = map(sin((colorStep + i) * 0.1) * 127 + 127, 0, 255, 0, 50);
    int green = map(cos((colorStep + i) * 0.15) * 127 + 127, 0, 255, 0, 50);
    int blue = map(sin((colorStep + i) * 0.08) * 127 + 127, 0, 255, 0, 50);
    strip.setLedColorData(i, red, green, blue);
  }
  strip.show();
  colorStep++;

  // --- NEW: Dynamic colors for your button LEDs using Adafruit ---
  for (int i = 0; i < BUTTONS_LED_COUNT; i++) {
    // We can use the same colorStep to synchronize the animation pattern!
    int red = map(sin((colorStep + i) * 0.1) * 127 + 127, 0, 255, 0, 50);
    int green = map(cos((colorStep + i) * 0.15) * 127 + 127, 0, 255, 0, 50);
    int blue = map(sin((colorStep + i) * 0.08) * 127 + 127, 0, 255, 0, 50);
    
    // Adafruit uses a single 32-bit color object: Color(R, G, B)
    buttonsStrip.setPixelColor(i, buttonsStrip.Color(red, green, blue));
  }
  buttonsStrip.show(); // Refresh button LEDs
}
// ---- INDEPENDENT BUTTON LED FUNCTIONS ----

// 1. Turn off all button LEDs completely
void clearButtonLEDs() {
  buttonsStrip.clear();
  buttonsStrip.show();
}

// 2. Set steady colors for buttons during the game (e.g., Green for P1, Red for P2)
// This functions checks if Smash is still available to decide if the button should light up
void updateGameButtonLEDs() {
  buttonsStrip.clear();
  
  // Player 1 button (first 8 LEDs): Lights up green if Smash is STILL AVAILABLE
  if (!p1SmashUsed) {
    for (int i = 0; i < 8; i++) {
      //buttonsStrip.setPixelColor(i, buttonsStrip.Color(0, 40, 0)); // Dim Green
      buttonsStrip.setPixelColor(i, buttonsStrip.Color(0, 0, 40)); // Dim Blou
    }
  }
  
  // Player 2 button (last 8 LEDs): Lights up red if Smash is STILL AVAILABLE
  if (!p2SmashUsed) {
    for (int i = 8; i < 16; i++) {
      buttonsStrip.setPixelColor(i, buttonsStrip.Color(40, 0, 0)); // Dim Red
    }
  }
  
  buttonsStrip.show();
}

// 3. Strobe / Flash animation when a Smash is executed (flashes the active player's button)
void flashSmashButtonLEDs(int player) {
  for (int f = 0; f < 3; f++) { // 3 quick explosive flashes
    buttonsStrip.clear();
    
    int startIdx = (player == 1) ? 0 : 8;
    int endIdx = (player == 1) ? 8 : 16;
    
    // Turn ON flash color (Bright Magenta for Smash)
    for (int i = startIdx; i < endIdx; i++) {
      buttonsStrip.setPixelColor(i, buttonsStrip.Color(255, 0, 255));
    }
    buttonsStrip.show();
    delay(40);
    
    // Turn OFF flash color
    buttonsStrip.clear();
    buttonsStrip.show();
    delay(40);
  }
}


void triggerTone(int toneType) {
  // ESP32 tone generation using basic ledc or Arduino tone() if supported by core
  tone(BUZZER_PIN, toneType, 80); // Plays tone for 80 milliseconds
}

void kratkiBip() {
    digitalWrite(BUZZER_PIN, HIGH); delay(80); digitalWrite(BUZZER_PIN, LOW);
}

void dvostrukiBip() {
    digitalWrite(BUZZER_PIN, HIGH); delay(40); digitalWrite(BUZZER_PIN, LOW);
    delay(40);
    digitalWrite(BUZZER_PIN, HIGH); delay(40); digitalWrite(BUZZER_PIN, LOW);
}

void smashBip() {
    digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW);
}

void dugiBip() {
    digitalWrite(BUZZER_PIN, HIGH); delay(300); digitalWrite(BUZZER_PIN, LOW);
}
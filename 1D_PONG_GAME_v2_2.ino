/*
1D_PONG_GAME_v2.2

- Short press (< 1.5s): Singleplayer mode (vs AI)
- Long hold (> 2.0s): Multiplayer mode (2P)
- Added full 2-Player (Multiplayer) game mode logic with symmetrical button handling.
- Added dynamic speed acceleration with minimum limits and Perfect Timing Bonus (last 3 LEDs).
- Fixed mode selection logic (Left button short press for 1P, long hold >2s for 2P).
- Added dynamic speed scaling based on LED_COUNT (adapts speeds and steps for shorter/longer strips).
*/

// Internal LEDs on the ESP32S3 module
#define LED_PLAVA 45
#define LED_CRVENA 48
#define BUZZER_PIN 47     // Buzzer pin

// RGB LED Strip definitions (150 LEDs)
#include "Freenove_WS2812_Lib_for_ESP32.h"
#define LED_PIN       38
#define LED_COUNT     150 // Total LEDs on track
#define LED_CHANNEL   0   // RMT channel (0-3 on S3)
Freenove_ESP32_WS2812 strip(LED_COUNT, LED_PIN, LED_CHANNEL, TYPE_GRB);

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

// ---- ALL 4 PONG BUTTONS ----          
#define PIN_BUT_RS    5   // Right primary start/hit button
#define PIN_BUT_RP    6   // Right power-up button (P2 Smash)
#define PIN_BUT_LS    7   // Left primary start/hit button (Primary control & mode selection)
#define PIN_BUT_LP    8   // Left power-up button (P1 Smash)

// ---- GAME CONFIGURATION & ZONES ----
const int MIN_ZONE = 5;
const int MAX_ZONE = 25;
int zoneSize = constrain((LED_COUNT * 10) / 100, MIN_ZONE, MAX_ZONE); // ~10% of track length
const int PERFECT_ZONE_LEDS = 3; // Last 3 LEDs for perfect timing bonus

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

// ---- DYNAMIC SPEED & ACCELERATION CONFIGURATION ----
int SP_START_SPEED = 40;  
int MP_START_SPEED = 100; 

int SP_MIN_SPEED = 15;    
int MP_MIN_SPEED = 25;    

int SPEED_STEP = 5;               
const int PERFECT_BONUS_STEPS = 2; 
const int SMASH_SPEED = 8;         

int currentSpeed = SP_START_SPEED;
unsigned long lastBallMoveTime = 0;

// ---- POWER-UP (SMASH) TRACKING ----
bool p1SmashUsed = false; 
bool p2SmashUsed = false; 
bool isSmashActive = false; 

// ---- PONG GAME VARIABLES ----
int ballPosition = LED_COUNT / 2;
int ballDirection = 1;     // 1 = moving right, -1 = moving left

int scoreLeft = 0;
int scoreRight = 0;
const int WINNING_SCORE = 5;

// ---- DEBUG CONFIGURATION ----
const bool DEBUG = true;  
int colorStep = 0;

void setup() {
    if (DEBUG) {
        Serial.begin(115200);
        Serial.println("1D PONG v2.2 Initializing...");
    }

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
    strip.setBrightness(20); 

    if (LED_COUNT != 150) {
        float scaleFactor = 150.0 / (float)LED_COUNT;
        SP_START_SPEED = (int)(SP_START_SPEED * scaleFactor);
        MP_START_SPEED = (int)(MP_START_SPEED * scaleFactor);
        SP_MIN_SPEED = (int)(SP_MIN_SPEED * scaleFactor);
        MP_MIN_SPEED = (int)(MP_MIN_SPEED * scaleFactor);
        SPEED_STEP = (int)(SPEED_STEP * scaleFactor);
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

// Check ONLY Left button (Pin 7) for mode selection (v1.8 reliable logic)
void checkModeSelection() {
    bool lsPressed = (digitalRead(PIN_BUT_LS) == LOW);

    // 1. Button was just pressed down
    if (lsPressed && !lsIsBeingHeld) {
        lsIsBeingHeld = true;
        hold2PTriggered = false;
        lsPressStartTime = millis();
        prikaziNaEkranu("1D PONG - MODE SELECT", "Left button pressed...", "Release for 1P mode", "Hold 2s for 2P mode");
    } 
    // 2. Button is being continuously held
    else if (lsPressed && lsIsBeingHeld) {
        unsigned long duration = millis() - lsPressStartTime;
        
        // Check if held long enough for 2P mode (2 seconds)
        if (duration >= HOLD_TIME_2P && !hold2PTriggered) {
            hold2PTriggered = true; // Prevents re-triggering while holding
            currentGameState = STATE_MULTIPLAYER;
            scoreLeft = 0; scoreRight = 0;
            currentSpeed = MP_START_SPEED; 
            dugiBip();
            if(DEBUG) Serial.println("-> Activated: MULTIPLAYER (2P) MODE!");
            prikaziNaEkranu("1D PONG", "Mode Selected:", "MULTIPLAYER (2P)", "Get Ready!");
            delay(1500);
            resetBall();
        }
    } 
    // 3. Button was released
    else if (!lsPressed && lsIsBeingHeld) {
        lsIsBeingHeld = false;
        unsigned long duration = millis() - lsPressStartTime;

        // If it wasn't held long enough for 2P, it counts as a short press for 1P
        if (!hold2PTriggered && duration < HOLD_TIME_2P) {
            currentGameState = STATE_SINGLEPLAYER;
            scoreLeft = 0; scoreRight = 0;
            currentSpeed = SP_START_SPEED; 
            kratkiBip();
            if(DEBUG) Serial.println("-> Activated: SINGLEPLAYER (vs AI) MODE!");
            prikaziNaEkranu("1D PONG", "Mode Selected:", "SINGLEPLAYER (vs AI)", "Get Ready!");
            delay(1500);
            resetBall();
        }
    }
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
    // if(DEBUG) Serial.println("CURENT SPEED = " + String(currentSpeed));
 
}

// Multiplayer game loop with button debounce protection
void updateMultiplayerGame() {
    bool currentLsState = (digitalRead(PIN_BUT_LS) == LOW);
    bool currentRsState = (digitalRead(PIN_BUT_RS) == LOW);
    bool currentLpState = (digitalRead(PIN_BUT_LP) == LOW);
    bool currentRpState = (digitalRead(PIN_BUT_RP) == LOW);

    static bool lastLsPinState = false;
    static bool lastRsPinState = false;
    static bool lastLpPinState = false;
    static bool lastRpPinState = false;

    unsigned long currentTime = millis();

    // 1. Check Player 1 SMASH Hit (Pin 8)
    if (currentLpState && !lastLpPinState && (currentTime - lastDebounceTimeLP > DEBOUNCE_DELAY)) {
        lastDebounceTimeLP = currentTime;
        if (!p1SmashUsed && ballDirection == -1 && ballPosition <= zoneSize) {
            p1SmashUsed = true;
            isSmashActive = true;
            ballDirection = 1;
            currentSpeed = SMASH_SPEED;
            smashBip();
            if(DEBUG) Serial.println("P1 ULTIMATE SMASH!");
        }
    }
    lastLpPinState = currentLpState;

    // 2. Check Player 2 SMASH Hit (Pin 6)
    if (currentRpState && !lastRpPinState && (currentTime - lastDebounceTimeRP > DEBOUNCE_DELAY)) {
        lastDebounceTimeRP = currentTime;
        if (!p2SmashUsed && ballDirection == 1 && ballPosition >= (LED_COUNT - 1 - zoneSize)) {
            p2SmashUsed = true;
            isSmashActive = true;
            ballDirection = -1;
            currentSpeed = SMASH_SPEED;
            smashBip();
            if(DEBUG) Serial.println("P2 ULTIMATE SMASH!");
        }
    }
    lastRpPinState = currentRpState;

    // 3. Check Player 1 Normal Hit (Pin 7)
    if (currentLsState && !lastLsPinState && (currentTime - lastDebounceTimeLS > DEBOUNCE_DELAY)) {
        lastDebounceTimeLS = currentTime;
        if (ballDirection == -1 && ballPosition <= zoneSize) {
            ballDirection = 1;
            isSmashActive = false;
            
            if (ballPosition < PERFECT_ZONE_LEDS) {
                int totalReduction = SPEED_STEP + (SPEED_STEP * PERFECT_BONUS_STEPS);
                currentSpeed = max(MP_MIN_SPEED, currentSpeed - totalReduction);
                dvostrukiBip();
                if(DEBUG) Serial.println("P1 PERFECT HIT!");
            } else {
                currentSpeed = max(MP_MIN_SPEED, currentSpeed - SPEED_STEP);
                kratkiBip();
                if(DEBUG) Serial.println("P1 Normal Hit!");
            }
        } else if (ballDirection == -1) {
            scoreRight++;
            dugiBip();
            handleScoreEvent(2, "P1 Early Hit! P2 Scores!");
            return;
        }
    }
    lastLsPinState = currentLsState;

    // 4. Check Player 2 Normal Hit (Pin 5)
    if (currentRsState && !lastRsPinState && (currentTime - lastDebounceTimeRS > DEBOUNCE_DELAY)) {
        lastDebounceTimeRS = currentTime;
        if (ballDirection == 1 && ballPosition >= (LED_COUNT - 1 - zoneSize)) {
            ballDirection = -1;
            isSmashActive = false;
            
            if (ballPosition >= (LED_COUNT - PERFECT_ZONE_LEDS)) {
                int totalReduction = SPEED_STEP + (SPEED_STEP * PERFECT_BONUS_STEPS);
                currentSpeed = max(MP_MIN_SPEED, currentSpeed - totalReduction);
                dvostrukiBip();
                if(DEBUG) Serial.println("P2 PERFECT HIT!");
            } else {
                currentSpeed = max(MP_MIN_SPEED, currentSpeed - SPEED_STEP);
                kratkiBip();
                if(DEBUG) Serial.println("P2 Normal Hit!");
            }
        } else if (ballDirection == 1) {
            scoreLeft++;
            dugiBip();
            handleScoreEvent(1, "P2 Early Hit! P1 Scores!");
            return;
        }
    }
    lastRsPinState = currentRsState;

    // 5. Move Ball
    if (millis() - lastBallMoveTime >= currentSpeed) {
        lastBallMoveTime = millis();
        ballPosition += ballDirection;

        if (ballPosition < 0) {
            scoreRight++;
            dugiBip();
            handleScoreEvent(2, "P1 Missed! P2 Scores!");
            return;
        } 
        else if (ballPosition >= LED_COUNT) {
            scoreLeft++;
            dugiBip();
            handleScoreEvent(1, "P2 Missed! P1 Scores!");
            return;
        }
    }

    renderGameTrack();
}

// Handle scoring event: show winner color on middle track & update screen
void handleScoreEvent(int winner, String message) {
    isSmashActive = false;
    unsigned long flashStart = millis();
    while (millis() - flashStart < 1500) { 
        for (int i = 0; i < LED_COUNT; i++) {
            int r = 0, g = 0, b = 0;
            if (i < zoneSize) g = 10;
            else if (i >= LED_COUNT - zoneSize) r = 10;

            if (i >= zoneSize && i < LED_COUNT - zoneSize) {
                if (winner == 1) { g = 50; } // Green for Left (P1)
                else { r = 50; }             // Red for Right (P2/AI)
            }
            strip.setLedColorData(i, r, g, b);
        }
        strip.show();
        delay(50);
    }

    if (scoreLeft >= WINNING_SCORE) {
        endGame(currentGameState == STATE_MULTIPLAYER ? "PLAYER 1 WINS!" : "YOU WIN THE MATCH!");
    } else if (scoreRight >= WINNING_SCORE) {
        endGame(currentGameState == STATE_MULTIPLAYER ? "PLAYER 2 WINS!" : "AI WINS THE MATCH!");
    } else {
        showScoreScreen(message);
        resetBall();
    }
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
}

void showScoreScreen(String message) {
    String title = (currentGameState == STATE_MULTIPLAYER) ? "1D PONG - 2P MODE" : "1D PONG - SINGLEPLAYER";
    prikaziNaEkranu(title, message, "Score - P1: " + String(scoreLeft), "P2/AI: " + String(scoreRight));
    delay(1000);
}

void endGame(String winnerMsg) {
    prikaziNaEkranu("GAME OVER", winnerMsg, "Final Score:", "P1: " + String(scoreLeft) + " | P2: " + String(scoreRight));
    delay(4000);
    currentGameState = STATE_IDLE;
    showIdleScreen();
}

void showIdleScreen() {
    prikaziNaEkranu("1D PONG GAME v2.2", "Status: IDLE", "1P: Click Left Button", "2P: Hold Left (2s)");
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
    for (int i = 0; i < LED_COUNT; i++) {
        int red = map(sin((colorStep + i) * 0.1) * 127 + 127, 0, 255, 0, 50);
        int green = map(cos((colorStep + i) * 0.15) * 127 + 127, 0, 255, 0, 50);
        int blue = map(sin((colorStep + i) * 0.08) * 127 + 127, 0, 255, 0, 50);
        strip.setLedColorData(i, red, green, blue);
    }
    strip.show();
    colorStep++;
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
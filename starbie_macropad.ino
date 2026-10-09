
/*
  STARBIE + BLUETOOTH MACROPAD
  Board: Seeed XIAO ESP32-C3

  D0 = DHT11 data
  D1 = Button 1
  D2 = Mode toggle
  D3 = Button 3
  D4 = Button 4
  D5 = Button 5
  D6 = Button 6
  D7 = OLED SDA
  D8 = OLED SCL

  Buttons connect their GPIO pin to GND when pressed.

  STARBIE MODE:
    D1 = Open/confirm tilt menu
    D2 = Switch to Macropad mode
    D3 = Show/hide stats
    D4 = Nap
    D5 = Play
    D6 = Feed

  MACROPAD MODE (Windows):
    D1 = Copy
    D2 = Return to Starbie mode
    D3 = Paste
    D4 = Cut
    D5 = Undo
    D6 = Screenshot (Win + Shift + S)
*/

#define USE_NIMBLE
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <Preferences.h>
#include <BleKeyboard.h>
#include <math.h>

// -------------------- PIN SETTINGS --------------------

const int SDA_PIN = 20;       // XIAO D7
const int SCL_PIN = 8;        // XIAO D8
const int DHT_PIN = 2;        // XIAO D0

const int BUTTON_D1 = 3;
const int BUTTON_D2 = 4;
const int BUTTON_D3 = 5;
const int BUTTON_D4 = 6;
const int BUTTON_D5 = 7;
const int BUTTON_D6 = 21;

const int BUTTON_PINS[6] = {
  BUTTON_D1, BUTTON_D2, BUTTON_D3,
  BUTTON_D4, BUTTON_D5, BUTTON_D6
};

const uint8_t OLED_ADDRESS = 0x3C;
const uint8_t MPU_ADDRESS = 0x68;

const bool USE_DHT11 = true;
const bool RESET_SAVED_PET_ON_BOOT = false;

// -------------------- DEVICES --------------------

const int SCREEN_WIDTH = 128;
const int SCREEN_HEIGHT = 64;

Adafruit_SSD1306 display(
  SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1
);

Adafruit_MPU6050 mpu;
DHT dht(DHT_PIN, DHT11);
Preferences preferences;

BleKeyboard bleKeyboard(
  "Starbie Macropad", "Starbie", 100
);

// -------------------- PET SETTINGS --------------------

const int PET_WIDTH = 32;
const int PET_HEIGHT = 32;

const int STARTING_JOY = 70;
const int STARTING_ENERGY = 75;
const int STARTING_FULLNESS = 65;

const uint32_t WALK_PIXEL_MS = 70;
const uint32_t PRE_JUMP_MS = 230;
const uint32_t JUMP_MS = 430;
const int JUMP_HEIGHT = 16;

const uint32_t NAP_DURATION_MS = 48000;
const uint32_t HEART_DURATION_MS = 1600;
const uint32_t PLAY_LAP_MS = 800;
const int PLAY_LAP_COUNT = 2;

const uint32_t SHAKE_COOLDOWN_MS = 650;
const float SHAKE_THRESHOLD = 7.0f;
const float GRAVITY = 9.80665f;

const float MENU_TILT_LIMIT = 6.0f;
const float MENU_DEADZONE = 0.8f;
const float MENU_X_DIRECTION = 1.0f;
const float MENU_Y_DIRECTION = -1.0f;
const bool SWAP_MPU_AXES = false;

const uint32_t BUTTON_DEBOUNCE_MS = 30;
const uint32_t MPU_INTERVAL_MS = 30;
const uint32_t DHT_INTERVAL_MS = 2200;

// -------------------- PET DATA --------------------

struct PetState {
  int joy;
  int energy;
  int fullness;
};

PetState pet = {
  STARTING_JOY, STARTING_ENERGY, STARTING_FULLNESS
};

struct ButtonState {
  bool stableState;
  bool lastRawState;
  uint32_t lastChangedAt;
};

ButtonState buttons[6];

enum DeviceMode {
  STARBIE_MODE,
  MACROPAD_MODE
};

enum ScreenView {
  PET_VIEW,
  MENU_VIEW,
  STATS_VIEW
};

DeviceMode currentMode = STARBIE_MODE;
ScreenView currentView = PET_VIEW;

bool mpuFound = false;
bool dhtFound = false;

float accelX = 0;
float accelY = 0;
float accelZ = GRAVITY;

float temperatureC = NAN;
float humidity = NAN;

float menuBallX = 64;
float menuBallY = 32;

float menuCenterX = 0;
float menuCenterY = 0;

int selectedMenuItem = -1;
int nappingPetX = 0;

uint32_t lastMpuReadAt = 0;
uint32_t lastDhtReadAt = 0;
uint32_t lastShakeAt = 0;

uint32_t shakeAnimationEndsAt = 0;
uint32_t jumpStartedAt = 0;
uint32_t nappingUntil = 0;
uint32_t heartsUntil = 0;
uint32_t playStartedAt = 0;

// -------------------- PET SPRITE --------------------

const uint8_t PROGMEM PET_SPRITE[] = {
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x70,0x0a,0x00,
  0x00,0xf8,0x1f,0x00,
  0x00,0x8c,0x3f,0x80,
  0x00,0x43,0xff,0x00,
  0x00,0x43,0xff,0x00,
  0x00,0x30,0x7e,0x00,
  0x00,0x98,0x01,0x00,
  0x01,0x98,0x01,0x80,
  0x03,0x00,0x10,0xc0,
  0x03,0x04,0x00,0xc0,
  0x01,0x8b,0x01,0x80,
  0x01,0xcb,0x03,0x80,
  0x03,0xc0,0x03,0xc0,
  0x03,0xc0,0x03,0xc0,
  0x01,0xc0,0x03,0x80,
  0x01,0x80,0x01,0x80,
  0x00,0x60,0x06,0x00,
  0x00,0x3f,0xfc,0x00,
  0x00,0x7f,0xfe,0x00,
  0x00,0x70,0x0e,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00
};

// -------------------- MENU ACTIONS --------------------

enum PetReaction {
  NAP_REACTION,
  RUN_REACTION,
  JUMP_REACTION,
  HEART_REACTION
};

struct MenuItem {
  const char *label;
  int joy;
  int energy;
  int fullness;
  PetReaction reaction;
};

const MenuItem MENU_ITEMS[] = {
  {"NAP",  1, 18, -4, NAP_REACTION},
  {"PLAY", 12, -9, -5, RUN_REACTION},
  {"FEED", 3, 2, 18, JUMP_REACTION},
  {"PET",  7, 0, 0, HEART_REACTION}
};

const int MENU_X[] = {64, 108, 64, 20};
const int MENU_Y[] = {12, 32, 53, 32};

// -------------------- GENERAL HELPERS --------------------

int clampStat(int value) {
  return constrain(value, 0, 100);
}

void changePet(int joy, int energy, int fullness) {
  pet.joy = clampStat(pet.joy + joy);
  pet.energy = clampStat(pet.energy + energy);
  pet.fullness = clampStat(pet.fullness + fullness);
}

void savePet() {
  preferences.putInt("joy", pet.joy);
  preferences.putInt("energy", pet.energy);
  preferences.putInt("full", pet.fullness);
}

void loadPet() {
  preferences.begin("starbie", false);

  if (RESET_SAVED_PET_ON_BOOT) {
    preferences.clear();
  }

  pet.joy = preferences.getInt("joy", STARTING_JOY);
  pet.energy = preferences.getInt("energy", STARTING_ENERGY);
  pet.fullness = preferences.getInt("full", STARTING_FULLNESS);
}

bool wasPressed(int index) {
  bool raw = digitalRead(BUTTON_PINS[index]);
  uint32_t now = millis();

  if (raw != buttons[index].lastRawState) {
    buttons[index].lastRawState = raw;
    buttons[index].lastChangedAt = now;
  }

  if (raw != buttons[index].stableState &&
      now - buttons[index].lastChangedAt >=
      BUTTON_DEBOUNCE_MS) {
    buttons[index].stableState = raw;
    return buttons[index].stableState == LOW;
  }

  return false;
}

bool isNapping() {
  return nappingUntil != 0 && millis() < nappingUntil;
}

bool isPlaying() {
  return playStartedAt != 0 &&
         millis() - playStartedAt <
         PLAY_LAP_MS * PLAY_LAP_COUNT;
}

int walkingX(uint32_t now) {
  const int farthest = SCREEN_WIDTH - PET_WIDTH;
  const uint32_t roundTrip = farthest * 2UL;
  const uint32_t step = (now / WALK_PIXEL_MS) % roundTrip;

  return step <= farthest ? step : roundTrip - step;
}

int runningX(uint32_t now) {
  const int farthest = SCREEN_WIDTH - PET_WIDTH;
  float progress = float((now - playStartedAt) % PLAY_LAP_MS)
                   / PLAY_LAP_MS;

  if (progress < 0.5f) {
    return int(progress * 2.0f * farthest);
  }

  return int((1.0f - progress) * 2.0f * farthest);
}

// -------------------- SENSOR UPDATES --------------------

void updateMPU() {
  if (!mpuFound ||
      millis() - lastMpuReadAt < MPU_INTERVAL_MS) {
    return;
  }

  lastMpuReadAt = millis();

  sensors_event_t acceleration;
  sensors_event_t gyro;
  sensors_event_t sensorTemperature;

  mpu.getEvent(
    &acceleration, &gyro, &sensorTemperature
  );

  accelX = acceleration.acceleration.x;
  accelY = acceleration.acceleration.y;
  accelZ = acceleration.acceleration.z;
}

void updateDHT() {
  if (!dhtFound ||
      millis() - lastDhtReadAt < DHT_INTERVAL_MS) {
    return;
  }

  lastDhtReadAt = millis();

  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  if (!isnan(newHumidity)) {
    humidity = newHumidity;
  }

  if (!isnan(newTemperature)) {
    temperatureC = newTemperature;
  }
}

// -------------------- PET ACTIONS --------------------

void doNap() {
  uint32_t now = millis();

  nappingPetX = walkingX(now);
  nappingUntil = now + NAP_DURATION_MS;

  jumpStartedAt = 0;
  playStartedAt = 0;
  heartsUntil = 0;

  changePet(1, 18, -4);
  savePet();
}

void doPlay() {
  playStartedAt = millis();
  jumpStartedAt = 0;
  nappingUntil = 0;
  heartsUntil = playStartedAt + PLAY_LAP_MS * PLAY_LAP_COUNT;

  changePet(12, -9, -5);
  savePet();
}

void doFeed() {
  uint32_t now = millis();

  nappingUntil = 0;
  playStartedAt = 0;
  jumpStartedAt = now;
  heartsUntil = 0;

  changePet(3, 2, 18);
  savePet();
}

void doPet() {
  uint32_t now = millis();

  nappingUntil = 0;
  playStartedAt = 0;
  jumpStartedAt = now;
  heartsUntil = now + HEART_DURATION_MS;

  changePet(7, 0, 0);
  savePet();
}

void updatePetTimers() {
  uint32_t now = millis();

  if (nappingUntil != 0 && now >= nappingUntil) {
    nappingUntil = 0;
  }

  if (playStartedAt != 0 && !isPlaying()) {
    playStartedAt = 0;
  }

  if (jumpStartedAt != 0 &&
      now - jumpStartedAt >= PRE_JUMP_MS + JUMP_MS) {
    jumpStartedAt = 0;
  }
}

void checkForShake() {
  if (!mpuFound || currentView != PET_VIEW) {
    return;
  }

  float magnitude = sqrtf(
    accelX * accelX +
    accelY * accelY +
    accelZ * accelZ
  );

  uint32_t now = millis();

  if (fabsf(magnitude - GRAVITY) >= SHAKE_THRESHOLD &&
      now - lastShakeAt >= SHAKE_COOLDOWN_MS) {
    lastShakeAt = now;
    nappingUntil = 0;
    shakeAnimationEndsAt = now + 350;

    changePet(5, -2, -1);
    savePet();
  }
}

// -------------------- RADIAL MENU --------------------

void openMenu() {
  currentView = MENU_VIEW;
  selectedMenuItem = -1;

  menuBallX = 64;
  menuBallY = 32;

  menuCenterX = accelX;
  menuCenterY = accelY;
}

void updateMenuBall() {
  if (!mpuFound) {
    selectedMenuItem = -1;
    return;
  }

  float xTilt = accelX - menuCenterX;
  float yTilt = accelY - menuCenterY;

  if (SWAP_MPU_AXES) {
    float temp = xTilt;
    xTilt = yTilt;
    yTilt = temp;
  }

  xTilt = constrain(
    xTilt * MENU_X_DIRECTION,
    -MENU_TILT_LIMIT, MENU_TILT_LIMIT
  );

  yTilt = constrain(
    yTilt * MENU_Y_DIRECTION,
    -MENU_TILT_LIMIT, MENU_TILT_LIMIT
  );

  float targetX = 64 + (xTilt / MENU_TILT_LIMIT) * 37;
  float targetY = 32 + (yTilt / MENU_TILT_LIMIT) * 22;

  menuBallX += (targetX - menuBallX) * 0.20f;
  menuBallY += (targetY - menuBallY) * 0.20f;

  if (fabsf(xTilt) < MENU_DEADZONE &&
      fabsf(yTilt) < MENU_DEADZONE) {
    selectedMenuItem = -1;
  } else if (fabsf(xTilt) > fabsf(yTilt)) {
    selectedMenuItem = xTilt > 0 ? 1 : 3;
  } else {
    selectedMenuItem = yTilt > 0 ? 2 : 0;
  }
}

void chooseMenuItem() {
  if (selectedMenuItem < 0) {
    return;
  }

  const MenuItem &item = MENU_ITEMS[selectedMenuItem];

  if (item.reaction == NAP_REACTION) {
    doNap();
  } else if (item.reaction == RUN_REACTION) {
    doPlay();
  } else if (item.reaction == JUMP_REACTION) {
    doFeed();
  } else {
    doPet();
  }

  currentView = PET_VIEW;
}

// -------------------- BLUETOOTH MACROPAD --------------------

void sendShortcut(uint8_t modifier, char key) {
  if (!bleKeyboard.isConnected()) {
    Serial.println("Macropad: Bluetooth not connected.");
    return;
  }

  bleKeyboard.press(modifier);
  bleKeyboard.press(key);

  delay(40);
  bleKeyboard.releaseAll();
  delay(30);
}

void sendScreenshotShortcut() {
  if (!bleKeyboard.isConnected()) {
    Serial.println("Macropad: Bluetooth not connected.");
    return;
  }

  // Windows + Shift + S
  bleKeyboard.press(KEY_LEFT_GUI);
  bleKeyboard.press(KEY_LEFT_SHIFT);
  bleKeyboard.press('s');

  delay(60);
  bleKeyboard.releaseAll();
  delay(30);
}

void handleMacropadButtons() {
  if (wasPressed(0)) {
    sendShortcut(KEY_LEFT_CTRL, 'c');  // D1: Copy
  }

  if (wasPressed(2)) {
    sendShortcut(KEY_LEFT_CTRL, 'v');  // D3: Paste
  }

  if (wasPressed(3)) {
    sendShortcut(KEY_LEFT_CTRL, 'x');  // D4: Cut
  }

  if (wasPressed(4)) {
    sendShortcut(KEY_LEFT_CTRL, 'z');  // D5: Undo
  }

  if (wasPressed(5)) {
    sendScreenshotShortcut();          // D6: Screenshot
  }
}

// -------------------- MODE AND BUTTON HANDLING --------------------

void toggleMode() {
  if (currentMode == STARBIE_MODE) {
    currentMode = MACROPAD_MODE;
    currentView = PET_VIEW;
    Serial.println("Mode: MACROPAD");
  } else {
    currentMode = STARBIE_MODE;
    currentView = PET_VIEW;
    Serial.println("Mode: STARBIE");
  }
}

void handleButtons() {
  // D2 always changes modes, regardless of the current mode.
  if (wasPressed(1)) {
    toggleMode();
    return;
  }

  if (currentMode == MACROPAD_MODE) {
    handleMacropadButtons();
    return;
  }

  // Starbie mode:
  if (wasPressed(0)) {
    if (currentView == MENU_VIEW) {
      chooseMenuItem();
    } else {
      openMenu();
    }
  }

  if (wasPressed(2)) {
    currentView = currentView == STATS_VIEW
                    ? PET_VIEW
                    : STATS_VIEW;
  }

  // Three extra direct-action buttons.
  if (wasPressed(3)) {
    doNap();
    currentView = PET_VIEW;
  }

  if (wasPressed(4)) {
    doPlay();
    currentView = PET_VIEW;
  }

  if (wasPressed(5)) {
    doFeed();
    currentView = PET_VIEW;
  }
}

// -------------------- DISPLAY DRAWING --------------------

void drawHeart(int x, int y) {
  display.fillRect(x - 2, y, 2, 2, SSD1306_WHITE);
  display.fillRect(x + 1, y, 2, 2, SSD1306_WHITE);
  display.fillRect(x - 3, y + 2, 7, 2, SSD1306_WHITE);
  display.fillRect(x - 2, y + 4, 5, 1, SSD1306_WHITE);
  display.fillRect(x - 1, y + 5, 3, 1, SSD1306_WHITE);
  display.drawPixel(x, y + 6, SSD1306_WHITE);
}

void drawPet() {
  display.clearDisplay();

  uint32_t now = millis();

  int petX = isNapping() ? nappingPetX : walkingX(now);

  if (isPlaying()) {
    petX = runningX(now);
  }

  int petY = SCREEN_HEIGHT - PET_HEIGHT;

  if (now < shakeAnimationEndsAt) {
    petX += int(sinf(now / 18.0f) * 3.0f);
  }

  if (!isNapping() && jumpStartedAt != 0) {
    uint32_t age = now - jumpStartedAt;

    if (age < PRE_JUMP_MS) {
      petX += int(sinf(now / 16.0f) * 3.0f);
    } else if (age < PRE_JUMP_MS + JUMP_MS) {
      float progress = float(age - PRE_JUMP_MS) / JUMP_MS;
      petY -= int(sinf(progress * PI) * JUMP_HEIGHT);
    }
  }

  petX = constrain(petX, 0, SCREEN_WIDTH - PET_WIDTH);

  display.drawBitmap(
    petX, petY, PET_SPRITE,
    PET_WIDTH, PET_HEIGHT, SSD1306_WHITE
  );

  if (isNapping()) {
    int rise = (now / 300UL) % 15UL;

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(petX + 23, petY - 2 - rise);
    display.print("z");
    display.setCursor(petX + 28, petY - 8 - rise / 2);
    display.print("z");
  }

  if (now < heartsUntil) {
    float progress = 1.0f -
      float(heartsUntil - now) / HEART_DURATION_MS;

    int rise = int(progress * 18.0f);
    drawHeart(petX + 10, petY - 3 - rise);
    drawHeart(petX + 22, petY - 9 - rise / 2);
  }
}

void drawMenuItem(int item) {
  const int boxWidth = 33;
  const int boxHeight = 12;

  int x = MENU_X[item] - boxWidth / 2;
  int y = MENU_Y[item] - boxHeight / 2;

  bool selected = item == selectedMenuItem;

  if (selected) {
    display.fillRoundRect(
      x, y, boxWidth, boxHeight, 3, SSD1306_WHITE
    );
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRoundRect(
      x, y, boxWidth, boxHeight, 3, SSD1306_WHITE
    );
    display.setTextColor(SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setCursor(
    MENU_X[item] - strlen(MENU_ITEMS[item].label) * 3,
    MENU_Y[item] - 3
  );

  display.print(MENU_ITEMS[item].label);
  display.setTextColor(SSD1306_WHITE);
}

void drawMenu() {
  display.clearDisplay();

  display.drawLine(0, 0, 127, 63, SSD1306_WHITE);
  display.drawLine(0, 63, 127, 0, SSD1306_WHITE);

  for (int i = 0; i < 4; i++) {
    drawMenuItem(i);
  }

  display.drawCircle(64, 32, 11, SSD1306_WHITE);
  display.fillCircle(
    int(menuBallX), int(menuBallY), 4, SSD1306_WHITE
  );
}

void drawStatBar(int y, const char *label, int value) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, y);
  display.print(label);

  display.drawRect(48, y, 57, 8, SSD1306_WHITE);

  display.fillRect(
    49, y + 1, map(value, 0, 100, 0, 55),
    6, SSD1306_WHITE
  );

  display.setCursor(109, y);
  display.print(value);
}

void drawStats() {
  display.clearDisplay();

  drawStatBar(5, "JOY", pet.joy);
  drawStatBar(20, "ENERGY", pet.energy);
  drawStatBar(35, "FULL", pet.fullness);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 53);

  if (!isnan(temperatureC)) {
    display.print("T ");
    display.print(int(temperatureC));
    display.print("C");
  } else {
    display.print("T --");
  }

  display.setCursor(76, 53);

  if (!isnan(humidity)) {
    display.print("H ");
    display.print(int(humidity));
    display.print("%");
  } else {
    display.print("H --");
  }
}

void drawMacropad() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("MACROPAD");

  display.setCursor(0, 12);
  display.println("D1: Copy   D3: Paste");

  display.setCursor(0, 24);
  display.println("D4: Cut    D5: Undo");

  display.setCursor(0, 36);
  display.println("D6: Screenshot");

  display.setCursor(0, 50);

  if (bleKeyboard.isConnected()) {
    display.println("Bluetooth: Connected");
  } else {
    display.println("Bluetooth: Waiting...");
  }
}

void drawCurrentView() {
  if (currentMode == MACROPAD_MODE) {
    drawMacropad();
  } else if (currentView == MENU_VIEW) {
    drawMenu();
  } else if (currentView == STATS_VIEW) {
    drawStats();
  } else {
    drawPet();
  }

  display.display();
}

// -------------------- SETUP --------------------

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  for (int i = 0; i < 6; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);

    buttons[i].stableState = digitalRead(BUTTON_PINS[i]);
    buttons[i].lastRawState = buttons[i].stableState;
    buttons[i].lastChangedAt = millis();
  }

  loadPet();

  if (!display.begin(
        SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED not found. Check SDA/SCL and address.");

    while (true) {
      delay(10);
    }
  }

  mpuFound = mpu.begin(MPU_ADDRESS, &Wire);

  if (mpuFound) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("MPU6050 found.");
  } else {
    Serial.println("MPU6050 not found. Tilt menu unavailable.");
  }

  if (USE_DHT11) {
    dht.begin();
    dhtFound = true;
  }

  bleKeyboard.begin();

  Serial.println("Starbie started.");
  Serial.println("Pair 'Starbie Macropad' over Bluetooth.");

  drawCurrentView();
}

// -------------------- MAIN LOOP --------------------

void loop() {
  updateMPU();
  updateDHT();
  updatePetTimers();

  handleButtons();

  if (currentMode == STARBIE_MODE) {
    if (currentView == MENU_VIEW) {
      updateMenuBall();
    } else {
      checkForShake();
    }
  }

  drawCurrentView();

  delay(16);
}
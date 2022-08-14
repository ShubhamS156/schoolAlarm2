#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <MenuData.h>
#include <MenuManager.h>
#include <RtcDS3231.h>
#include <TTP229.h>
#include <Wire.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TTP229_SDO 25
#define TTP229_SCL 26
#define countof(a) (sizeof(a) / sizeof(a[0]))
#define TAG "Main"
/*keypress mappings to ttp229*/
#define RELEASE 0
#define ONE 1
#define TWO 2
#define THREE 3
#define UP 4
#define FOUR 5
#define FIVE 6
#define SIX 7
#define DOWN 8
#define SEVEN 9
#define EIGHT 10
#define NINE 11
#define MENU 12
#define DELETE 13
#define ZERO 14
#define BACK 15
#define ENT 16

/*-------------object init-------------*/
LiquidCrystal_I2C lcd(0x27, 20, 4);
TTP229 ttp229;
MenuManager obj(sampleMenu_Root, menuCount(sampleMenu_Root));
RtcDS3231<TwoWire> rtc(Wire);
/*--------------var init---------------*/
byte verticalLine[8] = {B00100, B00100, B00100, B00100,
                        B00100, B00100, B00100, B00100};

byte char2[8] = {B00000, B00000, B00000, B11100,
                 B00100, B00100, B00100, B00100};

byte char1[8] = {0b00000, 0b00000, 0b00000, 0b00111,
                 0b00100, 0b00100, 0b00100, 0b00100};

byte char3[8] = {0b00100, 0b00100, 0b00100, 0b00111,
                 0b00000, 0b00000, 0b00000, 0b00000};

byte char4[8] = {0b00100, 0b00100, 0b00100, 0b11100,
                 0b00000, 0b00000, 0b00000, 0b00000};

uint8_t arrow[8] = {0x00, 0x04, 0x06, 0x1f,
                    0x06, 0x04, 0x00}; // Send 0,4,6,1F,6,4,0 for the arrow

enum Modes { SUMMER = 0, WINTER, EXAM, UNDEFINED };

static SemaphoreHandle_t lcdMutex;
//-1 = undefined, 0=homescreen, 1=menuscreen
static int currentItem = -1; // currently selected menuItem
static int currentMode = UNDEFINED;

/*------------util funcs-----------------*/
void createCustomCharacters() {
  lcd.createChar(0, verticalLine);
  lcd.createChar(1, char1);
  lcd.createChar(2, char2);
  lcd.createChar(3, char3);
  lcd.createChar(4, char4);
  lcd.createChar(5, arrow);
}

void printSelected() {
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  const MenuItem *curr = obj.getMenuItem();

  // let caller handle clearing the screen.
  //  if (curr != sampleMenu_Root) {
  //    lcd.clear();
  //  }

  int counter = 1;
  lcd.setCursor(0, 0);
  lcd.blink();
  if (obj.getCurrentItemIndex() < 4) {
    // print top 4 items
    for (int i = 0; i < 4; i++) {
      lcd.print(curr[i].name);
      Serial.println(curr[i].name);
      lcd.setCursor(0, counter++);
    }
    lcd.setCursor(0, obj.getCurrentItemIndex());
    Serial.printf("Resetting cursor to %d\n", obj.getCurrentItemIndex());
  } else {
    // print the selected and above 3
    for (int i = obj.getCurrentItemIndex() - 3; i <= obj.getCurrentItemIndex();
         i++) {
      lcd.print(curr[i].name);
      lcd.setCursor(0, counter++);
    }
    lcd.setCursor(0, 3);
  }
  xSemaphoreGive(lcdMutex);
  lcd.write(5); // TODO: enable this when custom chars implemented.
}
void printFrame() {
  lcd.setCursor(1, 0);
  lcd.print("------------------");
  lcd.setCursor(1, 3);
  lcd.print("------------------");
  lcd.setCursor(0, 1);
  lcd.write(byte(0));
  lcd.setCursor(0, 2);
  lcd.write(byte(0));
  lcd.setCursor(19, 1);
  lcd.write(byte(0));
  lcd.setCursor(19, 2);
  lcd.write(byte(0));
  lcd.setCursor(0, 0);
  lcd.write(byte(1));
  lcd.setCursor(19, 0);
  lcd.write(byte(2));
  lcd.setCursor(0, 3);
  lcd.write(byte(3));
  lcd.setCursor(19, 3);
  lcd.write(byte(4));
  lcd.setCursor(2, 1);
}
void printTime(RtcDateTime &tm) {
  lcd.setCursor(2, 1);
  lcd.print(tm.Month());
  lcd.print("/");
  lcd.print(tm.Day());
  lcd.print("/");
  lcd.print((tm.Year()));

  String seconds, minutes;
  lcd.setCursor(2, 2);
  lcd.print(tm.Hour());
  lcd.print(":");
  if (tm.Minute() < 10) {
    minutes = "0" + String(tm.Minute());
    lcd.print(minutes);
  } else {
    lcd.print(tm.Minute());
  }
  lcd.print(":");
  if (tm.Second() < 10) {
    seconds = "0" + String(tm.Second());
    lcd.print(seconds);
  } else {
    lcd.print(tm.Second());
  }
}
void drawHome(RtcDateTime &dt) {
  lcd.blink_off();
  lcd.noCursor();
  printTime(dt);
  lcd.setCursor(13, 1);
  lcd.print("Mode");
  lcd.setCursor(14, 2);
  if (currentMode == SUMMER) {
    lcd.print("Sum");
  } else if (currentMode == WINTER) {
    lcd.print("Win");
  } else if (currentMode == EXAM) {
    lcd.print("Exm");
  } else if (currentMode == UNDEFINED) {
    lcd.print("N/a");
  }
}

void keyChange() {
  // A key press changed
  ttp229.keyChange = true;
}

/*------------tasks----------------------*/

void handleHome() {
  RtcDateTime now = rtc.GetDateTime();
  int actionKey = -1;
  int keyPressed = 0;
  char *buf;
  lcd.clear();
  while (currentItem == mnuCmdHome) {
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    printFrame();
    drawHome(now);
    xSemaphoreGive(lcdMutex);
    if (ttp229.keyChange) {
      keyPressed = ttp229.GetKey16();
      if (keyPressed != RELEASE) {
        actionKey = keyPressed;
        obj.getCurrentItemName(buf);
        Serial.printf("Screen:%s\n", buf);
        Serial.printf("actionKey=%d\n", actionKey);
      } else {
        if (actionKey != -1) {
          switch (actionKey) {
          case UP:
            break;
          case DOWN:
            break;
          case ENT:
            break;
          case MENU:
            currentItem = -1; // exit flag
            break;
          case BACK:
            break;
          case DELETE:
            break;
          default:
            break;
          }
          actionKey = -1;
        }
      }
      delay(500);
    }
  }
  lcd.clear();
  printSelected();
}
void keyPressTask(void *pvParameters) {
  printSelected();
  Serial.println("Starting Key Press Detection");
  int actionKey = -1;
  int keyPressed = 0;
  while (1) {
    if (ttp229.keyChange) {
      keyPressed = ttp229.GetKey16();
      if (keyPressed != RELEASE) {
        actionKey = keyPressed;
        Serial.printf("actionKey=%d\n", actionKey);
      } else {
        if (actionKey != -1) {
          switch (actionKey) {
          case UP:
            if (obj.moveToPreviousItem()) {
              currentItem = obj.getCurrentItemCmdId();
              printSelected();
              Serial.println("Moving UP");
            }
            break;
          case DOWN:
            if (obj.moveToNextItem()) {
              currentItem = obj.getCurrentItemCmdId();
              printSelected();
              Serial.println("Moving DOWN");
            }
            break;
          case ENT:
            if (obj.currentItemHasChildren()) {
              lcd.clear();
              obj.descendToChildMenu();
              printSelected();
            } else {
              switch (currentItem) {
              case mnuCmdHome:
                handleHome();
                Serial.println("Home Exited");
                break;
              case mnuCmdManual:
                break;
              case mnuCmdModeSelect:
                break;
              default:
                break;
              }
            }
            break;
          case MENU:
            break;
          case BACK:
            break;
          case DELETE:
            break;
          default:
            break;
          }
          actionKey = -1;
        }
      }
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
  }
  // TODO: why this?
  vTaskDelete(NULL);
}
// void keyPressTask(void *pvParameters) {
//   Serial.println("Starting KeyPress Detection");
//   int currId;
//   int actionKey = -1; // undefined, used to check which key is pressed.
//   int keyPressed = 0; // released
//   while (1) {
//     if (ttp229.keyChange) {
//       keyPressed = ttp229.GetKey16();
//       if (keyPressed != RELEASE) {
//         actionKey = keyPressed;
//         Serial.printf("key-pressed=%d\n", actionKey);
//       } else {
//         if (actionKey != -1) {
//           /* key handling start */
//           if (actionKey == UP) {
//             Serial.println("UP pressed");
//             switch (currentItem) { // TODO: add cases to this block to add
//                                    // meaning to UP
//             case mnuCmdHome:
//               break;
//             default:
//               // if there is item to move to
//               if (obj.moveToPreviousItem()) {
//                 currentItem = obj.getCurrentItemCmdId();
//                 printSelected(); // print the updates
//                 Serial.println("Going up");
//               } else {
//                 Serial.print("cant go up");
//               }
//               break;
//             }
//           } else if (actionKey == DOWN) {
//             switch (currentItem) { // TODO: add case here for key DOWN.
//             case mnuCmdHome:
//               break;
//             default:
//               if (obj.moveToNextItem()) {
//                 currentItem = obj.getCurrentItemCmdId();
//                 printSelected();
//                 Serial.println("Going down");
//               } else {
//                 Serial.println("cant go down");
//               }
//               break;
//             }
//           } else if (actionKey == ENT) {
//             switch (currentItem) {
//             case mnuCmdHome:
//               Serial.println("entering home");
//               // loopFlag = mnuCmdHome;
//               break;
//             case mnuCmdManual:
//               lcd.clear();
//               Serial.println("Entering manual mode");
//               lcd.print("entered manual mode");
//               break;
//             case mnuCmdModeSelect:
//               Serial.print("entered mode selection");
//               lcd.clear();
//               lcd.print("mode select");
//               break;
//             default:
//               if (obj.currentItemHasChildren()) {
//                 obj.descendToChildMenu();
//                 lcd.clear();
//                 printSelected();
//               }
//               break;
//             }
//           } else if (actionKey == BACK) {
//             switch (currentItem) {
//             case mnuCmdHome:
//               lcd.clear();
//               currentItem = mnuCmdManual;
//               printSelected();
//             default:
//               break;
//             }
//           }
//           actionKey = -1; // clear the action key.
//         }
//       }
//     }
//     vTaskDelay(300 / portTICK_PERIOD_MS);
//   }
//   vTaskDelete(NULL);
// }
/*--------------------freertos tasks--------------------*/

void setup() {
  Serial.begin(115200);
  Serial.println("Starting");
  lcd.begin();
  lcd.backlight();
  ttp229.begin(TTP229_SCL, TTP229_SDO);
  attachInterrupt(digitalPinToInterrupt(TTP229_SDO), keyChange, RISING);
  lcdMutex = xSemaphoreCreateMutex();
  if (lcdMutex == NULL) {
    Serial.println("Could not create mutex for lcdMutex");
    // TODO: how to heal this if occurs?
  } else {
    Serial.println("Mutex Created");
  }
  createCustomCharacters();
  xTaskCreate(keyPressTask, "keyPress", 4096, NULL, 3, NULL);
}

void loop() { delay(10000); }

// int keyPressCheck() {
//   int actionKey = -1;
//   int keyPressed = 0;
//   while (!exit) {
//     if (ttp229.keyChange) {
//       keyPressed = ttp229.GetKey16();
//       if (keyPressed != RELEASE) {
//         actionKey = keyPressed;
//         Serial.printf("actionKey=%d\n", actionKey);
//       } else {
//         if (actionKey != -1) {
//           switch (actionKey) {
//           case UP:
//             break;
//           case DOWN:
//             break;
//           case ENT:
//             break;
//           case MENU:
//             break;
//           case BACK:
//             break;
//           case DELETE:
//             break;
//           default:
//             break;
//           }
//           actionKey = -1;
//         }
//       }
//     }
//   }
// }
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
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
DFRobotDFPlayerMini myDFPlayer;
HardwareSerial mySoftwareSerial(2);
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
void gotoRoot() {
  obj.reset();
  currentItem = mnuCmdHome;
  lcd.clear();
  printSelected();
}
int parseNumKeys(int actionKey) {
  switch (actionKey) {
  case ONE:
    return 1;
    break;
  case TWO:
    return 2;
    break;
  case THREE:
    return 3;
    break;
  case FOUR:
    return 4;
    break;
  case FIVE:
    return 5;
    break;
  case SIX:
    return 6;
    break;
  case SEVEN:
    return 7;
    break;
  case EIGHT:
    return 8;
    break;
  case NINE:
    return 9;
    break;
  case ZERO:
    return 0;
    break;
  default:
    return -1; // if invalid key return -1;
    break;
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
void handleManualMode() {
  lcd.clear();
  lcd.blink_off();
  String msg = "FILE=x";
  String counter = "";
  int cnt = 0;
  int fileCount = myDFPlayer.readFileCounts();
  int actionKey = -1;
  int keyPressed = 0;
  bool exit = false;
  Serial.printf("FILECOUNT=%d\n", fileCount);
  if (fileCount > 0) {
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    lcd.setCursor(0, 0);
    lcd.print(msg);
    while (!exit) {
      if (ttp229.keyChange) {
        keyPressed = ttp229.GetKey16();
        if (keyPressed != RELEASE) {
          actionKey = keyPressed;
          Serial.printf("actionKey=%d\n", actionKey);
        } else {
          if (actionKey != -1) {
            switch (actionKey) {
            case UP:
              --cnt;
              if (cnt < 0) {
                cnt = 0;
              }
              break;
            case DOWN:
              ++cnt;
              if (cnt > fileCount) {
                cnt = fileCount;
              }
              break;
            case ENT:
              myDFPlayer.play(cnt);
              Serial.printf("Playing File=%d\n", cnt);
              break;
            case MENU:
            case BACK:
              exit = true;
              break;
            case DELETE:
              break;
            default:
              break;
            }
            lcd.setCursor(5, 0);
            if (cnt < 10) {
              counter = "0" + String(cnt);
            } else {
              counter = String(cnt);
            }
            lcd.print(counter);
            actionKey = -1;
          }
        }
        delay(200);
      }
    }
    xSemaphoreGive(lcdMutex);
    lcd.clear();
  } else {
    Serial.println("Invalid File Count");
  }
  printSelected();
}

void handleSetDateTime() {
  // cnt=which value out of hh:mm
  // cursorPos= position of cursor.
  int hour = 0, min = 0, cnt = 0, cursorPos = 0, tmp = 0;
  int time_row = 1;
  int timeBuf[4] = {0, 0, 0, 0};
  int actionKey = -1;
  int keyPressed = 0;
  bool exit = false;
  bool setTime = false;
  String invalidMsg = "Invalid Key";
  RtcDateTime now;
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.blink_on();
  lcd.print("Set Time");
  lcd.setCursor(cursorPos, time_row);
  lcd.print("00:00");
  lcd.setCursor(cursorPos, time_row);
  while (!exit) {
    if (ttp229.keyChange) {
      keyPressed = ttp229.GetKey16();
      if (keyPressed != RELEASE) {
        actionKey = keyPressed;
        Serial.printf("actionKey=%d\n", actionKey);
      } else {
        if (actionKey != -1) {
          switch (actionKey) {
          case UP:
            Serial.println("Invalid Key");
            break;
          case DOWN:
            Serial.println("Invalid Key");
            break;
          case ENT:
            hour = 10 * timeBuf[0] + timeBuf[1];
            min = 10 * timeBuf[2] + timeBuf[3];
            setTime = true;
            exit = true;
            break;
          case MENU:
            // TODO: experimental. use flag for menu pressed, exit while() then
            // run cleanup if this does not works.
            xSemaphoreGive(lcdMutex);
            gotoRoot();
            return;
            break;
          case BACK:
            // currentItem->stays same. just exit and show printSelected()
            exit = true;
            break;
          case DELETE:
            if (cnt == 0 || cursorPos == 0) {
              break;
            }
            --cnt;
            --cursorPos;
            timeBuf[cnt] = 0;
            if (cursorPos == 2) { // there is colon at 2
              cursorPos = 1;
            }
            lcd.setCursor(cursorPos, time_row);
            lcd.print("0");
            lcd.setCursor(cursorPos, time_row);
            break;
          default:
            // default means a number key is pressed.
            // break if we have already entered 4 numbers.
            if (cnt >= 4) {
              break;
            }
            tmp = parseNumKeys(actionKey);
            if (tmp == -1) {
              Serial.println(
                  "Invalid Number Key pressed, shouldnt be possible!!");
            } else {
              timeBuf[cnt] = tmp;
              lcd.setCursor(cursorPos,
                            time_row); // todo:check affect of removing this
                                       // ..my guess is nothing
              lcd.print(String(tmp));
              ++cnt;
              ++cursorPos;
              if (cursorPos == 2) {
                ++cursorPos;
              }
            }
            break;
          }
          actionKey = -1;
        }
      }
    }
  }

  now = rtc.GetDateTime();
  RtcDateTime updated(now.Year(), now.Month(), now.Day(), hour, min, 0);
  updated = now;
  rtc.SetDateTime(updated);
  lcd.clear();
  xSemaphoreGive(lcdMutex);
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
              obj.descendToChildMenu();
              currentItem = obj.getCurrentItemCmdId();
              lcd.clear();
              printSelected();
            } else {
              switch (currentItem) {
              case mnuCmdHome:
                Serial.println("Home Entered");
                handleHome();
                Serial.println("Home Exited");
                break;
              case mnuCmdManual:
                Serial.println("ManualMode Entered");
                handleManualMode();
                Serial.println("ManualMode Exited");
                break;
              case mnuCmdSummer:
                currentMode = SUMMER;
                Serial.println("Mode=SUMMER");
                handleHome();
                break;
              case mnuCmdWinter:
                currentMode = WINTER;
                Serial.println("Mode=WINTER");
                handleHome();
                break;
              case mnuCmdExam:
                currentMode = EXAM;
                Serial.println("Mode=EXAM");
                handleHome();
                break;
              case mnuCmdOFF:
                currentMode = UNDEFINED;
                Serial.println("Mode=Undefined");
                break;
              case mnuCmdSetDateTime:
                Serial.println("SetDateTime Entered");
                handleSetDateTime();
                Serial.println("SetDateTime Exited");
              default:
                break;
              }
            }
            break;
          case MENU:
            obj.reset();
            currentItem = mnuCmdHome;
            lcd.clear();
            printSelected();
            break;
          case BACK:
            // todo: make it go to the item from where child was entered instead
            // of going to root of parent on pressing back
            if (obj.currentMenuHasParent()) {
              obj.ascendToParentMenu();
              currentItem = obj.getCurrentItemCmdId();
              lcd.clear();
              printSelected();
            }
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

  /*------------LCD-----------------*/
  lcd.begin();
  lcd.backlight();
  lcdMutex = xSemaphoreCreateMutex();
  if (lcdMutex == NULL) {
    Serial.println("Could not create mutex for lcdMutex");
    // TODO: how to heal this if occurs?
  } else {
    Serial.println("Mutex Created");
  }
  createCustomCharacters();

  /*-----------TTP229--------------*/
  ttp229.begin(TTP229_SCL, TTP229_SDO);
  attachInterrupt(digitalPinToInterrupt(TTP229_SDO), keyChange, RISING);
  /*-------------df player------------*/
  mySoftwareSerial.begin(9600, SERIAL_8N1, 16, 17);
  if (!myDFPlayer.begin(mySoftwareSerial)) {
    Serial.println("DF Module Error");
    while (1) {
    }
  }
  Serial.println(F("DFPlayer Mini online."));
  myDFPlayer.setTimeOut(500); // Set serial communictaion time out 500ms
  myDFPlayer.volume(26);      // Set volume value (0~30).
  myDFPlayer.EQ(DFPLAYER_EQ_JAZZ);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_AUX);
  /*---------rtc init------------*/
  rtc.Begin();
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  if (!rtc.IsDateTimeValid()) {
    if (rtc.LastError() != 0) {
      Serial.print("RTC communicatins error= ");
      Serial.println(rtc.LastError());
    } else {
      Serial.println("RTC lost confidence in date and time");
      rtc.SetDateTime(compiled);
    }
  }
  if (!rtc.GetIsRunning()) {
    Serial.println("RTC was not actively running, starting now");
    rtc.SetIsRunning(true);
  }

  RtcDateTime now = rtc.GetDateTime();
  if (now < compiled) {
    Serial.println("rtc is older than compile time. Updating");
    // rtc.SetDateTime(compiled); //NOTE: not updating date time
  } else {
    Serial.println("rtc time same or newer than compile time");
  }
  rtc.Enable32kHzPin(false);
  rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
  /*-----------Tasks-----------------*/
  xTaskCreate(keyPressTask, "keyPress", 4096, NULL, 3, NULL);
  // starting homescreen by default
  // handleHome();
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
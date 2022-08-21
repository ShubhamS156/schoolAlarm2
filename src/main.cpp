#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include <LiquidCrystal_I2C.h>
#include <MenuData.h>
#include <MenuManager.h>
#include <Preferences.h>
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
#define PROGSCHEDSIZE 24
#define BELLCOUNTMAX 50

/*--------------structs----------------*/
typedef struct {
  uint8_t hour;
  uint8_t min;
  uint8_t file;
} Bell __packed;

typedef struct {
  uint8_t id;
  uint8_t bellCount = 0;
  Bell *bells;
} ProgSched __packed;

typedef struct {
  int first = 0;
  int second = 0;
} Pair;
/*-------------object init-------------*/
LiquidCrystal_I2C lcd(0x27, 20, 4);
TTP229 ttp229;
MenuManager obj(sampleMenu_Root, menuCount(sampleMenu_Root));
RtcDS3231<TwoWire> rtc(Wire);
DFRobotDFPlayerMini myDFPlayer;
HardwareSerial mySoftwareSerial(2);
Preferences pref;
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
static int activeSchedIdx = -1; // no active yet.
ProgSched schedules[PROGSCHEDSIZE];
Bell bellArr[BELLCOUNTMAX];
ProgSched *activeSchedPtr = NULL;
ProgSched activeSchedule;
static bool schedFoundEeprom = false;
int activeBellCount = 0;
int activeBellCnt = 0;
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
void clearLcd() {
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.clear();
  xSemaphoreGive(lcdMutex);
}
void gotoRoot() {
  obj.reset();
  currentItem = mnuCmdHome;
  clearLcd();
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
  RtcDateTime now;
  bool exit = false;
  int actionKey = -1;
  int keyPressed = 0;
  char buf[100];
  clearLcd();
  while (!exit) {
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    printFrame();
    now = rtc.GetDateTime();
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
            // currentItem = mnuCmdManual; // exit flag
            exit = true;
            break;
          case BACK:
            break;
          case DELETE:
            Serial.printf("Erasing Prefs: %d\n", pref.clear());
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
  clearLcd();
  printSelected();
}
/*
Returns pair:
key,val
ENT,FILE
MENU,-1
BACK,-1
*/
Pair getFile(int min, int fileCount, String msg, int delayMs) {
  // String msg = "FILE=x";
  String counter = "";
  int cnt = min;
  // int fileCount = myDFPlayer.readFileCounts();
  int actionKey = -1;
  int keyPressed = 0;
  bool exit = false;
  Pair returnKey;
  returnKey.first = returnKey.second = 0;
  Serial.printf("FILECOUNT=%d\n", fileCount);
  if (fileCount > min) {
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    lcd.clear();
    lcd.blink_off();
    lcd.setCursor(0, 0);
    lcd.print(msg + String(min));
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
              if (cnt < min) {
                cnt = min;
              }
              break;
            case DOWN:
              ++cnt;
              if (cnt > fileCount) {
                cnt = fileCount;
              }
              break;
            case ENT:
              returnKey.first = ENT;
              returnKey.second = cnt;
              exit = true;
              break;
            case MENU:
              returnKey.first = MENU;
              returnKey.second = -1;
              exit = true;
            case BACK:
              returnKey.first = BACK;
              returnKey.second = -1;
              exit = true;
              break;
            case DELETE:
              break;
            default:
              break;
            }
            lcd.setCursor(msg.length(), 0);
            if (cnt < 10) {
              counter = "0" + String(cnt);
            } else {
              counter = String(cnt);
            }
            lcd.print(counter);
            actionKey = -1;
          }
        }
        delay(delayMs);
      }
    }
    xSemaphoreGive(lcdMutex);
  } else {
    Serial.println("Invalid File Count");
  }
  return returnKey;
}

void handleManualMode() {
  bool exit = false;
  Pair fileKey;
  while (!exit) {
    fileKey = getFile(0, myDFPlayer.readFileCounts(), "FILE-", 200);
    if (fileKey.first == MENU) {
      exit = true;
      gotoRoot();
    } else if (fileKey.first == BACK) {
      exit = true;
      lcd.clear();
      printSelected();
    } else if (fileKey.first == ENT) {
      myDFPlayer.play(fileKey.second);
      Serial.printf("Playing File=%d\n", fileKey.second);
    }
    delay(300);
  }
}

/*
Returns time in hhmm format
Or
Returns -1,abortKEY if pressed.
*/
Pair getDateTime(String msg) {
  // cnt=which value out of hh:mm
  // cursorPos= position of cursor.
  int hour = 0, min = 0, cnt = 0, cursorPos = 0, tmp = 0;
  int time_row = 1;
  int timeBuf[4] = {0, 0, 0, 0};
  int actionKey = -1;
  int keyPressed = 0;
  Pair returnKey;
  returnKey.first = returnKey.second = -1;
  bool exit = false;
  bool setTime = false;

  String invalidMsg = "Invalid Key";
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.blink_on();
  lcd.clear();
  lcd.print(msg);
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
            returnKey.first = hour;
            returnKey.second = min;
            exit = true;
            break;
          case MENU:
            // TODO: experimental. use flag for menu pressed, exit while() then
            // run cleanup if this does not works.
            returnKey.first = MENU;
            returnKey.second = -1; // key to send in the end of func
            exit = true;           // exit this loop
            break;
          case BACK:
            // currentItem->stays same. just exit and show printSelected()
            returnKey.first = BACK;
            returnKey.second = -1;
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
              lcd.setCursor(cursorPos, time_row);
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
                lcd.setCursor(cursorPos, time_row);
              }
            }
            break;
          }
          actionKey = -1;
        }
      }
    }
  }
  lcd.blink_off();
  lcd.clear();
  xSemaphoreGive(lcdMutex);
  return returnKey;
}
void handleSetDateTime() {
  RtcDateTime now;
  Pair timeKey = getDateTime("Set Time");

  if (timeKey.first == MENU) {
    gotoRoot();
  } else if (timeKey.first == BACK) {
    printSelected();
    return;
  } else {
    // got time;
    now = rtc.GetDateTime();
    RtcDateTime updated(now.Year(), now.Month(), now.Day(), timeKey.first,
                        timeKey.second, 0);
    rtc.SetDateTime(updated);
    Serial.println("Time Updated");
    Serial.printf("%d:%d\n", timeKey.first, timeKey.second);
    printSelected();
  }
}
void handleProgSched() {
  bool exit = false;
  Pair progKey;
  while (!exit) {
    progKey = getFile(0, PROGSCHEDSIZE - 1, "P-", 200);
    if (progKey.first == MENU) {
      gotoRoot();
      return;
    } else if (progKey.first == BACK) {
      clearLcd(); // thread safe.
      printSelected();
      return;
    } else if (progKey.first == ENT) {
      // selected a schedule to program.
      int selectedSched = progKey.second;
      // setting id = index
      schedules[selectedSched].id = selectedSched;
      // get number of bells for the selected schedule.
      Pair bellCountKey = getFile(1, BELLCOUNTMAX, "Bells=", 200);
      if (bellCountKey.first == MENU) {
        gotoRoot();
        return;
      } else if (bellCountKey.first == BACK) {
        clearLcd();
        printSelected();
        return;
      } else if (bellCountKey.first == ENT) {
        schedules[selectedSched].bellCount = bellCountKey.second;
        // got the bell count here, alloc memory and iterate this many times to
        // get time,file for each bell.
        schedules[selectedSched].bells =
            (Bell *)(calloc(bellCountKey.second, sizeof(Bell)));
        Bell *currBellPtr = schedules[selectedSched].bells;
        if (currBellPtr == NULL) {
          Serial.printf("Allocating Mem Failed for Sched=%d\n", selectedSched);
          return;
        }
        int currBellCnt = 1; // the bell which we are processing.
        while (currBellCnt <= bellCountKey.second) {
          Serial.printf("Processing Sched=%d, Bell=%d\n", selectedSched,
                        currBellCnt);
          Pair timeKey, fileKey;
          String msg = "Set time for Bell=" + String(currBellCnt);
          timeKey = getDateTime(msg);
          if (timeKey.first == MENU) {
            gotoRoot();
            return;
          } else if (timeKey.first == BACK) {
            clearLcd();
            printSelected();
            return;
          } else {
            // got time.
            currBellPtr[currBellCnt - 1].hour = timeKey.first;
            currBellPtr[currBellCnt - 1].min = timeKey.second;
            Serial.printf("Set %d:%d Sched=%d Bell=%d\n", timeKey.first,
                          timeKey.second, selectedSched, currBellCnt);
          }

          myDFPlayer.readFileCounts();
          // TODO: fix have to call readFileCounts twice to get true value. why?
          fileKey = getFile(0, myDFPlayer.readFileCounts(), "File-", 200);
          if (fileKey.first == MENU) {
            gotoRoot();
            return;
          } else if (fileKey.first == BACK) {
            clearLcd();
            printSelected();
            return;
          } else if (fileKey.first == ENT) {
            currBellPtr[currBellCnt - 1].file = fileKey.second;
            Serial.printf("Set File=%d, Sched=%d, Bell=%d\n", fileKey.second,
                          selectedSched, currBellCnt);
          }
          currBellCnt++;
          clearLcd();
        }
        Serial.printf("Completed Sched=%d\n", selectedSched);
        ProgSched tmp = schedules[selectedSched];
        Serial.printf(
            "Schedule=> Id=%d, BellCount=%d, FirstBell=%d:%d FirstFile=%d\n",
            tmp.id, tmp.bellCount, tmp.bells[0].hour, tmp.bells[0].min,
            tmp.bells[0].file);
        // TODO: store sched in eeprom here.
        // we have to store the data in dynamically allocated Bell too in
        // eeprom.
        String key = "p" + String(selectedSched);
        Serial.printf("Key=%s\n", key.c_str());
        void *value = (void *)(&(schedules[selectedSched]));
        int len = pref.putBytes(key.c_str(), value, sizeof(ProgSched));
        Serial.printf("Stored %d Bytes for %d\n", len, selectedSched);
        // storing bell array.
        String key2 = "pb" + String(selectedSched);
        void *value2 = (void *)(tmp.bells);
        int len2 =
            pref.putBytes(key2.c_str(), value2, sizeof(Bell) * tmp.bellCount);
        Serial.printf("Stored Bells, %dbytes\n", len2);
      }
    }
    delay(100);
  }
  clearLcd();
  printSelected();
}

/*--------------------------Tasks-----------------------*/
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
                Serial.println("Entering Home");
                handleHome();
                Serial.println("Exiting Home");
                break;
              case mnuCmdProgSched:
                Serial.println("ProgSched Entered");
                handleProgSched();
                Serial.println("ProgSched Exited");
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
    // after detecting key
    // check every 30 seconds for time.
  }
  // TODO: why this?
  vTaskDelete(NULL);
}

void alarmTask(void *pvParameters) {
  RtcDateTime now;
  int prevAlarmCheck = 0;
  while (1) {
    if (schedFoundEeprom) {
      Serial.printf("Checking for Schedule=%d, Bell=%d\n", activeSchedPtr->id,
                    activeBellCnt);
      now = rtc.GetDateTime();
      Serial.printf("%d:%d\n", now.Hour(), now.Minute());
      prevAlarmCheck = millis();
      if (activeSchedPtr->bells[activeBellCnt].hour == now.Hour() &&
          activeSchedPtr->bells[activeBellCnt].min == now.Minute()) {
        myDFPlayer.play(activeSchedPtr->bells[activeBellCnt].file);
        Serial.printf("Playing Bell=%d File=%d\n", activeBellCnt,
                      activeSchedPtr->bells[activeBellCnt].file);
        activeBellCnt++;
      }
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}
/*--------------------freertos tasks--------------------*/

void setup() {
  Serial.begin(115200);
  Serial.println("Starting");
  /*-----------Preferences---------*/
  pref.begin("alarm");

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
  /*-------------eeprom--------------*/
  String key = "p1"; // treating 1st schedule as active.
  int len = pref.getBytes(key.c_str(), &activeSchedule,
                          pref.getBytesLength(key.c_str()));
  activeSchedPtr = &activeSchedule;
  // retrieving bells
  key = "pb1";
  len = pref.getBytes(key.c_str(), &bellArr,
                      sizeof(Bell) * activeSchedule.bellCount);
  if (len == 0) {
    Serial.println("No Schedule Stored");
  } else {
    Serial.printf("Retrieved Bells &dBytes\n", len);
    schedFoundEeprom = true;
    activeBellCnt = 0;
    activeBellCount = activeSchedPtr->bellCount;
    activeSchedule.bells = &bellArr[0];
    Serial.println("Schedule Retrieved");
    Serial.printf(
        "Schedule=> Id=%d, BellCount=%d, FirstBell=%d:%d FirstFile=%d\n",
        activeSchedule.id, activeSchedule.bellCount,
        activeSchedule.bells[0].hour, activeSchedule.bells[0].min,
        activeSchedule.bells[0].file);
  }

  /*--------Alarm Task---------------*/
  xTaskCreate(alarmTask, "alarm", 2048 , NULL, 2, NULL);
  /*---------homescreen by default---------*/
  handleHome();
  /*-----------Keypress Task-----------------*/
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
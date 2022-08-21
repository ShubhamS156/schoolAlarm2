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
#define PROGSCHEDSIZE 50
#define BELLCOUNTMAX 15
#define MAXHOLIDAYS 30
#define SATCHKKEY "satchk"
/*--------------structs----------------*/
typedef struct
{
  uint8_t hour;
  uint8_t min;
  uint8_t file;
} Bell __packed;

typedef struct
{
  uint8_t id;
  uint8_t bellCount = 0;
  Bell *bells;
} ProgSched __packed;

typedef struct
{
  uint8_t scheds[7] = {0};
} ModeSched __packed;

typedef struct
{
  uint8_t year;
  uint8_t month;
  uint8_t day;
} HolidayDate __packed;

typedef struct
{
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

enum Modes
{
  SUMMER = 0,
  WINTER,
  EXAM,
  UNDEFINED
};
enum Days
{
  MONDAY,
  TUESDAY,
  WEDNESDAY,
  THURSDAY,
  FRIDAY,
  SATURDAY,
  SUNDAY
};
static SemaphoreHandle_t lcdMutex;
//-1 = undefined, 0=homescreen, 1=menuscreen
static int currentItem = -1; // currently selected menuItem
static uint8_t currentMode = UNDEFINED;
static int activeSchedIdx = -1; // no active yet.
ProgSched schedules[PROGSCHEDSIZE];
Bell bellArr[BELLCOUNTMAX];
ModeSched summerSched;
ModeSched winterSched;
ModeSched examSched;
ProgSched *activeSchedPtr = NULL;
ProgSched activeSchedule;
static bool checkSecondSat = true;
static bool schedFoundEeprom = false;
static bool todayHoliday = false;
int activeBellCount = 0;
int activeBellCnt = 0;

/*----------HW Pins-----------*/
int touchLedPin = 2;
int relayAmpPin = 12;
int relayMicPin = 14;
int errBuzPin = 15;
int mp3busyPin = 34;
int micActivatePin = 13;

/*------------util funcs-----------------*/
void createCustomCharacters()
{
  lcd.createChar(0, verticalLine);
  lcd.createChar(1, char1);
  lcd.createChar(2, char2);
  lcd.createChar(3, char3);
  lcd.createChar(4, char4);
  lcd.createChar(5, arrow);
}
void printSelected()
{
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  const MenuItem *curr = obj.getMenuItem();

  // let caller handle clearing the screen.
  //  if (curr != sampleMenu_Root) {
  //    lcd.clear();
  //  }

  int counter = 1;
  lcd.setCursor(0, 0);
  lcd.clear();
  if (obj.getCurrentItemIndex() < 4)
  {
    // print top 4 items
    for (int i = 0; i < 4; i++)
    {
      lcd.print(curr[i].name);
      Serial.println(curr[i].name);
      lcd.setCursor(0, counter++);
    }
    lcd.setCursor(0, obj.getCurrentItemIndex());
    Serial.printf("Resetting cursor to %d\n", obj.getCurrentItemIndex());
  }
  else
  {
    // print the selected and above 3
    for (int i = obj.getCurrentItemIndex() - 3; i <= obj.getCurrentItemIndex();
         i++)
    {
      lcd.print(curr[i].name);
      lcd.setCursor(0, counter++);
    }
    lcd.setCursor(0, 3);
  }
  xSemaphoreGive(lcdMutex);
  lcd.write(5); // TODO: enable this when custom chars implemented.
}
void printFrame()
{
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
void printTime(RtcDateTime &tm)
{
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
  if (tm.Minute() < 10)
  {
    minutes = "0" + String(tm.Minute());
    lcd.print(minutes);
  }
  else
  {
    lcd.print(tm.Minute());
  }
  lcd.print(":");
  if (tm.Second() < 10)
  {
    seconds = "0" + String(tm.Second());
    lcd.print(seconds);
  }
  else
  {
    lcd.print(tm.Second());
  }
}
void drawHome(RtcDateTime &dt)
{
  lcd.blink_off();
  lcd.noCursor();
  printTime(dt);
  lcd.setCursor(13, 1);
  lcd.print("Mode");
  lcd.setCursor(14, 2);
  if (currentMode == SUMMER)
  {
    lcd.print("Sum");
  }
  else if (currentMode == WINTER)
  {
    lcd.print("Win");
  }
  else if (currentMode == EXAM)
  {
    lcd.print("Exm");
  }
  else if (currentMode == UNDEFINED)
  {
    lcd.print("N/a");
  }
}
void clearLcd()
{
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.clear();
  xSemaphoreGive(lcdMutex);
}
void gotoRoot()
{
  obj.reset();
  currentItem = mnuCmdHome;
  clearLcd();
  printSelected();
}
int parseNumKeys(int actionKey)
{
  switch (actionKey)
  {
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
void keyChange()
{
  // A key press changed
  ttp229.keyChange = true;
}

/*------------tasks----------------------*/

void handleHome()
{
  RtcDateTime now;
  bool exit = false;
  int actionKey = -1;
  int keyPressed = 0;
  char buf[100];
  clearLcd();
  while (!exit)
  {
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    printFrame();
    now = rtc.GetDateTime();
    drawHome(now);
    xSemaphoreGive(lcdMutex);
    if (ttp229.keyChange)
    {
      keyPressed = ttp229.GetKey16();
      if (keyPressed != RELEASE)
      {
        actionKey = keyPressed;
        obj.getCurrentItemName(buf);
        Serial.printf("Screen:%s\n", buf);
        Serial.printf("actionKey=%d\n", actionKey);
      }
      else
      {
        if (actionKey != -1)
        {
          switch (actionKey)
          {
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
Pair getFile(int min, int fileCount, String msg, int delayMs)
{
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
  if (fileCount > min)
  {
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    lcd.clear();
    lcd.blink_off();
    lcd.setCursor(0, 0);
    lcd.print(msg + String(min));
    while (!exit)
    {
      if (ttp229.keyChange)
      {
        keyPressed = ttp229.GetKey16();
        if (keyPressed != RELEASE)
        {
          actionKey = keyPressed;
          Serial.printf("actionKey=%d\n", actionKey);
        }
        else
        {
          if (actionKey != -1)
          {
            switch (actionKey)
            {
            case UP:
              --cnt;
              if (cnt < min)
              {
                cnt = min;
              }
              break;
            case DOWN:
              ++cnt;
              if (cnt > fileCount)
              {
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
            if (cnt < 10)
            {
              counter = "0" + String(cnt);
            }
            else
            {
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
  }
  else
  {
    Serial.println("Invalid File Count");
  }
  return returnKey;
}

void handleManualMode()
{
  bool exit = false;
  Pair fileKey;
  while (!exit)
  {
    fileKey = getFile(0, myDFPlayer.readFileCounts(), "FILE-", 200);
    if (fileKey.first == MENU)
    {
      exit = true;
      gotoRoot();
    }
    else if (fileKey.first == BACK)
    {
      exit = true;
      lcd.clear();
      printSelected();
    }
    else if (fileKey.first == ENT)
    {
      myDFPlayer.play(fileKey.second);
      Serial.printf("Playing File=%d\n", fileKey.second);
      Serial.printf("mp3 pin state=%d\n",digitalRead(mp3busyPin));
    }
    delay(300);
  }
}

/*
Returns time in hhmm format
Or
Returns -1,abortKEY if pressed.
*/
Pair getDateTime(String msg)
{
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
  while (!exit)
  {
    if (ttp229.keyChange)
    {
      keyPressed = ttp229.GetKey16();
      if (keyPressed != RELEASE)
      {
        actionKey = keyPressed;
        Serial.printf("actionKey=%d\n", actionKey);
      }
      else
      {
        if (actionKey != -1)
        {
          switch (actionKey)
          {
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
            if (cnt == 0 || cursorPos == 0)
            {
              break;
            }
            --cnt;
            --cursorPos;
            timeBuf[cnt] = 0;
            if (cursorPos == 2)
            { // there is colon at 2
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
            if (cnt >= 4)
            {
              break;
            }
            tmp = parseNumKeys(actionKey);
            if (tmp == -1)
            {
              Serial.println(
                  "Invalid Number Key pressed, shouldnt be possible!!");
            }
            else
            {
              timeBuf[cnt] = tmp;
              lcd.setCursor(cursorPos,
                            time_row); // todo:check affect of removing this
                                       // ..my guess is nothing
              lcd.print(String(tmp));
              ++cnt;
              ++cursorPos;
              if (cursorPos == 2)
              {
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
bool getDate(uint8_t *buff)
{
  // get 3int: year,month,date ,store in buff.
  uint8_t YEAR = 0, MONTH = 1, DAY = 2;
  Pair yearKey = getFile(22, 99, "Year=20", 200);
  if (yearKey.first == MENU)
  {
    gotoRoot();
    return false;
  }
  else if (yearKey.first == BACK)
  {
    clearLcd();
    printSelected();
    return false;
  }
  else if (yearKey.first == ENT)
  {
    buff[YEAR] = yearKey.second;
    Serial.printf("Year=%d\n", buff[YEAR]);
  }
  else
  {
    Serial.println("Error in Retrieving Year\n");
  }

  Pair monthKey = getFile(1, 12, "Month=", 200);
  if (monthKey.first == MENU)
  {
    gotoRoot();
    return false;
  }
  else if (monthKey.first == BACK)
  {
    clearLcd();
    printSelected();
    return false;
  }
  else if (monthKey.first == ENT)
  {
    buff[MONTH] = monthKey.second;
    Serial.printf("Month=%d\n", buff[MONTH]);
  }
  else
  {
    Serial.println("Error in Retrieving Month\n");
  }

  Pair dayKey = getFile(1, 31, "Day=", 200);
  if (dayKey.first == MENU)
  {
    gotoRoot();
    return false;
  }
  else if (dayKey.first == BACK)
  {
    clearLcd();
    printSelected();
    return false;
  }
  else if (dayKey.first == ENT)
  {
    buff[DAY] = dayKey.second;
    Serial.printf("Day=%d\n", buff[DAY]);
  }
  else
  {
    Serial.println("Error in Retrieving Day\n");
  }

  return true;
}
void handleSetDateTime()
{
  RtcDateTime now;
  // first get date then time.
  uint8_t YEAR = 0, MONTH = 1, DAY = 2;
  uint8_t buf[3];
  if (getDate(&buf[0]))
  {
    Serial.println("Got Date");
    now = rtc.GetDateTime();
    RtcDateTime updateDate(buf[YEAR], buf[MONTH], buf[DAY], now.Hour(), now.Minute(), now.Second());
    rtc.SetDateTime(updateDate);
    Serial.printf("RTC Date Updated to Y=20%d,M=%d,D=%d\n", buf[YEAR], buf[MONTH], buf[DAY]);
  }
  else
  {
    Serial.println("Date not updated");
  }
  Pair timeKey = getDateTime("Set Time");

  if (timeKey.first == MENU)
  {
    gotoRoot();
  }
  else if (timeKey.first == BACK)
  {
    printSelected();
    return;
  }
  else
  {
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
void handleProgSched()
{
  bool exit = false;
  Pair progKey;
  while (!exit)
  {
    progKey = getFile(0, PROGSCHEDSIZE - 1, "P-", 200);
    if (progKey.first == MENU)
    {
      gotoRoot();
      return;
    }
    else if (progKey.first == BACK)
    {
      clearLcd(); // thread safe.
      printSelected();
      return;
    }
    else if (progKey.first == ENT)
    {
      // selected a schedule to program.
      int selectedSched = progKey.second;
      // setting id = index
      schedules[selectedSched].id = selectedSched;
      // get number of bells for the selected schedule.
      Pair bellCountKey = getFile(1, BELLCOUNTMAX, "Bells=", 200);
      if (bellCountKey.first == MENU)
      {
        gotoRoot();
        return;
      }
      else if (bellCountKey.first == BACK)
      {
        clearLcd();
        printSelected();
        return;
      }
      else if (bellCountKey.first == ENT)
      {
        schedules[selectedSched].bellCount = bellCountKey.second;
        // got the bell count here, alloc memory and iterate this many times to
        // get time,file for each bell.
        schedules[selectedSched].bells =
            (Bell *)(calloc(bellCountKey.second, sizeof(Bell)));
        Bell *currBellPtr = schedules[selectedSched].bells;
        if (currBellPtr == NULL)
        {
          Serial.printf("Allocating Mem Failed for Sched=%d\n", selectedSched);
          return;
        }
        int currBellCnt = 1; // the bell which we are processing.
        while (currBellCnt <= bellCountKey.second)
        {
          Serial.printf("Processing Sched=%d, Bell=%d\n", selectedSched,
                        currBellCnt);
          Pair timeKey, fileKey;
          String msg = "Set time for Bell=" + String(currBellCnt);
          timeKey = getDateTime(msg);
          if (timeKey.first == MENU)
          {
            gotoRoot();
            return;
          }
          else if (timeKey.first == BACK)
          {
            clearLcd();
            printSelected();
            return;
          }
          else
          {
            // got time.
            currBellPtr[currBellCnt - 1].hour = timeKey.first;
            currBellPtr[currBellCnt - 1].min = timeKey.second;
            Serial.printf("Set %d:%d Sched=%d Bell=%d\n", timeKey.first,
                          timeKey.second, selectedSched, currBellCnt);
          }

          myDFPlayer.readFileCounts();
          // TODO: fix have to call readFileCounts twice to get true value. why?
          fileKey = getFile(0, myDFPlayer.readFileCounts(), "File-", 200);
          if (fileKey.first == MENU)
          {
            gotoRoot();
            return;
          }
          else if (fileKey.first == BACK)
          {
            clearLcd();
            printSelected();
            return;
          }
          else if (fileKey.first == ENT)
          {
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

void daySchedHandler(int mode, int day)
{
  Pair dayKey = getFile(0, PROGSCHEDSIZE - 1, "P-", 200);
  if (dayKey.first == MENU)
  {
    gotoRoot();
  }
  else if (dayKey.first == BACK)
  {
    lcd.clear();
    printSelected();
  }
  else if (dayKey.first == ENT)
  {
    if (mode == SUMMER)
    {
      summerSched.scheds[day] = dayKey.second;
      Serial.printf("Mode=%d Day=%d, Schedule=%d\n", mode, day, summerSched.scheds[day]);
      String key = "modeSum";
      void *value = (void *)(&summerSched);
      int len = pref.putBytes(key.c_str(), value, sizeof(ModeSched));
      Serial.printf("Stored %dBytes\n", len);
    }
    else if (mode == WINTER)
    {
      winterSched.scheds[day] = dayKey.second;
      Serial.printf("Mode=%d Day=%d, Schedule=%d\n", mode, day, winterSched.scheds[day]);
      String key = "modeWin";
      void *value = (void *)(&winterSched);
      int len = pref.putBytes(key.c_str(), value, sizeof(ModeSched));
      Serial.printf("Stored %dBytes\n", len);
    }
    else if (mode == EXAM)
    {
      examSched.scheds[day] = dayKey.second;
      Serial.printf("Mode=%d Day=%d, Schedule=%d\n", mode, day, examSched.scheds[day]);
      String key = "modeExam";
      void *value = (void *)(&examSched);
      int len = pref.putBytes(key.c_str(), value, sizeof(ModeSched));
      Serial.printf("Stored %dBytes\n", len);
    }
    else
    {
      Serial.println("Invalid Mode");
    }
    clearLcd();
    printSelected();
  }
}
/* In: buffer to store year month and date.
  Out: True if getting values successfule
        ELSE
        False
*/
void handleProgHoliday()
{
  // we have to get 3 numbers using getFile()
  // store them in eeprom using String(month)+String(date) as key.
  // in setup try to get an entry with today's String(month)+String(date) as key.
  // if retrieved today is holiday.
  uint8_t YEAR = 0, MONTH = 1, DAY = 2;
  uint8_t buf[3];
  if (getDate(&buf[0]))
  {
    Serial.println("Got Date Successfully");
    String key = String(buf[MONTH]) + String(buf[DAY]);
    Serial.printf("Key=%s\n", key);
    void *value = (void *)(&buf);
    int len = pref.putBytes(key.c_str(), value, sizeof(buf));
    Serial.printf("Stored %dbytes\n", len);
  }
  else
  {
    Serial.println("Failed to Program Holiday");
  }
  clearLcd();
  printSelected();
}
/*--------------------------Tasks-----------------------*/
void keyPressTask(void *pvParameters)
{
  printSelected();
  Serial.println("Starting Key Press Detection");
  int actionKey = -1;
  int keyPressed = 0;
  int len = 0;
  int mp3State;
  void *value;
  while (1)
  {
    if (ttp229.keyChange)
    {
      keyPressed = ttp229.GetKey16();
      if (keyPressed != RELEASE)
      {
        digitalWrite(touchLedPin, HIGH);
        actionKey = keyPressed;
        Serial.printf("actionKey=%d\n", actionKey);
      }
      else
      {
        digitalWrite(touchLedPin,LOW);
        if (actionKey != -1)
        {
          switch (actionKey)
          {
          case UP:
            if (obj.moveToPreviousItem())
            {
              currentItem = obj.getCurrentItemCmdId();
              printSelected();
              Serial.println("Moving UP");
            }
            break;
          case DOWN:
            if (obj.moveToNextItem())
            {
              currentItem = obj.getCurrentItemCmdId();
              printSelected();
              Serial.println("Moving DOWN");
            }
            break;
          case ENT:
            if (obj.currentItemHasChildren())
            {
              obj.descendToChildMenu();
              currentItem = obj.getCurrentItemCmdId();
              lcd.clear();
              printSelected();
            }
            else
            {
              switch (currentItem)
              {
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
                value = (void *)(&currentMode);
                len = pref.putBytes("mode", value, sizeof(uint8_t));
                Serial.printf("Stored %dBytes\n", len);
                Serial.println("Mode=SUMMER");
                handleHome();
                break;
              case mnuCmdWinter:
                currentMode = WINTER;
                value = (void *)(&currentMode);
                len = pref.putBytes("mode", value, sizeof(uint8_t));
                Serial.printf("Stored %dBytes\n", len);
                Serial.println("Mode=WINTER");
                handleHome();
                break;
              case mnuCmdExam:
                currentMode = EXAM;
                value = (void *)(&currentMode);
                len = pref.putBytes("mode", value, sizeof(uint8_t));
                Serial.printf("Stored %dBytes\n", len);
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
              case mnuCmdSumMon:
                Serial.println("Summer ");
                daySchedHandler(SUMMER, MONDAY);
                Serial.println("Exit");
                break;
              case mnuCmdSumTue:
                Serial.println("Summer ");
                daySchedHandler(SUMMER, TUESDAY);
                Serial.println("Exit");
                break;
              case mnuCmdSumWed:
                Serial.println("Summer ");
                daySchedHandler(SUMMER, WEDNESDAY);
                Serial.println("Exit");
                break;
              case mnuCmdSumThu:
                Serial.println("Summer ");
                daySchedHandler(SUMMER, THURSDAY);
                Serial.println("Exit");
                break;
              case mnuCmdSumFri:
                Serial.println("Summer ");
                daySchedHandler(SUMMER, FRIDAY);
                Serial.println("Exit");
                break;
              case mnuCmdSumSat:
                Serial.println("Summer ");
                daySchedHandler(SUMMER, SATURDAY);
                Serial.println("Exit");
                break;
              case mnuCmdSumSun:
                Serial.println("Summer ");
                daySchedHandler(SUMMER, SUNDAY);
                Serial.println("Exit");
                break;
              case mnuCmdWinMon:
                Serial.println("Winter");
                daySchedHandler(WINTER, MONDAY);
                Serial.println("Exit");
                break;
              case mnuCmdWinTue:
                Serial.println("Winter");
                daySchedHandler(WINTER, TUESDAY);
                Serial.println("Exit");
                break;
              case mnuCmdWinWed:
                Serial.println("Winter");
                daySchedHandler(WINTER, WEDNESDAY);
                Serial.println("Exit");
                break;
              case mnuCmdWinThu:
                Serial.println("Winter");
                daySchedHandler(WINTER, THURSDAY);
                Serial.println("Exit");
                break;
              case mnuCmdWinFri:
                Serial.println("Winter");
                daySchedHandler(WINTER, FRIDAY);
                Serial.println("Exit");
                break;
              case mnuCmdWinSat:
                Serial.println("Winter");
                daySchedHandler(WINTER, SATURDAY);
                Serial.println("Exit");
                break;
              case mnuCmdWinSun:
                Serial.println("Winter");
                daySchedHandler(WINTER, SUNDAY);
                Serial.println("Exit");
                break;
              case mnuCmdExMon:
                Serial.println("Exam");
                daySchedHandler(EXAM, MONDAY);
                Serial.println("Exit");
                break;
              case mnuCmdExTue:
                Serial.println("Exam");
                daySchedHandler(EXAM, TUESDAY);
                Serial.println("Exit");
                break;
              case mnuCmdExWed:
                Serial.println("Exam");
                daySchedHandler(EXAM, WEDNESDAY);
                Serial.println("Exit");
                break;
              case mnuCmdExThu:
                Serial.println("Exam");
                daySchedHandler(EXAM, THURSDAY);
                Serial.println("Exit");
                break;
              case mnuCmdExFri:
                Serial.println("Exam");
                daySchedHandler(EXAM, FRIDAY);
                Serial.println("Exit");
                break;
              case mnuCmdExSat:
                Serial.println("Exam");
                daySchedHandler(EXAM, SATURDAY);
                Serial.println("Exit");
                break;
              case mnuCmdExSun:
                Serial.println("Exam");
                daySchedHandler(EXAM, SUNDAY);
                Serial.println("Exit");
                break;
                // case mnuCmdOff:
                //   checkSecondSat = false;
                //   pref.putBool(SATCHKKEY, checkSecondSat);
                //   Serial.println("SecondSat Check Off");
                //   clearLcd();
                //   lcd.print("Second Sat OFF");
                //   clearLcd();
                //   printSelected();
                //   Serial.println("Exit");
                //   break;
              case mnuCmdSecondSat:
                checkSecondSat = !checkSecondSat;
                pref.putBool(SATCHKKEY, checkSecondSat);
                Serial.printf("SecondSat=%d\n", checkSecondSat);
                clearLcd();
                xSemaphoreTake(lcdMutex, portMAX_DELAY);
                if (checkSecondSat)
                {
                  lcd.print("Second Sat ON");
                }
                else
                {
                  lcd.print("Second Sat OFF");
                }
                vTaskDelay(500 / portTICK_PERIOD_MS);
                xSemaphoreGive(lcdMutex);
                clearLcd();
                printSelected();
                Serial.println("Exit");
                break;
              case mnuCmdCalendarHoliday:
                Serial.println("Entering Programming Holiday");
                handleProgHoliday();
                Serial.println("Exiting");
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
            if (obj.currentMenuHasParent())
            {
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
      // hw checks
      mp3State = digitalRead(mp3busyPin);
      if (mp3State == HIGH)
      {
        digitalWrite(relayAmpPin, HIGH);
        digitalWrite(relayMicPin, HIGH);
        digitalWrite(micActivatePin, HIGH);
        digitalWrite(errBuzPin, LOW);
      }
      else
      {
        digitalWrite(relayAmpPin, HIGH);
        digitalWrite(relayMicPin, LOW);
        digitalWrite(errBuzPin, HIGH);
        delay(500);
        digitalWrite(errBuzPin, LOW);
      }
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    // after detecting key
    // check every 30 seconds for time.
  }
  // TODO: why this?
  vTaskDelete(NULL);
}

void alarmTask(void *pvParameters)
{
  RtcDateTime now;
  int prevAlarmCheck = 0;
  while (1)
  {
    now = rtc.GetDateTime();
    // if midnight restart.

    if (schedFoundEeprom)
    {
      // checking for 2nd saturday.
      if (checkSecondSat && now.Day() > 7 && now.Day() <= 14 && now.DayOfWeek() == 6)
      {
        // DayOfWeek = 6 if Saturday.
        Serial.println("Second Saturday");
        vTaskDelay(60000 / portTICK_PERIOD_MS);
      }
      else if (todayHoliday)
      {
        // assigned holidays.
        Serial.println("Not checking today Holiday");
        vTaskDelay(60000 / portTICK_PERIOD_MS);
      }
      else
      {
        Serial.printf("Checking for Schedule=%d, Bell=%d\n", activeSchedPtr->id,
                      activeBellCnt);
        Serial.printf("%d:%d\n", now.Hour(), now.Minute());
        prevAlarmCheck = millis();
        if (activeSchedPtr->bells[activeBellCnt].hour == now.Hour() &&
            activeSchedPtr->bells[activeBellCnt].min == now.Minute())
        {
          myDFPlayer.play(activeSchedPtr->bells[activeBellCnt].file);
          Serial.printf("Playing Bell=%d File=%d\n", activeBellCnt,
                        activeSchedPtr->bells[activeBellCnt].file);
          activeBellCnt++;
        }
      }
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}
/*--------------------freertos tasks--------------------*/

void setup()
{
  Serial.begin(115200);
  Serial.println("Starting");

  /*-----------Hw Pins------------*/
  pinMode(touchLedPin, OUTPUT);
  pinMode(relayAmpPin, OUTPUT);
  pinMode(relayMicPin, OUTPUT);
  pinMode(micActivatePin, OUTPUT);
  pinMode(errBuzPin, OUTPUT);
  pinMode(mp3busyPin, INPUT);
  digitalWrite(relayAmpPin,LOW);
  /*-----------Preferences---------*/
  pref.begin("alarm");

  /*------------LCD-----------------*/
  lcd.begin();
  lcd.backlight();
  lcdMutex = xSemaphoreCreateMutex();
  if (lcdMutex == NULL)
  {
    Serial.println("Could not create mutex for lcdMutex");
    // TODO: how to heal this if occurs?
  }
  else
  {
    Serial.println("Mutex Created");
  }
  createCustomCharacters();

  /*-----------TTP229--------------*/
  ttp229.begin(TTP229_SCL, TTP229_SDO);
  attachInterrupt(digitalPinToInterrupt(TTP229_SDO), keyChange, RISING);
  /*-------------df player------------*/
  mySoftwareSerial.begin(9600, SERIAL_8N1, 16, 17);
  if (!myDFPlayer.begin(mySoftwareSerial))
  {
    Serial.println("DF Module Error");
    while (1)
    {
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
  if (!rtc.IsDateTimeValid())
  {
    if (rtc.LastError() != 0)
    {
      Serial.print("RTC communicatins error= ");
      Serial.println(rtc.LastError());
    }
    else
    {
      Serial.println("RTC lost confidence in date and time");
      rtc.SetDateTime(compiled);
    }
  }
  if (!rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    rtc.SetIsRunning(true);
  }

  RtcDateTime now = rtc.GetDateTime();
  if (now < compiled)
  {
    Serial.println("rtc is older than compile time. Updating");
    // rtc.SetDateTime(compiled); //NOTE: not updating date time
  }
  else
  {
    Serial.println("rtc time same or newer than compile time");
  }
  rtc.Enable32kHzPin(false);
  rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
  /*-------------eeprom--------------*/
  String key = "mode";
  int modeSchedIdx = 0;
  int day = (now.DayOfWeek() - 1) % 7; // func returns 0 for sunday we have 0 for monday.
  Serial.printf("Day=%d\n", day);
  int len = pref.getBytes(key.c_str(), &currentMode, sizeof(uint8_t));
  Serial.printf("Got %dbytes\n", len);
  Serial.printf("Active Mode = %d\n", currentMode);
  if (currentMode == SUMMER)
  {
    key = "modeSum";
    len = pref.getBytes(key.c_str(), &summerSched, sizeof(ModeSched));
    Serial.printf("Got %dbytes\n", len);
    modeSchedIdx = summerSched.scheds[day];
    Serial.printf("modeSchedIdx=%d\n", modeSchedIdx);
  }
  else if (currentMode == WINTER)
  {
    key = "modeWin";
    len = pref.getBytes(key.c_str(), &winterSched, sizeof(ModeSched));
    Serial.printf("Got %dbytes\n", len);
    modeSchedIdx = winterSched.scheds[day];
  }
  else if (currentMode == EXAM)
  {
    key = "modeExam";
    len = pref.getBytes(key.c_str(), &examSched, sizeof(ModeSched));
    Serial.printf("Got %dbytes\n", len);
    modeSchedIdx = examSched.scheds[day];
  }
  else
  {
    Serial.println("Invalid Mode");
  }
  // got index of schedule to activate today.
  Serial.printf("modeSchedIdx=%d\n", modeSchedIdx);
  key = "p" + String(modeSchedIdx);
  Serial.printf("Schedule key=%s\n", key.c_str());
  len = pref.getBytes(key.c_str(), &activeSchedule, sizeof(ProgSched));
  activeSchedPtr = &activeSchedule;
  // retrieving bells
  key = "pb" + String(modeSchedIdx);
  Serial.printf("Bell key=%s\n", key.c_str());
  len = pref.getBytes(key.c_str(), &bellArr,
                      sizeof(Bell) * activeSchedule.bellCount);
  if (len == 0)
  {
    Serial.println("No Schedule Stored");
  }
  else
  {
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

  /*--------Retrieve Second Sat Bool----------*/
  checkSecondSat = pref.getBool(SATCHKKEY, true);
  Serial.printf("Check Second Sat=%d\n", checkSecondSat);

  /*---------Retrieve Holiday using Today's key if possible----------*/
  int month = now.Month();
  day = now.Day();
  key = "";
  key = String(month) + String(day);
  uint8_t buf[3];
  len = pref.getBytes(key.c_str(), &buf, sizeof(buf));
  if (len > 0)
  {
    Serial.println("Today is Holiday");
    todayHoliday = true;
    if (pref.remove(key.c_str()))
    {
      Serial.println("Removed Today from Holiday List");
    }
  }
  else
  {
    Serial.println("Today is not Holiday");
  }
  /*--------Alarm Task---------------*/
  xTaskCreate(alarmTask, "alarm", 2048, NULL, 2, NULL);
  /*---------homescreen by default---------*/
  handleHome();
  /*-----------Keypress Task-----------------*/
  xTaskCreate(keyPressTask, "keyPress", 4096, NULL, 3, NULL);
}

void loop()
{
  delay(10000);
}

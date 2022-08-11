#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <MenuData.h>
#include <MenuManager.h>
#include <TTP229.h>
#include <Wire.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Wire.h>

#define TTP229_SDO 25
#define TTP229_SCL 26
#define countof(a) (sizeof(a)/sizeof(a[0]))
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
LiquidCrystal_I2C lcd(0x27,20,4);
TTP229 ttp229;
MenuManager obj(sampleMenu_Root,menuCount(sampleMenu_Root));

/*--------------var init---------------*/
static SemaphoreHandle_t lcdMutex;
//-1 = undefined, 0=homescreen, 1=menuscreen
static int currentItem = -1; 
static int loopFlag = -1; //to indicate which item is selected that should be looped.

/*------------util funcs-----------------*/
void printSelected() {
  xSemaphoreTake(lcdMutex,portMAX_DELAY);
  const MenuItem *curr = obj.getMenuItem();

//let caller handle clearing the screen.
  // if (curr != sampleMenu_Root) {
  //   lcd.clear();
  // }

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
  //lcd.write(5); //TODO: enable this when custom chars implemented.
}
void keyChange() {
  // A key press changed
  ttp229.keyChange = true;
}

/*------------tasks----------------------*/
void keyPressTask(void *pvParameters){
  Serial.println("Starting KeyPress Detection");
  int currId;
  int actionKey = -1; //undefined, used to check which key is pressed.
  int keyPressed = 0; //released 
  while(1){
    if(ttp229.keyChange){
      keyPressed = ttp229.GetKey16();
      Serial.printf("key-pressed=%d",actionKey);
      if(keyPressed != RELEASE){
        actionKey = keyPressed;
      }
      else{
        if(actionKey != -1){
          /* key handling start */
          if(actionKey == UP){
            Serial.println("UP pressed");
            switch(currentItem){ //TODO: add cases to this block to add meaning to UP
              default:
                //if there is item to move to
                if(obj.moveToPreviousItem()){
                  currentItem = obj.getCurrentItemCmdId();
                  printSelected(); //print the updates
                  Serial.println("Going up");
                }
                else{
                  Serial.print("cant go up");
                }
                break;
            }
          }
          else if(actionKey == DOWN){
            switch(currentItem){ //TODO: add case here for key DOWN.
              default:
                if(obj.moveToNextItem()){
                  currentItem = obj.getCurrentItemCmdId();
                  printSelected();
                  Serial.println("Going down");
                }
                else
                {
                  Serial.println("cant go down");
                }
                break;
            }
          }
          else if(actionKey == ENT){
            switch(currentItem){
              case mnuCmdHome:
                Serial.println("entering home");
                //loopFlag = mnuCmdHome;
                break;
              case mnuCmdManual:
                lcd.clear();
                Serial.println("Entering manual mode");
                lcd.print("entered manual mode");
                break;
              case mnuCmdModeSelect:
                Serial.print("entered mode selection");
                lcd.clear();
                lcd.print("mode select");
                break; 
              default:
                if(obj.currentItemHasChildren()){
                  obj.descendToChildMenu();
                  lcd.clear();
                  printSelected();
                }
                break;
            }
          }
          actionKey = -1; //clear the action key.
        }
      }
    }
    vTaskDelay(300/portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}
/*--------------------freertos tasks--------------------*/


void setup() {
  Serial.begin(115200);
  Serial.println("Starting");
  lcd.begin();
  lcd.backlight();
  ttp229.begin(TTP229_SCL,TTP229_SDO);
  attachInterrupt(digitalPinToInterrupt(TTP229_SDO),keyChange,RISING);
  lcdMutex = xSemaphoreCreateMutex();
  if(lcdMutex == NULL){
    Serial.println("Could not create mutex for lcdMutex");
    //TODO: how to heal this if occurs?
  }
  else{
    Serial.println("Mutex Created");
  }
  xTaskCreate(keyPressTask,"keyPress",4096,NULL,3,NULL);
  currentItem = mnuCmdHome;
}

void loop() {
  switch(currentItem){
    case mnuCmdHome:
      //replace this by writer functions.
      xSemaphoreTake(lcdMutex,portMAX_DELAY);
      lcd.clear();
      lcd.print("home screen");
      xSemaphoreGive(lcdMutex);
      break;
    default:
      break;
  }
  delay(1000); //TODO: what effect will this delay have? delay to let other functions write?
}


// CATOS WATCH

// В КОМПЛЕКТЕ

// CAT# ENGINE v1.0
// CatOs Sleep v0.1


#define FIRMWARE_VERSION "w.0.1"

#include "Arduino.h"
#include <GyverOLED.h>
#include <GyverDS3231.h>
#include "GyverButton.h"
#include <Wire.h>
#include <GyverTimer.h>     // Либа таймера
#include <GyverDBFile.h>    // для настроек и wifi
#include <LittleFS.h>       // для хранения данных в little fs
#include <SettingsGyver.h>  // либа веб морды
#include <Random16.h>
#include "MAX17043.h"
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "esp_sleep.h"
#include <vector>
#include <stack>
#include <map>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"
#include "arduino_dino.h"
#include "bitmaps.h"
#include "custom_app.h"
#define USE_NIMBLE
#include <BleMouse.h>
#include <DFRobot_BMI160.h>
BleMouse bleMouse("CatOS Mouse", "CatOS", 100);
DFRobot_BMI160 bmi160;


// ЗДЕСЬ МОЖНО НАСТРОИТЬ ПИНЫ, НАПРИМЕР ДЛЯ DIY
#ifdef CATOS_S3_BUILD
  //настройки для билда для s3
  #define PIN_LED    3    //светодиод
  #define PIN_SDA    8    //sda дисплея/rtc
  #define PIN_SCL    9    //scl дисплея/rtc
  
  //кнопки
  #define PIN_BTN_L  7
  #define PIN_BTN_OK 6
  #define PIN_BTN_R  5
  #define PIN_PWR    4
#else
  //настройки для билда для c3
  #define PIN_LED    20
  
  //кнопки
  #define PIN_BTN_L  21
  #define PIN_BTN_OK 6
  #define PIN_BTN_R  10
  #define PIN_PWR    7
#endif

//-----------------------
// объекты
Random16 rnd;                           // рандом
GyverDS3231 rtc;                        //часики
GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_I2C> oled(0x3C); //олег
GyverDBFile db(&LittleFS, "/data.db");              //файл где хранятся настройки
SettingsGyver sett("CatOS-Watch " FIRMWARE_VERSION, &db); // веб морда

bool ledState = false;

GButton left(PIN_BTN_L); 
GButton ok(PIN_BTN_OK);  
GButton right(PIN_BTN_R);
GButton PWR(PIN_PWR);



void alarm_menu();
// читалка
byte cursor = 0;            // курсор меню
byte files = 0;             // количество файлов
const int MAX_PAGE_HISTORY = 150;
long pageHistory[MAX_PAGE_HISTORY] = {0};
int currentHistoryIndex = -1;
int totalPages = 0;
//wifi
bool alert_f;               // показ ошибки в вебморде
bool wifiConnected = false; // для wifi морды
// для показа времени
const char* months[] = {
  "ЯНВ", "ФЕВ", "МАР", "АПР", "МАЙ", "ИЮН",
  "ИЮЛ", "АВГ", "СЕН", "ОКТ", "НОЯ", "ДЕК"
};
struct AlarmConfig {
  uint8_t hour;
  uint8_t minute;
  uint8_t day;
  uint8_t month;
  bool enabled;
  bool triggered;
  uint32_t alarmUnixTime;
};
bool dotsVisible = true;
unsigned long lastBlink = 0;
unsigned long lastUpdate = 0;
AlarmConfig alarmConfig = {0, 0, 1, 1, false, false};
bool alarmActive = false;  //флаг состояния будильника
bool alarmRinging = false; //флаг срабатывания будильника
uint8_t watchfaceStyle;
bool isSleeping = false;
uint32_t sleepUnixStart = 0;  //время засыпания в unix формате
// Для CatOsGotchi
struct PetState {
  String name;         // имя
  int hunger;          // уровень голода
  int happiness;       // уровень счастья
  uint32_t lastUpdate; // время последнего обновления
  uint32_t lastVisit;  // время последнего посещения
};

PetState catOsGotchi = {"CatOsGotchi", 80, 80, 0, 0}; // начальное состояние

const uint8_t* catBitmaps[] = {
  normal_cat_44x53,
  angry_cat_44x53, 
  crying_cat_44x53,
  very_angry_cat_44x53,
  sad_cat_44x53
};
// --------------------------------
DB_KEYS(
  kk,
  OLED_BRIGHTNESS,
  AP_SSID,
  AP_PASS,
  wifi_enabled,
  wifi_ssid,
  wifi_pass,
  apply,
  time_sync_enabled,
  ALARM_HOUR,
  ALARM_MINUTE,
  ALARM_DAY,
  ALARM_MONTH,
  ALARM_ENABLED,
  WATCHFACE_STYLE,
  first_start,
  PET_NAME,
  PET_HUNGER,
  PET_HAPPINESS,
  PET_LAST_UPDATE,
  PET_LAST_VISIT
);
void reset_buttons() {
  ok.resetStates();
  left.resetStates();
  right.resetStates();
}

void buttons_tick() {
  right.tick();
  left.tick();
  ok.tick();
}

void exit() {
  reset_buttons();
  PWR.resetStates();
}

void settings_menu() {
// TODO: НАКОНЕЦ СДЕЛАТЬ НАСТРОЙКИ!
}
// проверка високосного года
bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}
// конвертация data в unix
uint32_t timeToUnix(const Datime& dt) {
  const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  
  uint32_t totalDays = 0;
  
  for (int year = 2000; year < dt.year; year++) {
    totalDays += isLeapYear(year) ? 366 : 365;
  }

  for (int month = 1; month < dt.month; month++) {
    totalDays += daysInMonth[month - 1];
    if (month == 2 && isLeapYear(dt.year)) {
      totalDays++;
    }
  }
  
  totalDays += dt.day - 1;
  
  uint32_t totalSeconds = totalDays * 86400UL;
  totalSeconds += dt.hour * 3600UL;
  totalSeconds += dt.minute * 60UL;
  totalSeconds += dt.second;
  
  return totalSeconds;
}

void loadAlarmSettings() {
  if (db.has(kk::ALARM_HOUR)) alarmConfig.hour = db[kk::ALARM_HOUR].toInt();
  if (db.has(kk::ALARM_MINUTE)) alarmConfig.minute = db[kk::ALARM_MINUTE].toInt();
  if (db.has(kk::ALARM_DAY)) alarmConfig.day = db[kk::ALARM_DAY].toInt();
  if (db.has(kk::ALARM_MONTH)) alarmConfig.month = db[kk::ALARM_MONTH].toInt();
  if (db.has(kk::ALARM_ENABLED)) alarmConfig.enabled = db[kk::ALARM_ENABLED].toInt();
  
  alarmActive = alarmConfig.enabled;
}

void saveAlarmSettings() {
  db[kk::ALARM_HOUR] = alarmConfig.hour;
  db[kk::ALARM_MINUTE] = alarmConfig.minute;
  db[kk::ALARM_DAY] = alarmConfig.day;
  db[kk::ALARM_MONTH] = alarmConfig.month;
  db[kk::ALARM_ENABLED] = alarmConfig.enabled;
  db.update();
}


bool isAlarmTimeValid() {
  Datime now = rtc.getTime();
  uint32_t currentUnix = timeToUnix(now);
  
  Datime alarmTime;
  alarmTime.second = 0;
  alarmTime.minute = alarmConfig.minute;
  alarmTime.hour = alarmConfig.hour;
  alarmTime.day = alarmConfig.day;
  alarmTime.month = alarmConfig.month;
  alarmTime.year = now.year;
  
  uint32_t alarmUnix = timeToUnix(alarmTime);
  
  //проверяем, не прошел ли уже будильник
  return alarmUnix > currentUnix;
}
void toggleAlarm() {
  if (alarmActive) {
    alarmActive = false;
    alarmConfig.enabled = false;
  } else {
    if (isAlarmTimeValid()) {
      alarmActive = true;
      alarmConfig.enabled = true;
    }
  }
  saveAlarmSettings();
}
// TODO: draw_time
void ui_rama(const char* name, bool draw_b, bool draw_l, bool cleardisplay, bool draw_time = true) {
  if (cleardisplay) {
    oled.clear();
  }
  if (alarmActive) {
    oled.drawBitmap(120, 0, alarm_icon, 8, 8);
  }
  oled.home();
  oled.setScale(1);
  oled.print(name);
  oled.setScale(0);
  
  if (draw_l) {
    oled.line(0, 10, 127, 10);
  }
  
  oled.update();
}
void showAlarmScreen() {
  unsigned long startTime = millis();
  unsigned long lastBlink = millis();
  bool inverted = false;
  
  while (alarmRinging) {
    buttons_tick();
    
    if (millis() - lastBlink > 500) {
      inverted = !inverted;
      lastBlink = millis();
      oled.invertDisplay(inverted);
    }
    
    oled.clear();
    ui_rama("БУДИЛЬНИК", true, true, false, false);
    
    oled.setScale(2);
    oled.setCursor(15, 2);
    oled.print("Сработал!");
    
    oled.setScale(1);
    oled.setCursor(21, 5);
    oled.print("Удержите OK");
    
    oled.update();
    
    if (ok.isHold()) {
      oled.invertDisplay(false);
      alarmRinging = false;
      alarmActive = false;
      alarmConfig.enabled = false;
      saveAlarmSettings();
      break;
    }
    

    if (millis() - startTime > 60000) {
      oled.invertDisplay(false); 
      alarmRinging = false;
      alarmActive = false;
      alarmConfig.enabled = false;
      saveAlarmSettings();
      break;
    }
    
    delay(50);
  }
  

  oled.invertDisplay(false);
}
void set_alarm_time() {
  Datime now = rtc.getTime();
  uint8_t hours = alarmActive ? alarmConfig.hour : now.hour;
  uint8_t minutes = alarmActive ? alarmConfig.minute : now.minute;
  
  bool editingHours = true;
  bool needRedraw = true;
  unsigned long lastInput = millis();
  
  reset_buttons();
  
  while (true) {
    buttons_tick();
    
    if (needRedraw) {
      needRedraw = false;
      
      oled.clear();
      ui_rama("Время будильника", true, true, false);
      
      oled.setScale(2);
      oled.setCursor(25, 3);
      
      if (editingHours) oled.invertText(true);
      if (hours < 10) oled.print("0");
      oled.print(hours);
      if (editingHours) oled.invertText(false);
      
      oled.print(":");
      
      if (!editingHours) oled.invertText(true);
      if (minutes < 10) oled.print("0");
      oled.print(minutes);
      if (!editingHours) oled.invertText(false);
      
      oled.setScale(1);
      oled.setCursor(10, 6);
      oled.print("Сейчас: ");
      if (now.hour < 10) oled.print("0");
      oled.print(now.hour);
      oled.print(":");
      if (now.minute < 10) oled.print("0");
      oled.print(now.minute);
      
      oled.setCursor(0, 7);
      oled.print("OK-выбор  Удр-сохр");
      
      oled.update();
    }
    
    //бля пж не забуть опять что тут обработка
    if (left.isClick() || (left.isHold() && millis() - lastInput > 200)) {
      if (editingHours) {
        hours = (hours + 1) % 24;
      } else {
        minutes = (minutes + 1) % 60;
      }
      needRedraw = true;
      lastInput = millis();
    }
    
    if (right.isClick() || (right.isHold() && millis() - lastInput > 200)) {
      if (editingHours) {
        hours = (hours == 0) ? 23 : hours - 1;
      } else {
        minutes = (minutes == 0) ? 59 : minutes - 1;
      }
      needRedraw = true;
      lastInput = millis();
    }
    
    if (ok.isClick()) {
      editingHours = !editingHours;
      needRedraw = true;
    }
    
    if (ok.isHold()) {
      alarmConfig.hour = hours;
      alarmConfig.minute = minutes;
      saveAlarmSettings();
      alarm_menu();
      return;
    }
    
    delay(50);
  }
}

void set_alarm_date() {
  Datime now = rtc.getTime();
  uint8_t day = alarmActive ? alarmConfig.day : now.day;
  uint8_t month = alarmActive ? alarmConfig.month : now.month;
  
  bool editingDay = true;
  bool needRedraw = true;
  unsigned long lastInput = millis();
  
  reset_buttons();
  
  while (true) {
    buttons_tick();
    
    if (needRedraw) {
      needRedraw = false;
      
      oled.clear();
      ui_rama("Дата будильника", true, true, false);
      
      oled.setScale(2);
      oled.setCursor(15, 3);
      
      if (editingDay) oled.invertText(true);
      if (day < 10) oled.print("0");
      oled.print(day);
      if (editingDay) oled.invertText(false);
      
      oled.print(" ");
      
      if (!editingDay) oled.invertText(true);
      oled.print(months[month - 1]);
      if (!editingDay) oled.invertText(false);
      
      oled.setScale(1);
      oled.setCursor(10, 6);
      oled.print("Сегодня: ");
      if (now.day < 10) oled.print("0");
      oled.print(now.day);
      oled.print(" ");
      oled.print(months[now.month - 1]);
      
      oled.setCursor(0, 7);
      oled.print("OK-выбор  Удр-сохр");
      
      oled.update();
    }
    
    //ОБРАБОТКА!!!
    if (right.isClick() || (right.isHold() && millis() - lastInput > 200)) {
      if (editingDay) {
        day = (day % 31) + 1;
      } else {
        month = (month % 12) + 1;
      }
      needRedraw = true;
      lastInput = millis();
    }
    
    if (left.isClick() || (left.isHold() && millis() - lastInput > 200)) {
      if (editingDay) {
        day = (day == 1) ? 31 : day - 1;
      } else {
        month = (month == 1) ? 12 : month - 1;
      }
      needRedraw = true;
      lastInput = millis();
    }
    
    if (ok.isClick()) {
      editingDay = !editingDay;
      needRedraw = true;
    }
    
    if (ok.isHold()) {
      alarmConfig.day = day;
      alarmConfig.month = month;
      saveAlarmSettings();
      alarm_menu();
      return;
    }
    
    delay(50);
  }
}
void alarm_menu() {
  uint8_t selected = 0;
  uint8_t prevSelected = 0;
  bool needFullRedraw = true;
  bool needCursorRedraw = true;
  
  reset_buttons();
  
  while (true) {
    buttons_tick();
    
    if (needFullRedraw) {
      needFullRedraw = false;
      
      oled.clear();
      ui_rama("Будильник", true, true, false);
      oled.setCursor(2, 2);
      oled.print(" Установить время ");
      
      oled.setCursor(2, 3);
      oled.print(" Установить дату  ");
      
      oled.setCursor(2, 4);
      oled.print(" Будильник: ");
      oled.print(alarmActive ? "ВКЛ " : "ВЫКЛ");
      
      oled.setCursor(2, 5);
      oled.print(" Назад           ");
      
      oled.setCursor(0, 7);
      oled.print("Уст: ");
      if (alarmConfig.hour < 10) oled.print("0");
      oled.print(alarmConfig.hour);
      oled.print(":");
      if (alarmConfig.minute < 10) oled.print("0");
      oled.print(alarmConfig.minute);
      oled.print(" ");
      if (alarmConfig.day < 10) oled.print("0");
      oled.print(alarmConfig.day);
      oled.print(" ");
      oled.print(months[alarmConfig.month - 1]);
      
      oled.setCursor(0, 2 + selected);
      oled.print(">");
      
      oled.update();
      prevSelected = selected;
    }
    
    if (needCursorRedraw && selected != prevSelected) {
      oled.setCursor(0, 2 + prevSelected);
      oled.print(" ");
      
      oled.setCursor(0, 2 + selected);
      oled.print(">");
      
      oled.update();
      prevSelected = selected;
      needCursorRedraw = false;
    }
    
    if (right.isClick() && selected > 0) {
      selected--;
      needCursorRedraw = true;
    }
    
    if (left.isClick() && selected < 3) {
      selected++;
      needCursorRedraw = true;
    }
    
    if (ok.isClick()) {
      switch (selected) {
        case 0: 
          set_alarm_time(); 
          needFullRedraw = true;
          break;
        case 1: 
          set_alarm_date(); 
          needFullRedraw = true;
          break;
        case 2: 
          toggleAlarm();
          needFullRedraw = true;
          break;
        case 3: 
          exit();
          return;
      }
    }
    
    if (ok.isHold()) {
      exit();
      return;
    }
    
    delay(50);
  }
}

void playDinosaurGame(void) {
  right.setTimeout(160);         // Настраиваем удобные таймауты удержания
  ok.setTimeout(160);
  ok.setStepTimeout(160);

startDinoGame:                         // Начало игры
  uint8_t gameSpeed = 10;              // Скорость игры
  uint16_t score = 0;                  // Текущий счет
  uint16_t bestScore = 0;              // Рекорд
  int8_t oldEnemyPos = 128;            // Позиция старого противника (тот, что уже заходит за горизонт)
  int8_t oldEnemyType = 0;             // Тип старого противника (тот, что уже заходит за горизонт)
  int8_t newEnemyPos = 128;            // Позиция нового противника (тот, что только выходит изза горизонта)
  int8_t newEnemyType = random(0, 3);  // Тип нового противника - определяем случайно
  bool dinoStand = true;               // Динозавр стоит на земле
  bool legFlag = true;                 // Флаг переключения ног динозавра
  bool birdFlag = true;                // Флаг взмахов птицы
  int8_t dinoY = DINO_GROUND_Y;        // Позиция динозавра по вертикали (изначально на земле)
  float dinoU = 0.0;                   // Скорость динозавра (вектор направлен вниз)

  //EEPROM.get(DINO_EE_ADDR, bestScore); // Читаем рекорд из EEPROM

  while (1) {                                                   // Бесконечный цикл игры
    yield();
    right.tick();
    ok.tick();
    left.tick();                                                        // Тикаем память

    if (left.isClick()) {
      right.setTimeout(300);
      ok.setTimeout(300);
      ok.setStepTimeout(400);
      exit();
      return;
    }                                            // Клик кнопки влево мгновенно возвращает нас в игровое меню

    /* ------------------ User input ------------------ */
    if (ok.isClick() and dinoY == DINO_GROUND_Y) {                             // Клик по ОК и динозавр стоит на земле (слабый прыжок)
      dinoU = -2.8;                                                          // Прибавляем скорости по направлению вверх
      dinoY -= 4;                                                            // Подкидываем немного вверх
    } else if ((ok.isHold() or ok.isStep()) and dinoY == DINO_GROUND_Y) {     // Удержание ОК и динозавр стоит на земле (сильный прыжок)
      dinoU = -3.4;                                                          // Прибавляем скорости по направлению вверх
      dinoY -= 4;                                                            // Подкидываем немного вверх
    } else if (right.isPress()) {                                               // Нажатие ВНИЗ
      dinoU = 3.2;                                                           // Прибавляем скорости по направлению к земле
      if (dinoY >= DINO_GROUND_Y) {                                          // Если динозавр коснулся земли
        dinoY = DINO_GROUND_Y;                                               // Ставим его на землю
        dinoU = 0.0;                                                         // Обнуляем скорость
      }
    }

    if (right.isHold() and dinoY >= DINO_GROUND_Y) {                         // Удержание ВНИЗ и дино стоит на земле
      dinoStand = false;                                                     // Переходим в присяд
    } else {
      dinoStand = true;                                                      // Иначе встаем обратно
    }

    /* ------------------ Game processing ------------------ */
    static uint32_t scoreTimer = millis();                                   // Таймер подсчета очков
    if (millis() - scoreTimer >= 100) {
      scoreTimer = millis();
      score++;                                                               // Увеличиваем счет
      if (score < 1000) 
        gameSpeed = constrain(map(score, 900, 0, 4, 10), 4, 10);             // Увеличиваем скорость игры! (10 - медленно, 4 - очень быстро)
      else 
        gameSpeed = constrain(map(score%1000, 900, 0, 4, 7), 4, 7);          // Увеличиваем скорость игры! (7 - нормально, 4 - очень быстро)
    }

    static uint32_t enemyTimer = millis();                                   // Таймер кинематики противников
    if (millis() - enemyTimer >= gameSpeed) {                                // Его период уменьшается с ростом счета
      enemyTimer = millis();
      if (--newEnemyPos < 16) {                                              // Как только НОВЫЙ противник приближается к динозавру
        oldEnemyPos = newEnemyPos;                                           // Новый противник становится старым
        oldEnemyType = newEnemyType;                                         // И копирует тип нового к себе
        newEnemyPos = 128;                                                   // Между тем новый противник выходит изза горизонта
        do newEnemyType = random(0, 3);                                      // Получаем нового случайного противника     
        while(newEnemyType == oldEnemyType);                                 // Но не позволяем спаунить одинаковых подряд         
      }
      if (oldEnemyPos >= -24) {                                              // Двигаем старый пока он полностью не скроется за горизонтом
        oldEnemyPos--;                                                       // Двигаем старый
      }
    }

    static uint32_t legTimer = millis();                                     // Таймер анимации ног динозавра
    if (millis() - legTimer >= 130) {
      legTimer = millis();
      legFlag = !legFlag;                                                    // Он просто переключает флаг
    }

    static uint32_t birdTimer = millis();                                    // Таймер анимации взмахов птицы
    if (millis() - birdTimer >= 200) {
      birdTimer = millis();
      birdFlag = !birdFlag;                                                  // Он тоже просто переключает флаг!
    }

    static uint32_t dinoTimer = millis();                                    // Таймер кинематики динозавра
    if (millis() - dinoTimer >= 15) {                                        // С периодом DT
      dinoTimer = millis();
      dinoU += (float)DINO_GRAVITY;                                          // Увеличиваем скорость
      dinoY += (float)dinoU;                                                 // И соответственно координату (динозавр падает)
      if (dinoY >= DINO_GROUND_Y) {                                          // При касании с землей
        dinoY = DINO_GROUND_Y;                                               // Ставим динозвра на землю
        dinoU = 0.0;                                                         // Тормозим его до нуля
      }
    }

    /* ------------------ Drawing ------------------ */
    static uint32_t oledTimer = millis();                                    // Таймер отрисовки игры!
    if (millis() - oledTimer >= (1000 / DINO_GAME_FPS)) {                    // Привязан к FPS игры
      oledTimer = millis();

      oled.clear();                                                                                     // Чистим дисплей                                                                             // Рисуем индикатор
      oled.setCursor(0, 0); oled.print("HI");                                                           // Выводим рекорд
      oled.setCursor(13, 0); oled.print("-"); oled.print(":"); oled.print(score);        // Рекорд:текущий счет
      oled.line(0, 63, 127, 63);                                                                        // Рисуем поверхность земли (линия)

      switch (oldEnemyType) {                                                                           // Выбираем старого противника
        case 0: oled.drawBitmap(oldEnemyPos, 48, CactusSmall_bmp, 16, 16);                   break;     // Рисуем маленький кактус
        case 1: oled.drawBitmap(oldEnemyPos, 48, CactusBig_bmp, 24, 16);                     break;     // Рисуем большой кактус
        case 2: oled.drawBitmap(oldEnemyPos, 35, birdFlag ? BirdL_bmp : BirdR_bmp, 24, 16);  break;     // Рисуем птицу (выбираем одну из двух картинок для анимации)
      }

      switch (newEnemyType) {                                                                           // Выбираем нового противника
        case 0: oled.drawBitmap(newEnemyPos, 48, CactusSmall_bmp, 16, 16);                     break;   // Рисуем маленький кактус
        case 1: oled.drawBitmap(newEnemyPos, 48, CactusBig_bmp, 24, 16);                       break;   // Рисуем большой кактус
        case 2: oled.drawBitmap(newEnemyPos, 35, birdFlag ? BirdL_bmp : BirdR_bmp, 24, 16);    break;   // Рисуем птицу (выбираем одну из двух картинок для анимации)
      }

      if (oldEnemyPos <= (16 + EFH) and oldEnemyPos >= (oldEnemyType > 0 ? -24 - EFH : -16 - EFH)) {    // Если противник в опасной зоне (Отслеживаем столкновения)
        if (oldEnemyType != 2 ? dinoY > 32 - EFH : dinoStand and dinoY > 19 - EFH) {                    // Выбираем условие столкновения в зависимости от типа противника 
          int uiTimer = millis();
          oled.drawBitmap(0, dinoY, DinoStandDie_bmp, 16, 16);                                          // Столкнулись - рисуем погибшего динозавра :(  
          oled.roundRect(0, 10, 127, 40, OLED_CLEAR); oled.roundRect(0, 10, 127, 40, OLED_STROKE);      // Очищаем и обводим область
          oled.setScale(2); oled.setCursor(7, 2); oled.print(F("GAME OVER"));                           // Выводим надпись   
          oled.setScale(1); oled.setCursor(3, 4); oled.print(F("<- Выход"));                            // Выводим подсказку
          oled.setCursor(73, 4); oled.print(F("Играть ->"));                                                // Выводим подсказку
          oled.update(); 
          reset_buttons();                                                                               // Отрисовка картинки на дисплей                                  
          while (1) {                                                                                   // Бесконечный цикл
            buttons_tick();
            if (left.isClick()) goto startDinoGame;                                                         // Начинаем сначала
            if (right.isClick() || millis() - uiTimer > 30000) {
              right.setTimeout(300);
              ok.setTimeout(300);
              ok.setStepTimeout(400);
              return;
            } 
            yield();
          }
        }
      }

      if (dinoStand) {                                                                                  // Если все окей, столкновения нет и дино стоит в полный рост
        oled.drawBitmap(0, dinoY, legFlag ? DinoStandL_bmp : DinoStandR_bmp, 16, 16);                   // Выводим в полный рост с анимацией переступания  
      } else {                                                                                          // Дино пригнулся
        oled.drawBitmap(0, 56, legFlag ? DinoCroachL_bmp : DinoCroachR_bmp, 16, 8);                     // Выводим пригнувшимся, тоже с анимацией ног
      }

      oled.update();                                                                                    // Финальная отрисовка на дисплей
    }
    yield();
  }
}
void dinosaurGame(void) {                                                           // Главное меню игры
  while (true) {                                                                    // Бесконечный цикл                                                                    // Тикаем память                                    // Лучший счет                                      // Берем его из EEPROM
    oled.clear();                                                                   // Очистка дисплея
    oled.roundRect(0, 9, 127, 46, OLED_STROKE);                                     // Отрисовка интерфейса
    oled.setCursor(3, 0); oled.print(F("GOOGLE DINOSAUR GAME"));                    // Отрисовка интерфейса
    oled.setCursor(0, 7); oled.print(F("<- Выход"));                                // Вывод доп. инфы
    oled.setCursor(73, 7); oled.print(F("Играть ->"));                                  // Вывод доп. инфы
    oled.drawBitmap(10, 30, DinoStandL_bmp, 16, 16);                                // Вывод картинок
    oled.drawBitmap(46, 30, CactusBig_bmp, 24, 16);                                 // Вывод картинок
    oled.drawBitmap(91, 20, BirdL_bmp, 24, 16);                                     // Вывод картинок
    oled.update();                                                                  // Вывод на дисплей
    Wire.setClock(1E6);
    while (true) {                                                                  // Вложенный бесконечный цикл
      buttons_tick();                                                               // Тикаем память

      if(right.isClick()){
        Wire.setClock(100000);
        return;
      }

      if (left.isClick()) {                                                         // Нажатие на правую - начать играть
        playDinosaurGame();                                                     // Запускаем игру
        break;                                                                  // При выходе из игры переходим к отрисовке
      }

      yield();
    }
  }
}


// переделанный код пинг понга
void playPong() {
  const int PH = 12, PW = 3, BS = 3; 
  int pY = 26, aiY = 26;
  float bX = 64, bY = 32, bVX = 2.5, bVY = 1.5;
  int scP = 0, scAI = 0;
  
  reset_buttons(); 
  oled.clear();

  while (true) {
    buttons_tick();
    if (ok.isHold()) { oled.clear(); return; }

    //мувмент
    if (left.state()) pY -= 3;
    if (right.state()) pY += 3;
    pY = constrain(pY, 0, 64 - PH);

    //ии
    int aiCenter = aiY + (PH / 2);
    int target = bY;
    if (random(0, 100) < 5) target = random(0, 64); 

    if (aiCenter < target - 2) aiY += 2;
    else if (aiCenter > target + 2) aiY -= 2;
    aiY = constrain(aiY, 0, 64 - PH);

    bX += bVX; bY += bVY;
    if (bY <= 0 || bY >= 64 - BS) bVY = -bVY;

    if (bX <= PW && bX >= 0 && bY + BS >= pY && bY <= pY + PH) {
      bVX = -bVX + 0.2; 
      bX = PW + 1;
      if (left.state()) bVY -= 0.5;
      if (right.state()) bVY += 0.5;
    }
    if (bX >= 127 - PW - BS && bX <= 128 && bY + BS >= aiY && bY <= aiY + PH) {
      bVX = -bVX;
      bX = 127 - PW - BS - 1;
    }

    // счёт
    if (bX < 0) {
      scAI++; // L
      bX = 64; bY = 32; bVX = 2.5; bVY = (random(0, 2) ? 1.5 : -1.5);
    }
    if (bX > 128) {
      scP++; // W
      bX = 64; bY = 32; bVX = -2.5; bVY = (random(0, 2) ? 1.5 : -1.5);
    }

    // графон
    oled.clear();
    
    for (int i = 0; i < 64; i += 4) oled.fastLineV(63, i, i + 2, 1); 

    // счёт
    oled.setCursorXY(56, 0); 
    oled.print(scP); oled.print(":"); oled.print(scAI);

    // мяч и ракетки
    oled.rect(0, pY, PW, pY + PH, OLED_FILL); 
    oled.rect(127 - PW, aiY, 127, aiY + PH, OLED_FILL); 
    oled.rect((int)bX, (int)bY, (int)bX + BS, (int)bY + BS, OLED_FILL);

    oled.update();
    delay(10); 
  }
}
void rouletteGame() {
    const uint8_t* symbols[] = {
        chest_26x26,    // 0 - сундук (15%)
        dimond_26x26,   // 1 - алмаз (7%)  
        apple_26x26,    // 2 - яблоко (25%)
        chery_26x26,    // 3 - вишня (30%)
        strawbery_26x26,// 4 - клубника (20%)
        seven_26x26     // 5 - семерка (3%)
    };
    
    // вероятности выпадения каждого символа (в процентах)
    const uint8_t probabilities[] = {15, 7, 25, 30, 20, 3};
    
    uint8_t reels[3] = {0, 0, 0};
    uint8_t finalReels[3] = {0, 0, 0};
    bool spinning[3] = {false, false, false};
    uint32_t spinStartTime = 0;
    uint32_t lastReelUpdate = 0;
    bool isSpinning = false;
    bool hasWon = false;
    uint8_t reelsStopped = 0;
    bool showResult = false;
    
    const int16_t reelX[3] = {5, 46, 87};
    const int16_t reelY = 15;
    const int16_t reelWidth = 26;
    const int16_t reelHeight = 26;
    
    reset_buttons();
    
    auto weightedRandom = [&]() -> uint8_t {
        int total = 100;
        int r = random(0, total);
        int cumulative = 0;
        
        for (int i = 0; i < 6; i++) {
            cumulative += probabilities[i];
            if (r < cumulative) {
                return i;
            }
        }
        return 5;
    };
    

    auto drawReel = [&](uint8_t reelIndex, uint8_t symbol) {
        oled.clear(reelX[reelIndex], reelY, reelX[reelIndex] + reelWidth - 1, reelY + reelHeight - 1);
        oled.drawBitmap(reelX[reelIndex], reelY, symbols[symbol], reelWidth, reelHeight);
    };
    

    auto drawStaticUI = [&]() {
        oled.clear();
        ui_rama("РУЛЕТКА", true, true, false);
        
        oled.line(42, 12, 42, 50);
        oled.line(83, 12, 83, 50);
        
        for (int i = 0; i < 3; i++) {
            oled.roundRect(reelX[i]-2, reelY-2, reelX[i]+28, reelY+28, OLED_STROKE);
        }
        
        for (int i = 0; i < 3; i++) {
            drawReel(i, reels[i]);
        }
        
        oled.setCursor(0, 7);
        if (showResult) {
            if (hasWon) {
                oled.print("ДЖЕКПОТ! OK-еще");
            } else {
                oled.print("Готово! OK-еще   ");
            }
        } else {
            oled.print("OK-крутить Удр-выход");
        }
        oled.update();
    };
    
    //апдейт статуса
    auto updateStatus = [&]() {
        // клеар
        oled.clear(0, 56, 127, 63);
        oled.setCursor(0, 7);
        if (showResult) {
            if (hasWon) {
                oled.print("ДЖЕКПОТ! OK-еще");
            } else {
                oled.print("Готово! OK-еще   ");
            }
        } else if (isSpinning) {
            oled.print("Крутим...        ");
        } else {
            oled.print("OK-крутить Удр-выход");
        }
    };
    
    //крутите барабан!
    auto startSpin = [&]() {
        isSpinning = true;
        showResult = false;
        hasWon = false;
        reelsStopped = 0;
        spinStartTime = millis();
        lastReelUpdate = millis();
        
        for (int i = 0; i < 3; i++) {
            finalReels[i] = weightedRandom();
            spinning[i] = true;
        }
        
        
        updateStatus();
        oled.update();
    };
    
    //апдейт врашения
    auto updateSpin = [&]() {
        uint32_t currentTime = millis();
        uint32_t spinTime = currentTime - spinStartTime;
        
        if (currentTime - lastReelUpdate > 80) {
            for (int i = 0; i < 3; i++) {
                if (spinning[i]) {
                    reels[i] = random(0, 6);
                    drawReel(i, reels[i]);
                }
            }
            oled.update();
            lastReelUpdate = currentTime;
        }
        
        if (spinTime > 1500 && spinning[0]) {
            spinning[0] = false;
            reels[0] = finalReels[0];
            drawReel(0, reels[0]);
            reelsStopped++;
            oled.update();
        }
        if (spinTime > 2000 && spinning[1]) {
            spinning[1] = false;
            reels[1] = finalReels[1];
            drawReel(1, reels[1]);
            reelsStopped++;
            oled.update();
        }
        if (spinTime > 2500 && spinning[2]) {
            spinning[2] = false;
            reels[2] = finalReels[2];
            drawReel(2, reels[2]);
            reelsStopped++;
            oled.update();
        }
        
        if (reelsStopped >= 3) {
            isSpinning = false;
            showResult = true;
            hasWon = (finalReels[0] == 5 && finalReels[1] == 5 && finalReels[2] == 5);
        }
    };
    
    auto showWinAnimation = [&]() {
        unsigned long animationStart = millis();
        unsigned long lastBlink = animationStart;
        bool inverted = false;
        uint8_t blinkCount = 0;
        
        while (blinkCount < 6) {
            buttons_tick();
            

            if (millis() - lastBlink > 400) {
                inverted = !inverted;
                lastBlink = millis();
                blinkCount++;
                oled.invertDisplay(inverted);
                oled.update();
            }
            
            if (ok.isClick() || ok.isHold()) {
                break;
            }
            delay(50);
        }
        oled.invertDisplay(false);
        oled.update();
    };
    
    drawStaticUI();
    
    while (true) {
        buttons_tick();
        
        if (ok.isHold()) {
            Wire.setClock(100000);
            exit();
            return;
        }
        
        if (isSpinning) {
            updateSpin();
            
            if (!isSpinning && showResult) {
                updateStatus();
                oled.update();
                
                if (hasWon) {
                    showWinAnimation();
                    showResult = true;
                    updateStatus();
                    oled.update();
                }
            }
            
        } else {
            if (ok.isClick()) {
                if (showResult) {
                    showResult = false;
                    startSpin();
                } else {
                    startSpin();
                }
            }
        }
        
        delay(30);
    }
}
// Написал Гайвер (https://github.com/AlexGyver/GyverMatrixBT/blob/master/firmware/GyverMatrixOS_v1.12/g_snake.ino). Комментарии автора созранены
// игра змейка! (выход по удержанию ок)
// **************** НАСТРОЙКИ ЗМЕЙКИ ****************
#define START_LENGTH 4    // начальная длина змейки
#define MAX_LENGTH 80     // максимальная длина змейки
// **************** ДЛЯ РАЗРАБОТЧИКОВ ****************
int8_t vectorX, vectorY;
int8_t headX, headY, buttX, buttY;
int8_t appleX, appleY;
boolean apple_flag, missDelete = false;
bool loadingFlag = 0;      // Флаг игр (остался от прошивки Гайвера)
int8_t buttVector[MAX_LENGTH];
int snakeLength;
boolean butt_flag, gameover;
#define MAX_WIDTH 64
#define MAX_HEIGHT 128
uint8_t oledbuf[MAX_WIDTH * MAX_HEIGHT]; // замена матрицы для игр
#define T_SEGMENT 4
#define SEGMENT (T_SEGMENT)
uint8_t WIDTH = (64/SEGMENT - 16/SEGMENT);          // -1 (для текста) // ширина для того же тетриса
uint8_t HEIGHT = (128/SEGMENT);  
uint32_t uiTimer = 0; 
#define X0 16
uint8_t buttons;
uint8_t button_rev;
#define GAME_SPEED        350           // Скорость  (меньше - быстрее)
GTimer_ms gameTimer(GAME_SPEED); // Таймер игр
void _left() {
  if (vectorX == 0 && vectorY == 1) {
    vectorX = 1;
    vectorY = 0;
  } else if (vectorX == 1 && vectorY == 0) {
    vectorX = 0;
    vectorY = -1;
  } else if (vectorX == 0 && vectorY == -1) {
    vectorX = -1;
    vectorY = 0;
  } else if (vectorX == -1 && vectorY == 0) {
    vectorX = 0;
    vectorY = 1;
  }
}
void _right() {
  if (vectorX == 0 && vectorY == 1) {
    vectorX = -1;
    vectorY = 0;
  } else if (vectorX == 1 && vectorY == 0) {
    vectorX = 0;
    vectorY = 1;
  } else if (vectorX == 0 && vectorY == -1) {
    vectorX = 1;
    vectorY = 0;
  } else if (vectorX == -1 && vectorY == 0) {
    vectorX = 0;
    vectorY = -1;
  }
}

void buttonsTickSnake() {
  buttons_tick();
  
  if (left.isClick()) {
    _right();
  } else if (right.isClick()) {
    _left();
  }
}

void newGameSnake() {
  oled.clear();
  // свежее зерно для генератора случайных чисел
  randomSeed(millis());
  for (int i = 0; i < MAX_WIDTH * MAX_HEIGHT; i++) oledbuf[i] = 0;

  // длина из настроек, начинаем в середине экрана, бла-бла-бла
  snakeLength = START_LENGTH;
  headY = WIDTH / 2;
  headX = HEIGHT / 2;
  buttY = headY;

  vectorX = 1;  // начальный вектор движения задаётся вот здесь
  vectorY = 0;
  buttons = 4;

  // первоначальная отрисовка змейки и забивка массива векторов для хвоста
  for (byte i = 0; i < snakeLength; i++) {
    oled.rect((headX - i)*SEGMENT, X0 + headY * SEGMENT, (headX - i)*SEGMENT + SEGMENT - 1, X0 + headY * SEGMENT + SEGMENT - 1, OLED_STROKE);
    oledbuf[headY + (headX - i)*WIDTH] = 2;
    buttVector[i] = 0;
  }
  oled.update();
  buttX = headX - snakeLength;   // координата хвоста как голова - длина
  missDelete = false;
  apple_flag = false;
}
void snakeRoutine() {
  if (loadingFlag) {
    oled.clear();
    loadingFlag = false;
    newGameSnake();
  }
  buttonsTickSnake();
  if (gameTimer.isReady()) {
    // БЛОК ГЕНЕРАЦИИ ЯБЛОКА
    while (!apple_flag) {                         // пока яблоко не создано
      appleY = random(0, WIDTH);                  // взять случайные координаты
      appleX = random(0, HEIGHT);

      // проверить, не совпадает ли координата с телом змеи
      while (oledbuf[appleY + appleX * WIDTH] != 0) {
        appleY = random(0, WIDTH);                // взять случайные координаты
        appleX = random(0, HEIGHT);
      }
      apple_flag = true;                          // если не совпадает, считаем что яблоко создано
      oled.rect(appleX * SEGMENT, X0 + appleY * SEGMENT, appleX * SEGMENT + SEGMENT - 1, X0 + appleY * SEGMENT + SEGMENT - 1, OLED_STROKE); // и рисуем
      oledbuf[appleY + appleX * WIDTH] = 1;
      oled.update();
    }

    // запоминаем, куда повернули голову
    // 0 - право, 1 - лево, 2 - вверх, 3 - вниз
    if (vectorX > 0) buttVector[snakeLength] = 0;
    else if (vectorX < 0) buttVector[snakeLength] = 1;
    if (vectorY > 0) buttVector[snakeLength] = 2;
    else if (vectorY < 0) buttVector[snakeLength] = 3;

    // смещение головы змеи
    headX += vectorX;
    headY += vectorY;

    if (headX < 0 || headX > HEIGHT - 1 || headY < 0 || headY > WIDTH - 1) { // если вышла за границы поля
      gameover = true;
    }
    buttons_tick();
    if (!gameover) {
      // проверка на gameover
      if (oledbuf[headY + headX * WIDTH] == 2) { // если змея врезалась в себя
        gameover = true;                           // флаг на отработку
      }

      // БЛОК ОТРАБОТКИ ПОЕДАНИЯ ЯБЛОКА
      if (!gameover && oledbuf[headY + headX * WIDTH] == 1) { // если попали головой в яблоко
        apple_flag = false;                       // флаг что яблока больше нет
        snakeLength++;                            // увеличить длину змеи
        buttVector[snakeLength] = 4;              // запоминаем, что надо будет не стирать хвост
      }

      // вычисляем координату хвоста (чтобы стереть) по массиву вектора
      switch (buttVector[0]) {
        case 0: buttX += 1;
          break;
        case 1: buttX -= 1;
          break;
        case 2: buttY += 1;
          break;
        case 3: buttY -= 1;
          break;
        case 4: missDelete = true;  // 4 значит не стирать!
          break;
      }

      // смещаем весь массив векторов хвоста ВЛЕВО
      for (byte i = 0; i < snakeLength; i++) {
        buttVector[i] = buttVector[i + 1];
      }

      // если змея не в процессе роста, закрасить бывший хвост чёрным
      if (!missDelete) {
        oled.clear(buttX * SEGMENT, X0 + buttY * SEGMENT, buttX * SEGMENT + SEGMENT - 1, X0 + buttY * SEGMENT + SEGMENT - 1);
        oledbuf[buttY + buttX * WIDTH] = 0;
      } else missDelete = false;

      // рисуем голову змеи в новом положении
      oled.rect(headX * SEGMENT, X0 + headY * SEGMENT, headX * SEGMENT + SEGMENT - 1, X0 + headY * SEGMENT + SEGMENT - 1, OLED_STROKE);
      oledbuf[headY + headX * WIDTH] = 2;
      oled.update();
    }
  }

  // если он настал
  if (gameover) {
    gameover = false;

    oled.clear();
    oled.update();
    int score = snakeLength - START_LENGTH;
    //if (score > sets.snakeBestScore) {
    //  sets.snakeBestScore = score;
    //  data.update();
    //}
    delay(1000);
    newGameSnake(); // Тыгдык опять
  }
}
void playSnakeGame() {
  loadingFlag = true;
  oled.clear();
  for (int i = 0; i < MAX_WIDTH * MAX_HEIGHT; i++) oledbuf[i] = 0;
  while (1) {
    buttons_tick();
                                                                                        // Рисуем индикатор
    oled.line(0, 10, 127, 10);                // Линия
    oled.rect(0, 16, 127, 63, OLED_STROKE);   // Рамка
    oled.setCursor(0, 0); oled.print("HI");                                                                              // Выводим рекорд
    oled.setCursor(13, 0); //oled.print(sets.snakeBestScore);
     oled.print(":"); oled.print(snakeLength - START_LENGTH);     // Рекорд:текущий счет
    if (ok.isHold()) {
      uiTimer = millis();
      //if (snakeLength - START_LENGTH > sets.snakeBestScore) {
      //  sets.snakeBestScore = snakeLength - START_LENGTH;  // записать
      //  data.update();                                     // обновить
      //}
      break;
    }
    snakeRoutine();
    yield();
  }
  return;
}

void snakeGame() {  
  uiTimer = millis();                                                                         // Главное меню игры
  while (true) {                                                                             // Бесконечный цикл                                                                            // Тикаем память
    oled.clear();                                                                            // Очистка дисплея
    oled.roundRect(0, 9, 127, 46, OLED_STROKE);                                              // Отрисовка интерфейса
    oled.setCursor(3, 0); oled.print(F("SNAKE GAME"));                                       // Отрисовка интерфейса
//    oled.setCursor(18, 6); oled.print(F("Лучший счет:")); oled.print(sets.snakeBestScore);   // Вывод рекорда
    oled.setCursor(0, 7); oled.print(F("<- Выход"));                                         // Вывод доп. инфы
    oled.setCursor(73, 7); oled.print(F("Играть ->"));                                           // Вывод доп. инфы
    //drawFigureRaw(3, 0, 32, 24);                                                             // Вывод картинок
    //drawFigureRaw(5, 1, 16, 16);                                                             // Вывод картинок
    //drawFigureRaw(6, 2, 20, 40);                                                             // Вывод картинок
    const int d_pxlsX[] = {0, 0, 0, 1, 2, 2, 3, 4, 4, 5, 6, 6, 6, 6, 5, 4, 2};
    const int d_pxlsY[] = {1, 2, 3, 3, 3, 2, 2, 2, 3, 3, 3, 2, 1, 0, 0, 0, 0};
    for (int i = 0; i < sizeof(d_pxlsX) / sizeof(int); i++) {
      int X = 7 + d_pxlsX[i];
      int Y = 0 + d_pxlsY[i];
      oled.rect(X * 6, X0 + Y * 6, X * 6 + 5, X0 + Y * 6 + 5, OLED_STROKE);
    }
    oled.update();                                                                           // Вывод на дисплей
    while (true) {                                                                           // Вложенный бесконечный цикл
      buttons_tick();                                                                        // Тикаем память

      if (right.isClick() || millis() - uiTimer >= 10000) {
        return;
      }

      if (left.isClick()) {                                                                  // Нажатие на правую - начать играть
        playSnakeGame();                                                                // Запускаем игру
        loadingFlag = true;
        break;                                                                           // При выходе из игры переходим к отрисовке
      }

      yield();
    }
  }
}
String constrainString(String str, uint8_t minLen, uint8_t maxLen) {
  if (str.length() < minLen) {
      return String("12345678"); //вернуть мин. допустимый
  }
  if (str.length() > maxLen) {
      str = str.substring(0, maxLen);
  }
  return str;
}
void update(sets::Updater& upd) {
  static uint8_t lastBrightness = db[kk::OLED_BRIGHTNESS].toInt();
  if(lastBrightness != db[kk::OLED_BRIGHTNESS].toInt()) {
      lastBrightness = db[kk::OLED_BRIGHTNESS].toInt();
      oled.setContrast(lastBrightness);
      db.update();
  }
  

    
  if (alert_f) {
    alert_f = false;
    upd.alert("Пароль должен быть не менее 8 символов!");
  }
}

// Всем ку кто читает код :)

// TODO: переделать этот багованный код
bool connectToWiFi() {
  String ssid = db[kk::wifi_ssid];
  String pass = db[kk::wifi_pass];
  WiFi.mode(WIFI_STA);
  if(ssid.isEmpty()) {
      Serial.println("WiFi SSID not configured!");
      return false;
  }
  // ОБЯЗАТЕЛЬНО ДЛЯ ESP32 C3!!!
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  // ---
  wifiConnected = false;
  WiFi.begin(ssid, pass);
  oled.autoPrintln(true);
  oled.clear();
  oled.home();
  oled.print("Подключение к");
  oled.setCursor(0, 2);
  oled.print(ssid);
  oled.setCursor(0, 3);
  oled.print("Статус:");
  oled.update();
  uint32_t startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    oled.print(".");
    oled.update();
    if (millis() - startTime > 10000) {
      oled.print("Не подключено! Запуск AP");
      oled.update();
      delay(1000);
      oled.autoPrintln(false);
      return false;
    }
  }
  oled.autoPrintln(false);
  return true;
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  String ssid = db[kk::AP_SSID];
  String pass = db[kk::AP_PASS];
 
  if (ssid.length() == 0) {
      ssid = "CatOs";
      db[kk::AP_SSID] = ssid;
  }
  if (pass.length() < 8) {
      pass = "12345678";
      db[kk::AP_PASS] = pass;
  }
  WiFi.softAP(
    db[kk::AP_SSID].toString().c_str(),
    db[kk::AP_PASS].toString().c_str()
  );
 
}
void stopWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi completely stopped");
}
String validatePetName(const String& name) {
  if (name.length() == 0) {
    return "CatOsGotchi";
  } else if (name.length() > 16) {
    return name.substring(0, 16);
  }
  return name;
}
// сохранение состояния
void savePetState() {
  db[kk::PET_NAME] = catOsGotchi.name;
  db[kk::PET_HUNGER] = catOsGotchi.hunger;
  db[kk::PET_HAPPINESS] = catOsGotchi.happiness;
  db[kk::PET_LAST_UPDATE] = catOsGotchi.lastUpdate;
  db[kk::PET_LAST_VISIT] = catOsGotchi.lastVisit;
  db.update();
}
void build(sets::Builder& b) { // страница веб морды
  // Секция настроек дисплея
  {
      sets::Group g(b, "Дисплей");
      b.Slider(kk::OLED_BRIGHTNESS, "Яркость дисплея", 0, 255, 1, "", nullptr, sets::Colors::Blue);
  }

  // Секция WiFi
  {
      sets::Group g(b, "WiFi");
      // Переключатель WiFi с привязкой к значению из БД
      bool wifiEnabled = db[kk::wifi_enabled].toInt();
      b.Switch(kk::wifi_enabled, "WiFi подключение", &wifiEnabled);
      b.Input(kk::wifi_ssid, "SSID WiFi");
      b.Pass(kk::wifi_pass, "Пароль WiFi");
  }

  // Секция точки доступа
  {
      sets::Group g(b, "Точка доступа");
      b.Input(kk::AP_SSID, "SSID точки");
      b.Pass(kk::AP_PASS, "Пароль точки");
  }
  {
      sets::Group g(b, "CatOsGotchi");
      b.Input(kk::PET_NAME, "Имя питомца (макс. 16 символов)");
      
      // Информация о текущем состоянии
      b.Label("Текущее имя", catOsGotchi.name.c_str());
      b.Label("Уровень голода", String(catOsGotchi.hunger) + "%");
      b.Label("Уровень счастья", String(catOsGotchi.happiness) + "%");
      if(b.Button("Применить имя питомца")) {
        String newName = db[kk::PET_NAME].toString();
        String validatedName = validatePetName(newName);
        
        if (validatedName != newName) {
          db[kk::PET_NAME] = validatedName;
          db.update();
          if (newName.length() > 16) {
            alert_f = true;
          } else {
            alert_f = true;
          }
        }
        
        catOsGotchi.name = validatedName;
        savePetState();
        b.reload();
      }
  }
  {
    sets::Group g(b, "Циферблат");
    bool watch_style_enabled = db[kk::WATCHFACE_STYLE].toInt();
    b.Switch(kk::WATCHFACE_STYLE, "Установить круглый циферблат", &watch_style_enabled);
    b.Slider(kk::PET_HUNGER, "PET HUNDER DEBUG", 10, 100, 1, "", nullptr, sets::Colors::Blue);
    b.Slider(kk::PET_HAPPINESS, "PET HAPPINESS DEBUG", 10, 100, 1, "", nullptr, sets::Colors::Blue);
  }
  {
      sets::Group g(b, "Управление");
      if(b.Button("Применить сетевые настройки")) {
        if (db[kk::AP_PASS].toString().length() >= 8) {
            db.update();
            sett.reload(true);
        } else {
            alert_f = true;  // Устанавливаем флаг
        }
    }
  }
}
void showWelcomeScreen() {
  
  ui_rama("Добро пожаловать",1,1,1,0);
  
  oled.setScale(1);
  oled.setCursor(0, 2);
  oled.print("  Спасибо за то, что");
  oled.setCursor(0,3);
  oled.print(" выбрали CatOs-Watch.");
  oled.setCursor(0,4);
  oled.print("Надеемся что прошивка");
  oled.setCursor(0,5);
  oled.print("  вам понравится :)");
  oled.setCursor(0, 7);
  oled.print("Нажмите любую кнопку");
  
  oled.update();
  
  reset_buttons();
  while (true) {
    buttons_tick();
    if (ok.isClick() || left.isClick() || right.isClick()) {
      db.init(kk::first_start, true);
      db.update();
      break;
    }
    delay(10);
  }
}

void loadPetState() {
  if (db.has(kk::PET_NAME)) catOsGotchi.name = db[kk::PET_NAME].toString();
  if (db.has(kk::PET_HUNGER)) catOsGotchi.hunger = db[kk::PET_HUNGER].toInt();
  if (db.has(kk::PET_HAPPINESS)) catOsGotchi.happiness = db[kk::PET_HAPPINESS].toInt();
  if (db.has(kk::PET_LAST_UPDATE)) catOsGotchi.lastUpdate = db[kk::PET_LAST_UPDATE].toInt();
  if (db.has(kk::PET_LAST_VISIT)) catOsGotchi.lastVisit = db[kk::PET_LAST_VISIT].toInt();
  
  if (catOsGotchi.name.length() == 0) {
    catOsGotchi.name = "CatOsGotchi";
    catOsGotchi.hunger = 80;
    catOsGotchi.happiness = 80;
    catOsGotchi.lastUpdate = timeToUnix(rtc.getTime());
    catOsGotchi.lastVisit = timeToUnix(rtc.getTime());
    savePetState();
  }
}

// инициализировать настройки
void initSettings() {
  if (!db.has(kk::OLED_BRIGHTNESS)) db.init(kk::OLED_BRIGHTNESS, 128);
  if (!db.has(kk::AP_SSID)) {
    db.init(kk::AP_SSID, "CatOs-Watch");
  }
  if (!db.has(kk::AP_PASS)) {
      db.init(kk::AP_PASS, "12345678");
  }
  if (!db.has(kk::wifi_enabled)) {
      db.init(kk::wifi_enabled, 0);
  }
  if (!db.has(kk::wifi_ssid)) db.init(kk::wifi_ssid, "");
  if (!db.has(kk::wifi_pass)) db.init(kk::wifi_pass, "");
  if (!db.has(kk::WATCHFACE_STYLE)) db.init(kk::WATCHFACE_STYLE, false);
  if (!db.has(kk::time_sync_enabled)) {
      db.init(kk::time_sync_enabled, 1);
  }
  
  if (!db.has(kk::ALARM_HOUR)) db.init(kk::ALARM_HOUR, 7);
  if (!db.has(kk::ALARM_MINUTE)) db.init(kk::ALARM_MINUTE, 0);
  if (!db.has(kk::ALARM_DAY)) db.init(kk::ALARM_DAY, 1);
  if (!db.has(kk::ALARM_MONTH)) db.init(kk::ALARM_MONTH, 1);
  if (!db.has(kk::ALARM_ENABLED)) db.init(kk::ALARM_ENABLED, false);
  if (!db.has(kk::first_start)) showWelcomeScreen();
  if (!db.has(kk::PET_NAME)) db.init(kk::PET_NAME, "CatOsGotchi");
  if (!db.has(kk::PET_HUNGER)) db.init(kk::PET_HUNGER, 80);
  if (!db.has(kk::PET_HAPPINESS)) db.init(kk::PET_HAPPINESS, 80);
  if (!db.has(kk::PET_LAST_UPDATE)) db.init(kk::PET_LAST_UPDATE, timeToUnix(rtc.getTime()));
  if (!db.has(kk::PET_LAST_VISIT)) db.init(kk::PET_LAST_VISIT, timeToUnix(rtc.getTime()));
  db.update();
  loadPetState();
  loadAlarmSettings();
}
void update_header_time() {
  Datime dt = rtc.getTime();
  char timeStr[6];
  if (dt.hour < 10 && dt.minute < 10) {
    sprintf(timeStr, "0%d:0%d", dt.hour, dt.minute);
  } else if (dt.hour < 10) {
    sprintf(timeStr, "0%d:%d", dt.hour, dt.minute);
  } else if (dt.minute < 10) {
    sprintf(timeStr, "%d:0%d", dt.hour, dt.minute);
  } else {
    sprintf(timeStr, "%d:%d", dt.hour, dt.minute);
  }
  
  oled.setScale(1);
  oled.setCursorXY(98, 0);
  oled.print(timeStr);
  oled.update();
}
void create_settings() {
  String ssid = db[kk::wifi_ssid];
  bool wifi_connected = false;
  
    if(db[kk::wifi_enabled].toInt()) {
      connectToWiFi();
      if(ssid.isEmpty()) {
        Serial.println("WiFi SSID not configured!");
        startAP();
      }
      if(WiFi.status() == WL_CONNECTED) {
          Serial.print("Connected! IP: ");
          Serial.println(WiFi.localIP());
          wifiConnected = true;
      } else {
          startAP();
      }
  } else {
      startAP();
  }
 
  oled.clear();
  ui_rama("WiFi Веб", true, true, true, false);
  oled.setCursor(0, 2);
  if (wifiConnected) {
    oled.print("IP:");
    oled.print(WiFi.localIP());
    oled.setCursor(0, 3);
    oled.print("Сеть: ");
    oled.print(ssid);
  } else {
    oled.setCursor(0, 2);
    oled.print("IP: " + WiFi.softAPIP().toString());
    oled.setCursor(0, 3);
    oled.print("Сеть: " + db[kk::AP_SSID].toString());
    oled.setCursor(0, 4);
    oled.print("Пароль: " + db[kk::AP_PASS].toString());
    oled.update();
  }
  oled.setCursor(0, 6);
  oled.print("OK - Перезагрузка");
  oled.update();
  sett.setVersion(FIRMWARE_VERSION);
  // Запуск веб-сервера
  sett.begin();
  sett.onBuild(build);
  uint8_t last_minute = rtc.getTime().minute;
  while(true) {
      sett.tick();
      delay(10);
      //Datime current_time = rtc.getTime();
      //if (current_time.minute != last_minute) {
      //  update_header_time();
      //  last_minute = current_time.minute;
      //}
      buttons_tick();
      if(ok.isClick()) {
          ESP.restart();
      }
  }
}
// парсинг параметров
String getParamValue(String body, String param) {
    int startIndex = body.indexOf(param + "=");
    if(startIndex == -1) return "";
    
    startIndex += param.length() + 1;
    int endIndex = body.indexOf("&", startIndex);
    if(endIndex == -1) {
        endIndex = body.length();
    }
    
    return body.substring(startIndex, endIndex);
}

void time_sync_menu() {
    String ssid = db[kk::wifi_ssid];
    bool wifi_connected = false;
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    if(db[kk::wifi_enabled].toInt()) {
        wifi_connected = connectToWiFi();
        if(!wifi_connected) {
            startAP();
        }
    } else {
        startAP();
    }
    
    WiFiServer timeServer(80);
    timeServer.begin();
    
    oled.clear();
    ui_rama("Time Sync", true, true, true, false);
    
    if (wifi_connected) {
        oled.setCursor(0, 2);
        oled.print("IP:");
        oled.print(WiFi.localIP());
        oled.setCursor(0, 3);
        oled.print("Сеть: ");
        oled.print(ssid);
        oled.setCursor(0, 4);
        oled.print("Готов к синхронизации");
    } else {
        oled.setCursor(0, 2);
        oled.print("IP: " + WiFi.softAPIP().toString());
        oled.setCursor(0, 3);
        oled.print("Сеть: " + db[kk::AP_SSID].toString());
        oled.setCursor(0, 4);
        oled.print("Пароль: " + db[kk::AP_PASS].toString());
    }
    
    oled.setCursor(0, 6);
    oled.print("OK - Выход");
    oled.update();
    
    bool timeSynced = false;
    uint32_t lastClientCheck = millis();
    
    while(true) {
        if(millis() - lastClientCheck >= 100) {
            lastClientCheck = millis();
            
            WiFiClient client = timeServer.available();
            if(client) {
                Serial.println("New Client.");
                String currentLine = "";
                uint32_t requestTime = millis();
                
                while(client.connected() && millis() - requestTime < 5000) {
                    if(client.available()) {
                        char c = client.read();
                        Serial.write(c);
                        
                        if(c == '\n') {
                            if(currentLine.length() == 0) {
                                client.println("HTTP/1.1 200 OK");
                                client.println("Content-type:text/html");
                                client.println();
                                
                                client.println("<!DOCTYPE html><html>");
                                client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                                client.println("<style>");
                                client.println("body { background-color: #000000; color: #ffffff; text-align: center; font-family: 'Courier New', monospace; margin: 0; padding: 0; height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; position: relative; }");
                                client.println(".button { background-color: #000000; color: #ffffff; border: 2px solid #ffffff; width: 200px; height: 80px; text-align: center;");
                                client.println("text-decoration: none; display: inline-block; font-size: 24px; font-family: 'Courier New', monospace; cursor: pointer; border-radius: 0; margin: 20px; letter-spacing: 1px; }");
                                client.println(".button:active { background-color: #333333; }");
                                client.println("#status { margin-top: 20px; font-size: 16px; font-family: 'Courier New', monospace; min-height: 24px; letter-spacing: 1px; }");
                                client.println(".footer { position: absolute; bottom: 15px; width: 100%; text-align: center; font-size: 12px; color: #888888; }");
                                client.println("</style>");
                                client.println("<script>");
                                client.println("function syncTime() {");
                                client.println("  var now = new Date();");
                                client.println("  var year = now.getFullYear();");
                                client.println("  var month = now.getMonth() + 1;");
                                client.println("  var day = now.getDate();");
                                client.println("  var hour = now.getHours();");
                                client.println("  var minute = now.getMinutes();");
                                client.println("  var second = now.getSeconds();");
                                client.println("  ");
                                client.println("  var xhr = new XMLHttpRequest();");
                                client.println("  xhr.open('POST', '/sync', true);");
                                client.println("  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');");
                                client.println("  xhr.send('year=' + year + '&month=' + month + '&day=' + day + '&hour=' + hour + '&minute=' + minute + '&second=' + second);");
                                client.println("  ");
                                client.println("  xhr.onload = function() {");
                                client.println("    if (xhr.status == 200) {");
                                client.println("      document.getElementById('status').innerHTML = 'Time sync successful!';");
                                client.println("    }");
                                client.println("  };");
                                client.println("}");
                                client.println("</script>");
                                client.println("</head>");
                                client.println("<body>");
                                client.println("<button class=\"button\" onclick=\"syncTime()\">SYNC TIME</button>");
                                client.println("<div id=\"status\"></div>");
                                client.println("<div class=\"footer\">");
                                client.println("By CatDevCode<br>");
                                client.println("CatOs-Watch " FIRMWARE_VERSION);
                                client.println("</div>");
                                client.println("</body></html>");
                                client.println();
                                break;
                            } else {
                                currentLine = "";
                            }
                        } else if(c != '\r') {
                            currentLine += c;
                        }
                        
                        if(currentLine.startsWith("POST /sync")) {
                            while(client.connected()) {
                                String header = client.readStringUntil('\n');
                                if(header == "\r") break;
                            }
                            
                            String body = "";
                            while(client.available()) {
                                body += (char)client.read();
                            }
                            
                            int year = getParamValue(body, "year").toInt();
                            int month = getParamValue(body, "month").toInt();
                            int day = getParamValue(body, "day").toInt();
                            int hour = getParamValue(body, "hour").toInt();
                            int minute = getParamValue(body, "minute").toInt();
                            int second = getParamValue(body, "second").toInt();
                            
                            if(year > 2000 && month >= 1 && month <= 12) {
                                rtc.setTime(second, minute, hour, day, month, year);
                                timeSynced = true;
                                
                                oled.clear();
                                ui_rama("Time Sync", true, true, true, false);
                                oled.setCursor(0, 2);
                                oled.print("Время синхронизировано!");
                                oled.setCursor(0, 3);
                                oled.print(String(hour) + ":" + String(minute) + ":" + String(second));
                                oled.setCursor(0, 4);
                                oled.print(String(day) + "." + String(month) + "." + String(year));
                                oled.update();
                                
                                client.println("HTTP/1.1 200 OK");
                                client.println("Content-type:text/plain");
                                client.println();
                                client.println("Time synced successfully");
                            } else {
                                client.println("HTTP/1.1 400 Bad Request");
                                client.println("Content-type:text/plain");
                                client.println();
                                client.println("Invalid time data");
                            }
                            break;
                        }
                    }
                }
                
                client.stop();
                Serial.println("Client disconnected.");
            }
        }
        
        buttons_tick();
        if(ok.isClick()) {
          esp_restart();
        }
        
        delay(10);
    }
}

/* Написано Матвеем и Андреем Бородиными, потому что мы можем в Си. */
double display = 0;      // Число на экране
double accumulator = 0;  // Аккумулятор для +, -, *, /
int op = 0;              // Операции 1 +, 2 -, 3 *, 4 /
int button_x = 0;        // Выбранная кнопка по горизонтали
int button_y = 0;        // Выбранная кнопка по вертикали

void drawDisplay() {
  oled.clear();
  // Число из display (может нарисовать и аккумулятор с операций? места на экране ещё полно)
  oled.setCursor(1, 1);
  oled.print(display);

  // Рисуем клавиатуру
  oled.setCursor(5, 3);
  oled.print(F("C = + -"));
  oled.setCursor(5, 4);
  oled.print(F("1 2 3 *"));
  oled.setCursor(5, 5);
  oled.print(F("4 5 6 /"));
  oled.setCursor(5, 6);
  oled.print(F("7 8 9 0"));

  // Выбранная кнопка
  oled.roundRect(3 + button_y * 12, 22 + button_x * 8, 13 + button_y * 12, 32 + button_x * 8, OLED_STROKE);
  oled.update();
}

void okButtonClick() {
  // Ифы, ифчики, ифята
  if (button_x == 0) {
    if (button_y == 0) {
      // C
      display = 0;
    } else if (button_y == 1) {
      // =
      if (op == 1) {
        display = display + accumulator;
      }
      if (op == 2) {
        display = accumulator - display;
      }
      if (op == 3) {
        display = accumulator * display;
      }
      if (op == 4) {
        display = accumulator / display;
      }
      op = 0;
    } else if (button_y == 2) {
      // +
      accumulator = display;
      display = 0;
      op = 1;
    } else if (button_y == 3) {
      // -
      accumulator = display;
      display = 0;
      op = 2;
    }
  } else if (button_x == 1) {
    if (button_y == 0) {
      //1
      display = display * 10 + 1;
    } else if (button_y == 1) {
      //2
      display = display * 10 + 2;
    } else if (button_y == 2) {
      //3
      display = display * 10 + 3;
    } else if (button_y == 3) {
      //*
      accumulator = display;
      display = 0;
      op = 3;
    }

  } else if (button_x == 2) {
    if (button_y == 0) {
      //4
      display = display * 10 + 4;
    } else if (button_y == 1) {
      //5
      display = display * 10 + 5;
    } else if (button_y == 2) {
      //6
      display = display * 10 + 6;
    } else if (button_y == 3) {
      // /
      accumulator = display;
      display = 0;
      op = 4;
    }

  } else if (button_x == 3) {
    if (button_y == 0) {
      //7
      display = display * 10 + 7;
    } else if (button_y == 1) {
      //8
      display = display * 10 + 8;
    } else if (button_y == 2) {
      //9
      display = display * 10 + 9;
    } else if (button_y == 3) {
      // 0
      display = display * 10;
    }
  }
  // Элсы, элсики, элсята
}

void calc(void) {
  while (true) {
    oled.clear();
    oled.update();
    drawDisplay();
    while (true) {
      buttons_tick();
      if (ok.isClick()) {
        okButtonClick();
        drawDisplay();
        oled.update();
      }

      // Передвижения кнопок
      if (right.isClick()) {
        button_x++;
        if (button_x >= 4)
          button_x = 0;
        drawDisplay();
        oled.update();
      }
      if (left.isClick()) {
        button_y++;
        if (button_y >= 4)
          button_y = 0;
        drawDisplay();
        oled.update();
      }


      if (ok.isHold()) {
        Wire.setClock(100000);
        exit();
        return;
      }
      yield();
    }
  }
}
// Универсальная функция отрисовки меню

  
//void drawMenu(Menu* menu) {
  
//  oled.clear();
//  for (uint8_t i = 0; i < VISIBLE_ITEMS && (i + top_item) < menu->itemCount; i++) {
//    oled.setCursor(1, i + 2);
//    oled.print(menu->items[i + top_item].name);
 // }
  
//  ui_rama(menu->title, true, true, false);
//}

//void updatePointer() {
//  static int8_t last_pointer = -1;
//  static int8_t last_top = -1;
//  
//  if (last_top != top_item) {
//    drawMenu(currentMenu);
//  last_top = top_item;
//  }
//  
//  if (last_pointer != -1) {
//    oled.setCursor(0, last_pointer - top_item + 2);
//    oled.print(" ");
//  }
//  
//  oled.setCursor(0, pointer - top_item + 2);
//  oled.print(">");
//  oled.update();
//  last_pointer = pointer;
//}
void timer() {
  enum {SET_HOURS, SET_MINS, SET_SECS, RUNNING, ALARM} state = SET_HOURS;
  uint8_t hours = 0;
  uint8_t mins = 0;
  uint8_t secs = 0;
  unsigned long targetTime = 0;
  bool needRedraw = true;
  unsigned long lastBlink = 0;
  bool blinkState = true;
  uint8_t last_hours = 255, last_mins = 255, last_secs = 255;
  
  reset_buttons();
  
  oled.clear();
  ui_rama("Таймер", true, true, false);
  oled.update();

  while(1) {
      buttons_tick();
      

      bool valuesChanged = (hours != last_hours) || (mins != last_mins) || (secs != last_secs);
      
      switch(state) {
          case SET_HOURS:
              if(right.isClick()) {
                  hours = (hours + 1) % 24;
                  needRedraw = true;
              }
              if(left.isClick()) {
                  hours = (hours > 0) ? hours - 1 : 23;
                  needRedraw = true;
              }
              
              if(ok.isClick()) {
                  state = SET_MINS;
                  needRedraw = true;
              }
              break;
              
          case SET_MINS:
              if(right.isClick()) {
                  mins = (mins + 1) % 60;
                  needRedraw = true;
              }
              if(left.isClick()) {
                  mins = (mins > 0) ? mins - 1 : 59;
                  needRedraw = true;
              }
              
              if(ok.isClick()) {
                  state = SET_SECS;
                  needRedraw = true;
              }
              break;
              
          case SET_SECS:
              if(right.isClick()) {
                  secs = (secs + 1) % 60;
                  needRedraw = true;
              }
              if(left.isClick()) {
                  secs = (secs > 0) ? secs - 1 : 59;
                  needRedraw = true;
              }
              
              if(ok.isClick()) {
                  if(hours == 0 && mins == 0 && secs == 0) {
                      needRedraw = true;
                  } else {
                      targetTime = millis() + 
                          (hours * 3600000UL) + 
                          (mins * 60000UL) + 
                          (secs * 1000UL);
                      state = RUNNING;
                      needRedraw = true;
                  }
              }
              break;
              
          case RUNNING: {
              unsigned long remaining = targetTime - millis();
              
              if(remaining <= 0) {
                  state = ALARM;
                  needRedraw = true;
                  break;
              }
              
              static uint32_t lastSecondUpdate = 0;
              if(millis() - lastSecondUpdate >= 1000) {
                  lastSecondUpdate = millis();
                  needRedraw = true;
              }
              
              if(ok.isClick()) {
                  state = SET_HOURS;
                  hours = mins = secs = 0;
                  needRedraw = true;
              }
              break;
          }
              
          case ALARM:
              if(millis() - lastBlink >= 500) {
                  lastBlink = millis();
                  blinkState = !blinkState;
                  needRedraw = true;
              }
              
              if(ok.isClick()) {
                  oled.invertDisplay(false);
                  state = SET_HOURS;
                  hours = mins = secs = 0;
                  needRedraw = true;
              }
              break;
      }
      
      //исправлен баг с мирцанием, перерисовываем только когда надо
      if(needRedraw) {
          needRedraw = false;
          
          last_hours = hours;
          last_mins = mins;
          last_secs = secs;
          
          oled.setCursor(0, 2);
          oled.print("                    ");
          oled.setCursor(0, 3);
          oled.print("                    ");
          oled.setCursor(0, 4);
          oled.print("                    ");
          oled.setCursor(0, 5);
          oled.print("                    ");
          
          oled.setCursor(0, 3);
          oled.setScale(2);
          
          switch(state) {
              case SET_HOURS:
              case SET_MINS:
              case SET_SECS:
                  oled.print(hours < 10 ? "0" : ""); oled.print(hours);
                  oled.print(":");
                  oled.print(mins < 10 ? "0" : ""); oled.print(mins);
                  oled.print(":");
                  oled.print(secs < 10 ? "0" : ""); oled.print(secs);
                  break;
                  
              case RUNNING: {
                  unsigned long remaining = targetTime - millis();
                  if(remaining > 86400000UL) remaining = 0;
                  
                  uint8_t rem_h = remaining / 3600000;
                  remaining %= 3600000;
                  uint8_t rem_m = remaining / 60000;
                  remaining %= 60000;
                  uint8_t rem_s = remaining / 1000;
                  
                  oled.print(rem_h < 10 ? "0" : ""); oled.print(rem_h);
                  oled.print(":");
                  oled.print(rem_m < 10 ? "0" : ""); oled.print(rem_m);
                  oled.print(":");
                  oled.print(rem_s < 10 ? "0" : ""); oled.print(rem_s);
                  break;
              }
                  
              case ALARM:
                  if(blinkState) {
                      oled.print("  ВРЕМЯ!");
                  } else {
                      oled.print("        ");
                  }
                  break;
          }
          
          oled.setScale(1);
          oled.setCursor(0, 5);
          oled.print("Статус: ");
          switch(state) {
              case SET_HOURS: 
                  oled.print("Уст. часы    "); 
                  break;
              case SET_MINS:  
                  oled.print("Уст. минуты  "); 
                  break;
              case SET_SECS:  
                  if(hours == 0 && mins == 0 && secs == 0) {
                      oled.print("Уст. время!  "); 
                  } else {
                      oled.print("Уст. секунды "); 
                  }
                  break;
              case RUNNING:   
                  oled.print("Работает     "); 
                  break;
              case ALARM:     
                  oled.print("Сработал!    "); 
                  break;
          }
          
          oled.setCursor(0, 7);
          switch(state) {
              case SET_HOURS: 
                  oled.print("+->   -<-    OK->"); 
                  break;
              case SET_MINS:  
                  oled.print("+->   -<-    OK->"); 
                  break;
              case SET_SECS:  
                  if(hours == 0 && mins == 0 && secs == 0) {
                      oled.print("+->   -<- УСТ.ВР!"); 
                  } else {
                      oled.print("+->   -<-   СТАРТ"); 
                  }
                  break;
              case RUNNING:   
                  oled.print("             СТОП"); 
                  break;
              case ALARM:     
                  oled.print("           ОК-СБРОС"); 
                  break;
          }
          
          if(state == ALARM && blinkState) {
              oled.invertDisplay(true);
          } else {
              oled.invertDisplay(false);
          }
          
          oled.update();
      }
      
      if(ok.isHold() && state != ALARM) {
          oled.invertDisplay(false);
          Wire.setClock(100000);
          exit();
          return;
      }
      
      delay(10);
  }
}
void stopwatch() {
  bool running = false;
  bool redraw = true;
  unsigned long startTime = 0;
  unsigned long elapsedTime = 0;
  unsigned long lastDraw = 0;
  
  reset_buttons();
  
  oled.clear();
  ui_rama("Секундомер", true, true, false);
  
  oled.setCursor(0, 7);
  oled.print("СТАРТ->  СБРОС->");
  oled.update();
  
  //цикл
  while(1) {
      buttons_tick();
      
      if(ok.isClick()) {
          running = !running;
          if(running) {
              startTime = millis() - elapsedTime;
          }
          redraw = true;
      }
      
      if(right.isClick()) { 
          elapsedTime = 0;
          running = false;
          redraw = true;
      }
      
      if(running) {
          elapsedTime = millis() - startTime;
          
          if(millis() - lastDraw >= 100) {
              redraw = true;
          }
      }
      
      if(redraw) {
          redraw = false;
          lastDraw = millis();
          
          oled.setCursor(0, 2);
          oled.print("                    ");
          oled.setCursor(0, 3);
          oled.print("                    ");
          oled.setCursor(0, 4);
          oled.print("                    ");
          oled.setCursor(0, 5);
          oled.print("                    ");
          
          unsigned long hours = elapsedTime / 3600000;
          unsigned long mins = (elapsedTime / 60000) % 60;
          unsigned long secs = (elapsedTime / 1000) % 60;
          unsigned long ms = elapsedTime % 1000;
          
          oled.setCursor(10, 3);
          oled.setScale(2);
          
          if(hours > 0) {
              oled.print(hours < 10 ? "0" : ""); 
              oled.print(hours);
              oled.print(":");
          }
          
          oled.print(mins < 10 ? "0" : ""); 
          oled.print(mins);
          oled.print(":");
          oled.print(secs < 10 ? "0" : ""); 
          oled.print(secs);
          oled.print(".");
          oled.print(ms / 100);
          
          oled.setScale(1);
          oled.setCursor(0, 6);
          oled.print("Статус: ");
          oled.print(running ? "РАБОТА" : "ПАУЗА ");
          
          oled.update();
      }
      
      if(ok.isHold()) {
          Wire.setClock(100000);
          exit();
          return;
      }
      
      delay(10);
  }
}
int getFilesCount() {
  File root = LittleFS.open("/");
  int count = 0;
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    if (filename.endsWith(".txt") || filename.endsWith(".h")) {
      count++;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  return count;
}

String getFilenameByIndex(int idx) {
  File root = LittleFS.open("/");
  int i = 0;
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    if (filename.endsWith(".txt") || filename.endsWith(".h")) {
      if (i == idx) {
        String name = "/" + filename;
        file.close();
        root.close();
        return name;
      }
      i++;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  return "";
}
void update_cursor() {
  for (uint8_t i = 0; i < 6 && i < files; i++) {    // Проходимся от 2й до 8й строки оледа
    oled.setCursor(0, i + 2);                       // Ставим курсор на нужную строку
    oled.print(getFilenameByIndex(cursor < 6 ? i : i + (cursor - 5)));  // Выводим имя файла
  }
  oled.setCursor(0, constrain(cursor, 0, 5) + 2); oled.print(" ");      // Чистим место под указатель
  oled.setCursor(0, constrain(cursor, 0, 5) + 2); oled.print(">");      // Выводим указатель на нужной строке
  oled.update();                                    // Выводим картинку
}
bool drawMainMenu(void) {                           // Отрисовка главного меню
  oled.clear();                                     // Очистка
  oled.home();                                      // Возврат на 0,0
  oled.line(0, 10, 127, 10);                        // Линия
  if (files == 0) {
    oled.clear();
    oled.setScale(2);
    oled.setCursorXY(34, 16);
    oled.print(F("файлов"));
    oled.setCursorXY(22, 32);
    oled.print(F("нету :("));
    oled.setScale(1);
    oled.update();
    delay(1500);
    return false;
  }
  oled.print("НАЙДЕННЫЕ ФАЙЛЫ: "); oled.print(files);   // Выводим кол-во файлов
  update_cursor();
  return true;
}
/* ======================================================================= */
/* ============================ Чтение файла ============================= */
uint8_t parseHFile(uint8_t *img, File &file) {
  int imgLen = 0;
  memset(img, 0, 1024); // Очистка буфера

  // Пропускаем все символы до '{'
  while (file.available()) {
    if (file.read() == '{') break;
  }

  // Читаем данные до '}' или конца файла
  while (file.available() && imgLen < 1024) {
    char c = file.read();
    if (c == '}') break; // Конец данных
    
    // Парсим HEX-значения вида 0xXX
    if (c == '0' && file.peek() == 'x') {
      file.read(); // Пропускаем 'x'
      char hex[3] = {0};
      hex[0] = file.read();
      hex[1] = file.read();
      img[imgLen++] = strtoul(hex, NULL, 16); // Конвертируем HEX в байт
    }
    yield(); // Для стабильности ESP
  }

  return (imgLen == 1024) ? 0 : 1; // 0 = успех, 1 = ошибка
}
void enterToReadBmpFile(String filename) {
  File file = LittleFS.open(filename, "r");
  if (!file) {
    files = getFilesCount();
    drawMainMenu();
    file.close();
    return;
  }

  uint8_t *img = new uint8_t[1024]; // 128x64 бит = 1024 байт
  if (parseHFile(img, file)) {
    delete[] img;
    file.close();
    files = getFilesCount();
    drawMainMenu();
    return;
  }

  oled.clear();
  oled.drawBitmap(0, 0, img, 128, 64);
  oled.update();
  delete[] img;
  setCpuFrequencyMhz(80);
  while (true) {
    ok.tick();
    if (ok.isClick()) {
      setCpuFrequencyMhz(240);
      file.close();
      files = getFilesCount();
      drawMainMenu();
      setCpuFrequencyMhz(80);
      return;
    }
    yield();
  }
}

void drawPage(File &file, bool storeHistory = true) {
  if(storeHistory) {
    currentHistoryIndex = (currentHistoryIndex + 1) % MAX_PAGE_HISTORY;
    pageHistory[currentHistoryIndex] = file.position();
    totalPages = min(totalPages + 1, MAX_PAGE_HISTORY);
  }
  oled.clear();
  oled.home();
  
  const uint8_t maxLineLength = 36;
  uint8_t currentLine = 0;
  String buffer = "";
  String word = "";
  uint8_t spaceLeft = maxLineLength;

  while (file.available() && currentLine < 8) {
    char c = file.read();
    
    if (c == '\n' || c == '\r') {
      if (!buffer.isEmpty()) {
        oled.println(buffer);
        currentLine++;
        buffer = "";
        spaceLeft = maxLineLength;
      }
      continue;
    }

    if (c == ' ') {
      if (buffer.length() + word.length() + 1 <= maxLineLength) {
        buffer += word;
        buffer += ' ';
        spaceLeft = maxLineLength - buffer.length();
        word = "";
      } else {
        oled.println(buffer);
        currentLine++;
        buffer = word + ' ';
        spaceLeft = maxLineLength - buffer.length();
        word = "";
      }
      continue;
    }

    word += c;

    if (word.length() > spaceLeft) {
      String part = word.substring(0, spaceLeft);
      buffer += part;
      oled.println(buffer);
      currentLine++;
      
      word = word.substring(spaceLeft);
      buffer = "";
      spaceLeft = maxLineLength;
      
      if (currentLine >= 8) break;
    }
  }
  if (!buffer.isEmpty() && currentLine < 8) {
    oled.println(buffer);
    currentLine++;
  }
  if (!word.isEmpty() && currentLine < 8) {
    oled.println(word);
  }

  oled.update();
}
void enterToReadTxtFile(String filename){
  File file = LittleFS.open(getFilenameByIndex(cursor), "r"); // Чтение имени файла по положению курсора и открытие файла
  if (!file) {                                      // Если сам файл не порядке
    files = getFilesCount(); drawMainMenu();        // Смотрим количество файлов и рисуем главное меню
    file.close(); return;                           // Закрываем файл и выходим
  }
  memset(pageHistory, 0, sizeof(pageHistory));
  currentHistoryIndex = -1;
  totalPages = 0;

  drawPage(file);                                   // Если с файлом все ок - рисуем первую страницу
  setCpuFrequencyMhz(80);
  while (1) {                                       // Бесконечный цикл
    right.tick();                                      // Опрос кнопок
    ok.tick();
    left.tick();
    if (ok.isClick()) {                             // Если ок нажат
      setCpuFrequencyMhz(240);
      files = getFilesCount(); drawMainMenu();      // Смотрим количество файлов и рисуем главное меню
      file.close();
      setCpuFrequencyMhz(80);
      return;                         // Закрываем файл и выходим
    } else if (right.isClick() or right.isHold()) {       // Если нажата или удержана вверх
      if(totalPages > 0) {
        totalPages--;
        currentHistoryIndex = (currentHistoryIndex - 1 + MAX_PAGE_HISTORY) % MAX_PAGE_HISTORY;
        file.seek(pageHistory[currentHistoryIndex]);
        drawPage(file, false);                      // Не сохраняем в историю
      }                                             // Устанавливаем указатель файла
    } else if (left.isClick() or left.isHold()) {   // Если нажата или удержана вниз
      drawPage(file);                               // Рисуем страницу
    }
    yield();                                        // Внутренний поллинг ESP
  }
}
void enterToReadFile(void) { 
  setCpuFrequencyMhz(240);
  String filename = getFilenameByIndex(cursor);
  if (filename.endsWith(".h")) {
    enterToReadBmpFile(filename);
  } else if(filename.endsWith(".txt")) {
    // Вызов существующей функции для текстовых файлов
    enterToReadTxtFile(filename);
  } else {
  }                
}
// TODO: сделано норм не лагающее меню.
void ShowFilesLittleFS() {
  oled.autoPrintln(false);
  setCpuFrequencyMhz(240);
  files = getFilesCount();                    // Читаем количество файлов
  if (drawMainMenu() == false){               // Рисуем главное меню
    exit();
    setCpuFrequencyMhz(80);
    return;
  }        
  setCpuFrequencyMhz(80);          
  while (true)
  {
    buttons_tick();                                     // Опрос кнопок
    static uint32_t timer = 0;                          // таймер
    if (right.isClick() || (right.isHold() && millis() - timer > 50)) {                // Если нажата или удержана кнопка вверх
      setCpuFrequencyMhz(240);
      cursor = constrain(cursor - 1, 0, files - 1);   // Двигаем курсор
      timer = millis();
      update_cursor();    
      setCpuFrequencyMhz(80);
    } else if (left.isClick() || (left.isHold() && millis() - timer > 50)) {       // Если нажата или удержана кнопка вниз
      setCpuFrequencyMhz(240);
      cursor = constrain(cursor + 1, 0, files - 1);   // Двигаем курсор
      timer = millis();
      update_cursor();                                 // Обновляем главное меню
      setCpuFrequencyMhz(80);
    } else if (ok.isHold()) {                         // Если удержана ОК
      exit();                                         // Выход                        
      return;                                         // Выход
    } else if (ok.isClick()) {                        // Если нажата ОК
      enterToReadFile();                              // Переходим к чтению файла
    }
  }
  
}
//void navigate_menu(Menu* menu) {
//  currentMenu = menu;
//  pointer = 0;
//  top_item = 0;
//  
//  drawMenu(menu);
//  updatePointer();
//  
//  uint8_t last_minute = rtc.getTime().minute;
//  
//  while (true) {
//    static uint32_t timer = 0;
//    buttons_tick();
//
//    Datime current_time = rtc.getTime();
//    if (current_time.minute != last_minute) {
//      update_header_time();
//      last_minute = current_time.minute;
//    }
//
//    if (right.isClick() || (right.isHold() && millis() - timer > 150)) {
//      if (pointer > 0) {
//        pointer--;
//        if (pointer < top_item) top_item--;
//      }
//      timer = millis();
//      updatePointer();
//    }
//
//    if (left.isClick() || (left.isHold() && millis() - timer > 150)) {
//      if (pointer < menu->itemCount - 1) {
//        pointer++;
//        if (pointer >= top_item + VISIBLE_ITEMS) top_item++;
//      }
//      timer = millis();
//      updatePointer();
//    }
//    if (ok.isHold()) {
//      exit();
//      return;
//    } 
//    if (ok.isClick()) {
//      MenuItem* selectedItem = &menu->items[pointer];
//      
//      if (selectedItem->action != nullptr) {
//        previousMenu = menu;
//        selectedItem->action();
//        
//        if (currentMenu == menu) {
//          drawMenu(menu);
//          updatePointer();
//        }
//      } else {
//        exit();
//        return;
//      }
//    }
//  }
//}
//
//void main_menu() {
//  navigate_menu(&mainMenu);
//}
//
//void games_menu() {
//  navigate_menu(&gamesMenu);
//}
//
//void select_wifi_mode() {
//  navigate_menu(&selectWifiMode);
//}
//
//void utilities_menu() {
//  navigate_menu(&utilitiesMenu);
//}
#include "menu_data.h"

// тут можно настроить скорость перелистывания
#define ANIMATION_SPEED 5



int getRealStringLength(const char* str) {
    int len = 0;
    while (*str) {
        if ((*str & 0xC0) != 0x80) len++;
        str++;
    }
    return len;
}

// рисование статики
void drawMenuStatic(GraphMenu* menu) {
    Datime dt = rtc.getTime();
    
    oled.setScale(1);
    oled.setCursorXY(3, 2);
    oled.print(menu->title);
    oled.line(menu->titleEndPos, 1, menu->titleEndPos - 5, 10, OLED_STROKE);
    oled.fastLineH(10, 1, menu->titleEndPos - 5, OLED_STROKE);

    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", dt.hour, dt.minute);
    oled.setCursorXY(95, 2);
    oled.print(timeStr);
    oled.fastLineH(10, 94, 128, OLED_STROKE);
    oled.line(89, 1, 94, 10, OLED_STROKE);
}

void drawWidget(int x, const char* name, const uint8_t* icon, bool isCenter) {
    if (x < -40 || x > 140) return;

    int dist = abs(x - 48);
    int y_offset = (dist * dist) / 350; 
    int y = 16 + y_offset; 

    oled.roundRect(x, y, x + 31, y + 31, OLED_STROKE);
    
    if (icon != nullptr) {
        oled.drawBitmap(x + 4, y + 4, icon, 24, 24);
    } else {
        oled.drawBitmap(x + 4, y + 4, unkown_file_24x24, 24, 24);
    }

    if (isCenter && name != nullptr && dist < 12) {
        oled.setScale(1);
        int textLen = getRealStringLength(name);
        int textWidth = textLen * 6;
        int textX = x + (32 - textWidth) / 2; 
        if (textX < 0) textX = 0;
        
        oled.setCursorXY(textX, y + 34); 
        oled.print(name);
    }
}

void navigate_graphical_menu(GraphMenu* menu) {
    int currentGraphItemIndex = 0; 
    
    bool needRedraw = true;
    uint8_t last_minute = rtc.getTime().minute;
    
    reset_buttons();

    while (true) {
        buttons_tick();

        Datime dt = rtc.getTime();
        if (dt.minute != last_minute) { last_minute = dt.minute; needRedraw = true; }

        if (needRedraw) {
            needRedraw = false;
            oled.clear();
            
            //фон
            drawMenuStatic(menu);

            //иконки и текст
            int total = menu->itemCount;
            if (total == 0) return; 
            int prev = (currentGraphItemIndex - 1 + total) % total;
            int next = (currentGraphItemIndex + 1) % total;

            drawWidget(8, nullptr, menu->items[prev].icon, false);
            drawWidget(48, menu->items[currentGraphItemIndex].name, menu->items[currentGraphItemIndex].icon, true);
            drawWidget(88, nullptr, menu->items[next].icon, false);
            
            //рамка
            oled.rect(0, 0, 127, 63, OLED_STROKE);

            oled.update();
        }

        if (left.isClick() || left.isHold()) {
            setCpuFrequencyMhz(160); 
            Wire.setClock(800000L); 
            
            int step = ANIMATION_SPEED;
            int total = menu->itemCount;
            int idx = currentGraphItemIndex;
            int nextIdx = (idx + 1) % total;
            int nextNextIdx = (idx + 2) % total;
            int prevIdx = (idx - 1 + total) % total;

            for (int offset = 0; offset <= 40; offset += step) {
                oled.clear();
                drawMenuStatic(menu); // фон
                
                // иконки и текст
                drawWidget(8 - offset, nullptr, menu->items[prevIdx].icon, false);
                drawWidget(48 - offset, nullptr, menu->items[idx].icon, offset < 8); 
                drawWidget(88 - offset, nullptr, menu->items[nextIdx].icon, false);
                drawWidget(128 - offset, nullptr, menu->items[nextNextIdx].icon, false);
                
                // рамка
                oled.rect(0, 0, 127, 63, OLED_STROKE);
                
                oled.update();
            }

            currentGraphItemIndex = nextIdx;
            needRedraw = true;
            
            Wire.setClock(400000L); 
            setCpuFrequencyMhz(80); 
            
            if (right.isHold()) delay(50); 
        }

        if (right.isClick() || right.isHold()) {
            setCpuFrequencyMhz(160); 
            Wire.setClock(800000L); 
            
            int step = ANIMATION_SPEED;
            int total = menu->itemCount;
            int idx = currentGraphItemIndex;
            int prevIdx = (idx - 1 + total) % total;
            int prevPrevIdx = (idx - 2 + total) % total;
            int nextIdx = (idx + 1) % total;

            for (int offset = 0; offset <= 40; offset += step) {
                oled.clear();
                drawMenuStatic(menu); // фон
                
                // иконки и текст
                drawWidget(-32 + offset, nullptr, menu->items[prevPrevIdx].icon, false);
                drawWidget(8 + offset, nullptr, menu->items[prevIdx].icon, false);
                drawWidget(48 + offset, nullptr, menu->items[idx].icon, offset < 8);
                drawWidget(88 + offset, nullptr, menu->items[nextIdx].icon, false);
                
                // рамка
                oled.rect(0, 0, 127, 63, OLED_STROKE);
                
                oled.update();
            }

            currentGraphItemIndex = prevIdx;
            needRedraw = true;
            
            Wire.setClock(400000L);
            setCpuFrequencyMhz(80);
            
             if (left.isHold()) delay(50);
        }


        if (ok.isClick()) {
            if (menu->items[currentGraphItemIndex].action != nullptr) {
                menu->items[currentGraphItemIndex].action();
                needRedraw = true;
                reset_buttons(); 
            } else {
                exit(); return; 
            }
        }
        if (ok.isHold()) { exit(); return; }
        
        yield(); 
    }
}

//(костыли) функции запуска
void open_graphical_games() { navigate_graphical_menu(&data_GamesMenu); }
void open_graphical_wifi() { navigate_graphical_menu(&data_WifiMenu); }
void open_graphical_utils() { navigate_graphical_menu(&data_UtilsMenu); }
void open_graphical_main() { navigate_graphical_menu(&data_MainMenu); }

void drawRoundWatchFace() {
  oled.clear();
  
  Datime dt = rtc.getTime();
  
  oled.circle(64, 32, 30, OLED_STROKE);
  oled.circle(64, 32, 31, OLED_STROKE);
  
  for (int i = 0; i < 12; i++) {
    float angle = i * 30 * PI / 180;
    int x1 = 64 + 25 * sin(angle);
    int y1 = 32 - 25 * cos(angle);
    int x2 = 64 + 30 * sin(angle);
    int y2 = 32 - 30 * cos(angle);
    oled.line(x1, y1, x2, y2, 1);
  }
  
  for (int i = 0; i < 60; i++) {
    if (i % 5 != 0) {
      float angle = i * 6 * PI / 180;
      int x1 = 64 + 27 * sin(angle);
      int y1 = 32 - 27 * cos(angle);
      int x2 = 64 + 30 * sin(angle);
      int y2 = 32 - 30 * cos(angle);
      oled.line(x1, y1, x2, y2, 1);
    }
  }
  
  uint8_t hour12 = dt.hour % 12;
  if (hour12 == 0) hour12 = 12;
  
  float hourAngle = (hour12 + dt.minute / 60.0) * 30 * PI / 180;
  float minuteAngle = (dt.minute + dt.second / 60.0) * 6 * PI / 180;
  float secondAngle = dt.second * 6 * PI / 180;
  
  int hourX = 64 + 12 * sin(hourAngle);
  int hourY = 32 - 12 * cos(hourAngle);
  oled.line(64, 32, hourX, hourY, 1);
  
  int minuteX = 64 + 18 * sin(minuteAngle);
  int minuteY = 32 - 18 * cos(minuteAngle);
  oled.line(64, 32, minuteX, minuteY, 1);
  
  int secondX = 64 + 25 * sin(secondAngle);
  int secondY = 32 - 25 * cos(secondAngle);
  oled.line(64, 32, secondX, secondY, 1);
  
  oled.circle(64, 32, 3, OLED_FILL);
  oled.circle(64, 32, 1, OLED_CLEAR);
  
  oled.setScale(1);
  oled.setCursor(0, 7);
  if (dt.day < 10) oled.print("0");
  oled.print(dt.day);
  oled.print(" ");
  oled.print(months[dt.month - 1]);
  
  if (alarmActive) {
    oled.drawBitmap(110, 50, alarm_icon_big, 16, 16);
  }
  
  oled.update();
}
void drawStandardWatchFace() {
  oled.clear();
  
  Datime dt = rtc.getTime();
  
  oled.setScale(3);
  oled.setCursorXY(18, 20);
  
  if (dt.hour < 10) oled.print("0");
  oled.print(dt.hour);
  
  if (dotsVisible) {
    oled.print(":");
  } else {
    oled.print(" ");
  }
  
  if (dt.minute < 10) oled.print("0");
  oled.print(dt.minute);
  
  // дата
  oled.setScale(1);
  oled.setCursor(25, 7);
  
  if (dt.day < 10) oled.print("0");
  oled.print(dt.day);
  oled.print(" ");
  oled.print(months[dt.month - 1]);
  oled.print(" ");
  oled.print(dt.year);
  
  // значок будильника
  if (alarmActive) {
    oled.drawBitmap(110, 50, alarm_icon_big, 16, 16);
  }
  
  oled.update();
}
void drawWatchFace() {
  
  if (watchfaceStyle == 0) {
    drawStandardWatchFace();
  } else {
    drawRoundWatchFace();
  }
}


void updatePetState() {
  uint32_t currentTime = timeToUnix(rtc.getTime());
  uint32_t timePassed = currentTime - catOsGotchi.lastUpdate;
  
  if (timePassed > 7200) {
    int hoursPassed = timePassed / 7200;
    catOsGotchi.hunger = max(0, catOsGotchi.hunger - hoursPassed);
    catOsGotchi.happiness = max(0, catOsGotchi.happiness - hoursPassed);
    catOsGotchi.lastUpdate = currentTime;
    savePetState();
  }
}

//получение битмапа для котика
const uint8_t* getCurrentCatBitmap() {
  // получаем дату
  Datime dt = rtc.getTime();
  
  bool isChristmasTime = (dt.month == 12 && dt.day >= 15) || (dt.month == 1 && dt.day <= 18);

  if (catOsGotchi.hunger < 20 || catOsGotchi.happiness < 20) {
    //очень злой
    return isChristmasTime ? very_angry_cat_c : very_angry_cat_44x53;
  } else if (catOsGotchi.hunger < 40) {
    //злой
    return isChristmasTime ? angry_cat_c : angry_cat_44x53;
  } else if (catOsGotchi.happiness < 40) {
    //плачет
    return isChristmasTime ? crying_cat_c : crying_cat_44x53;
  } else if (catOsGotchi.hunger < 60 || catOsGotchi.happiness < 60) {
    //грустный
    return isChristmasTime ? sad_cat_c : sad_cat_44x53;
  } else {
    //нормальный
    return isChristmasTime ? normal_cat_c : normal_cat_44x53;
  }
}

// проверка, нужно ли показывать иконку на циферблате
bool shouldShowPetIcon() {
  uint32_t currentTime = timeToUnix(rtc.getTime());
  uint32_t timeSinceLastVisit = currentTime - catOsGotchi.lastVisit;
  return (timeSinceLastVisit > 2592000);
}
void pet_menu() {
  catOsGotchi.lastVisit = timeToUnix(rtc.getTime());
  savePetState();
  
  uint8_t selected = 0;
  bool needRedraw = true;
  bool inActionMenu = false;
  
  reset_buttons();
  
  while (true) {
    buttons_tick();
    updatePetState();
    
    if (needRedraw) {
      needRedraw = false;
      
      oled.clear();
      String title = catOsGotchi.name;
      if (title.length() > 20) {
        title = title.substring(0, 20) + "..."; 
      }
      ui_rama(title.c_str(), true, true, false, false);
      
      // рисуем котика
      const uint8_t* currentBitmap = getCurrentCatBitmap();
      oled.drawBitmap(0, 12, currentBitmap, 44, 53);
      
      if (!inActionMenu) {
        oled.setCursor(50, 2);
        oled.print("Состояние:");
        
        oled.setCursor(50, 3);
        oled.print("Голод: ");
        oled.print(catOsGotchi.hunger);
        oled.print("%");
        oled.setCursor(50, 4);
        oled.print("[");
        int hungerBars = map(catOsGotchi.hunger, 0, 100, 0, 11);
        for (int i = 0; i < 11; i++) {
          oled.print(i < hungerBars ? "=" : " ");
        }
        oled.print("]");
        
        oled.setCursor(50, 5);
        oled.print("Счастье:");
        oled.print(catOsGotchi.happiness);
        oled.print("%");
        oled.setCursor(50, 6);
        oled.print("[");
        int happyBars = map(catOsGotchi.happiness, 0, 100, 0, 11);
        for (int i = 0; i < 11; i++) {
          oled.print(i < happyBars ? "=" : " ");
        }
        oled.print("]");
        
        oled.setCursor(50, 7);
        oled.print("OK-действия");
      } else {
        oled.setCursor(50, 2);
        oled.print("Выберите действие:");
        
        oled.setCursor(52, 4);
        if (selected == 0) oled.print(">");
        oled.print("Покормить");
        
        oled.setCursor(52, 5);
        if (selected == 1) oled.print(">");
        oled.print("Поиграть");
        
        oled.setCursor(50, 7);
        oled.print("OK-выбор Назад->");
      }
      
      oled.update();
    }
    
    if (!inActionMenu) {
      if (ok.isClick()) {
        inActionMenu = true;
        selected = 0;
        needRedraw = true;
      }
    } else {
      if (right.isClick()) {
        selected = (selected + 1) % 2;
        needRedraw = true;
      }
      
      if (left.isClick()) {
        selected = (selected == 0) ? 1 : 0;
        needRedraw = true;
      }
      
      if (ok.isClick()) {
        if (selected == 0) {
          catOsGotchi.hunger = min(100, catOsGotchi.hunger + 30);
          catOsGotchi.happiness = min(100, catOsGotchi.happiness + 10);
          savePetState();
          
          oled.setCursor(50, 7);
          oled.print("Ням-ням!       ");
          oled.update();
          delay(1000);
          
        } else {
          catOsGotchi.happiness = min(100, catOsGotchi.happiness + 30);
          catOsGotchi.hunger = max(0, catOsGotchi.hunger - 5);
          savePetState();
          
          oled.setCursor(50, 7);
          oled.print("Играем с котом!");
          oled.update();
          delay(1000);
        }
        
        inActionMenu = false;
        needRedraw = true;
      }
    }
    
    if (ok.isHold()) {
      if (inActionMenu) {
        inActionMenu = false;
        needRedraw = true;
      } else {
        exit();
        return;
      }
    }
    
    delay(50);
  }
}
void air_mouse_app() {
  reset_buttons();

  // кфг
  const int8_t i2c_addr = 0x69;
  const float DIVIDER = 35.0; 
  const int DEADZONE = 10; 
  const float SMOOTH = 0.3; 
  const int DIR_X = -1; 
  const int DIR_Y = -1; 
  const unsigned long MOVE_DELAY = 150; 

  // чекаем
  bool sensorFound = true;
  if (bmi160.softReset() != BMI160_OK) sensorFound = false;
  if (bmi160.I2cInit(i2c_addr) != BMI160_OK) sensorFound = false;

  if (!sensorFound) {
       oled.clear(); ui_rama("Air Mouse", true, true, false);
       oled.setScale(2); oled.setCursor(10, 3); oled.print("Err");
       oled.update(); delay(2000); return;
  }

  bleMouse.begin(); 
  
  int16_t data[6] = {0}; 
  float valX = 0, valY = 0; 
  
  bool isConnectedOld = false;
  bool needRedraw = true;
  bool leftPressed = false;
  bool rightPressed = false;
  bool isActive = false;          
  unsigned long btnPressTime = 0; 
  uint8_t waitAnim = 0;

  oled.clear(); oled.update();

  while (true) {
    buttons_tick();
    PWR.tick(); 

    if (ok.isHold()) {
      oled.clear();
      ui_rama("Выход...", true, true, false);
      oled.update();
      
      bleMouse.end(); 
      delay(100);
      
      exit();
      return;
    }

    // конект
    bool isConnected = bleMouse.isConnected();
    if (isConnected != isConnectedOld) {
        isConnectedOld = isConnected;
        needRedraw = true;
    }
    //потом переименую кнопку
    if (PWR.isDouble() && isConnected) {
        bleMouse.press(MOUSE_RIGHT);
        delay(30); 
        bleMouse.release(MOUSE_RIGHT);
        
        oled.rect(64, 14, 64, 50, OLED_FILL); oled.update();
        delay(100);
        needRedraw = true;
        isActive = false; valX = 0; valY = 0;
    }

    if (PWR.state()) {
        if (btnPressTime == 0) btnPressTime = millis();
        if (millis() - btnPressTime > MOVE_DELAY) {
            if (!isActive) {
                isActive = true;
                needRedraw = true;
                valX = 0; valY = 0;
            }
        }
    } else {
        btnPressTime = 0;
        if (isActive) {
            isActive = false;
            needRedraw = true;
        }
    }

    if (needRedraw || (!isConnected && millis() % 500 == 0)) {
        oled.clear();
        ui_rama("Air Mouse", true, true, false);
        
        if (!isConnected) {
             oled.setCursor(55, 5);
             if (waitAnim == 0) oled.print(".  ");
             if (waitAnim == 1) oled.print(".. ");
             if (waitAnim == 2) oled.print("...");
             waitAnim++; if (waitAnim > 2) waitAnim = 0;
             needRedraw = false; 
        } else {
            if (isActive) {
                oled.roundRect(15, 20, 113, 45, OLED_FILL);
                oled.invertText(true);
                oled.setScale(1);
                oled.invertText(false);
            } else {
                oled.roundRect(15, 20, 113, 45, OLED_STROKE);
            }

            oled.setCursor(0, 7); oled.print("ЛКМ");
            oled.setCursor(108, 7); oled.print("ПКМ");
            
            if (leftPressed) oled.rect(0, 14, 15, 64, OLED_FILL);
            if (rightPressed) oled.rect(113, 14, 128, 64, OLED_FILL);
            
            needRedraw = false;
        }
        oled.update();
    }

    if (isConnected) {
        if (isActive) {
            int rslt = bmi160.getAccelGyroData(data);
            if (rslt == 0) {
                float rawX = data[2]; 
                float rawY = data[0]; 

                if (abs(rawX) < DEADZONE) rawX = 0;
                if (abs(rawY) < DEADZONE) rawY = 0;

                valX = (valX * (1.0 - SMOOTH)) + (rawX * SMOOTH);
                valY = (valY * (1.0 - SMOOTH)) + (rawY * SMOOTH);

                int moveX = (int)((valX / DIVIDER) * DIR_X);
                int moveY = (int)((valY / DIVIDER) * DIR_Y);
                
                moveX = constrain(moveX, -100, 100);
                moveY = constrain(moveY, -100, 100);

                if (moveX != 0 || moveY != 0) {
                    bleMouse.move(moveX, moveY);
                }
            }
        }

        if (left.state() && !leftPressed) {
            bleMouse.press(MOUSE_LEFT);
            leftPressed = true;
            needRedraw = true;
        } 
        else if (!left.state() && leftPressed) {
            bleMouse.release(MOUSE_LEFT);
            leftPressed = false;
            needRedraw = true;
        }

        if (right.state() && !rightPressed) {
            bleMouse.press(MOUSE_RIGHT);
            rightPressed = true;
            needRedraw = true;
        } 
        else if (!right.state() && rightPressed) {
            bleMouse.release(MOUSE_RIGHT);
            rightPressed = false;
            needRedraw = true;
        }
    }
    
    delay(10); 
  }
}
void battery_info() {
  reset_buttons();
  
  bool needRedraw = true;
  uint32_t lastCheck = 0;
  
  float oldVoltage = -1.0;
  float oldPercent = -1.0;

  oled.clear();
  FuelGauge.clearAlert(); 
  
  while (true) {
    buttons_tick();
    
    if (ok.isClick() || ok.isHold()) {
      exit();
      return;
    }

    if (millis() - lastCheck > 1000 || lastCheck == 0) {
        lastCheck = millis();
        
        float voltage = FuelGauge.voltage() / 1000.0; 
        float percent = FuelGauge.percent();
        
        if (abs(voltage - oldVoltage) > 0.01 || abs(percent - oldPercent) > 0.5) {
            oldVoltage = voltage;
            oldPercent = percent;
            needRedraw = true;
        }
    }
    
    if (needRedraw) {
        needRedraw = false;
        oled.clear();
        ui_rama("Батарея", true, true, false);
        
        oled.roundRect(4, 20, 54, 50, OLED_STROKE); 
        oled.rect(0, 27, 4, 31, OLED_FILL);
        oled.rect(0, 39, 4, 43, OLED_FILL);
        
        oled.line(18, 28, 14, 33, OLED_STROKE); 
        oled.line(14, 33, 18, 33, OLED_STROKE); 
        oled.line(18, 33, 14, 38, OLED_STROKE);

        oled.line(40, 28, 36, 33, OLED_STROKE); 
        oled.line(36, 33, 40, 33, OLED_STROKE); 
        oled.line(40, 33, 36, 38, OLED_STROKE);
        
        if (oldPercent > 20) {
            oled.line(22, 43, 24, 45, OLED_STROKE); 
            oled.line(24, 45, 30, 45, OLED_STROKE); 
            oled.line(30, 45, 32, 43, OLED_STROKE);
        } else {
            oled.line(22, 45, 24, 43, OLED_STROKE); 
            oled.line(24, 43, 30, 43, OLED_STROKE); 
            oled.line(30, 43, 32, 45, OLED_STROKE);
        }

        // инфа
        oled.roundRect(60, 15, 127, 48, OLED_STROKE);
        oled.line(54, 30, 60, 25, OLED_STROKE);
        oled.line(54, 30, 60, 35, OLED_STROKE);
        oled.line(60, 26, 60, 34, OLED_CLEAR); 
        
        oled.setScale(1);
        
        oled.setCursorXY(64, 19); 
        if (oldPercent >= 90) oled.print("Full!");
        else if (oldPercent >= 50) oled.print("Yummy!");
        else if (oldPercent >= 20) oled.print("Hungry!");
        else oled.print("Feed me!");
        
        oled.setCursorXY(64, 35);
        oled.print("Lvl:");
        
        oled.setScale(2);
        oled.setCursorXY(88, 30);
        oled.print((int)oldPercent); 
        
        oled.setScale(1);
        oled.fastLineH(54, 0, 127, OLED_STROKE);
        
        oled.setCursor(0, 7);
        oled.print(oldVoltage, 2); oled.print("V");
        
        oled.setCursor(64, 7);
        if (FuelGauge.alertIsActive() || oldPercent < 5) { 
             oled.print("LOW BAT!"); 
        } else {
             oled.print("Status: OK");
        }

        oled.update();
    }
    
    delay(20);
  }
}
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(2000); 
  
  Serial.println("boot");
  Serial.println("catos" FIRMWARE_VERSION);
  #ifdef CATOS_S3_BUILD
    Serial.println("[log] s3 build");
  #else
    Serial.println("[log] c3 build");
  #endif
  Serial.println("[log] conf. pins");
  
  #ifdef CATOS_S3_BUILD
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000); 
  #else
    Serial.println("[log] start wire c3");
    Wire.begin(); 
  #endif
  
  delay(100);
  Serial.println("[log] init MAX17043");
  if (FuelGauge.begin()) {
    FuelGauge.quickstart();
    Serial.println("[log] MAX17043 found");
  } else {
    Serial.println("[log] MAX17043 not found");
  }
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() != 0) {
      Serial.println("[log] oled not found");
      Wire.end();
      for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, HIGH);
        delay(100);
        digitalWrite(PIN_LED, LOW);
        delay(100);
      }
      #ifdef CATOS_S3_BUILD
        Wire.begin(PIN_SDA, PIN_SCL);
      #else
        Wire.begin();
      #endif
  } else {
      Serial.println("[log] oled found");
  }
  oled.init();
  
  
  Serial.println("[log] start!");
  rtc.begin();

  if (rtc.isReset()) {
    rtc.setBuildTime();
  }
  LittleFS.begin(true);
  db.begin();  //читаем дб
  //инициализируем значения
  initSettings();
  loadPetState();
  updatePetState();
  //применяем
  oled.setContrast(db[kk::OLED_BRIGHTNESS].toInt());
  watchfaceStyle = db[kk::WATCHFACE_STYLE].toInt();
  // дисплей
  oled.init();
  oled.clear();
  oled.setContrast(255);
  // кфг сна
  gpio_config_t pwr_config;
  pwr_config.pin_bit_mask = (1ULL << GPIO_NUM_7);
  pwr_config.mode = GPIO_MODE_INPUT;
  pwr_config.pull_up_en = GPIO_PULLUP_ENABLE;
  pwr_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  pwr_config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&pwr_config);
  
  // для esp32-c3 используем gpio wakeup
  esp_sleep_enable_gpio_wakeup();
  gpio_wakeup_enable(GPIO_NUM_7, GPIO_INTR_LOW_LEVEL);
  // rtc
  Datime dt = rtc.getTime();
  // random
  int seed = dt.hour + dt.second + dt.day;
  randomSeed(seed);
  rnd.setSeed(seed);
  // led
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  Wire.setClock(400000);
}
void wakeFromSleep() {
  //врубаем дисплей
  oled.setPower(true);
  oled.clear();
  oled.update();
  Datime currentTime = rtc.getTime();
  uint32_t currentUnix = timeToUnix(currentTime);
  uint32_t sleepSeconds = currentUnix - sleepUnixStart;

  reset_buttons();
  PWR.resetStates();
  
  //для стабильности отображения
  lastBlink += sleepSeconds * 1000;
  lastUpdate += sleepSeconds * 1000;
  
  //флаг
  isSleeping = false;
  
  //перерисовываем
  drawWatchFace();
  
}

void enterLightSleep() {
  Datime sleepTime = rtc.getTime();
  sleepUnixStart = timeToUnix(sleepTime);
  
  //вычисляем время до будильника
  uint32_t timeToAlarm = 0;
  if (alarmActive) {
    Datime now = rtc.getTime();
    uint32_t currentUnix = timeToUnix(now);
    
    Datime alarmTime;
    alarmTime.second = 0;
    alarmTime.minute = alarmConfig.minute;
    alarmTime.hour = alarmConfig.hour;
    alarmTime.day = alarmConfig.day;
    alarmTime.month = alarmConfig.month;
    alarmTime.year = now.year;
    
    uint32_t alarmUnix = timeToUnix(alarmTime);
    
    if (alarmUnix > currentUnix) {
      timeToAlarm = (alarmUnix - currentUnix) * 1000000;
    }
  }
  
  oled.clear();
  oled.setScale(2);
  oled.setCursor(15, 3);
  oled.print("Спать...");
  oled.setScale(1);
  oled.setCursor(0, 7);
  oled.print("CatOs Sleep v0.1");
  oled.update();
  delay(100);
  
  //вырубаем питание
  oled.clear();
  oled.update();
  oled.setPower(false);
  
  //кфг энерго-сбережения
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
  
  // если будильник активен, устанавливаем таймер пробуждения
  if (alarmActive && timeToAlarm > 0) {
    esp_sleep_enable_timer_wakeup(timeToAlarm);
    Serial.printf("Таймер сна установлен на: %d секунд\n", timeToAlarm / 1000000);
  }
  
  // флаг
  isSleeping = true;
  
  Serial.flush();
  
  // переходим в light sleep
  esp_light_sleep_start();
  
  wakeFromSleep();
}
void checkAlarm() {
  if (!alarmActive || alarmRinging) return;
  
  Datime now = rtc.getTime();
  uint32_t currentUnix = timeToUnix(now);
  
  // вычисляем unix-время будильника
  Datime alarmTime;
  alarmTime.second = 0;
  alarmTime.minute = alarmConfig.minute;
  alarmTime.hour = alarmConfig.hour;
  alarmTime.day = alarmConfig.day;
  alarmTime.month = alarmConfig.month;
  alarmTime.year = now.year;
  
  alarmConfig.alarmUnixTime = timeToUnix(alarmTime);
  
  if (currentUnix >= alarmConfig.alarmUnixTime) {
    alarmRinging = true;
    alarmConfig.triggered = true;
    
    if (isSleeping) {
      wakeFromSleep();
    }
  }
}
// ==========================================
// ========== CAT# (CatSharp) ENGINE v1.0 ===
// ==========================================

enum CatOpCode {
    OP_UNKNOWN = 0,
    OP_VAR_INT, OP_VAR_FLOAT, OP_VAR_TEXT,
    OP_IF, OP_ELSE, OP_WHILE, OP_END,
    OP_PRINT, OP_PRINTLN, OP_UPDATE, OP_CLEAR, OP_DELAY,
    OP_DRAW_PIXEL, OP_DRAW_LINE, OP_DRAW_RECT, OP_DRAW_CIRCLE,
    OP_SET_CURSOR, OP_SET_CURSOR_XY, OP_SET_SCALE, OP_RANDOM,
    OP_EXIT,
    OP_CLICK, OP_HOLD, OP_PRESS,
    OP_LED, OP_UI_RAMA, OP_INVERT, OP_CONTRAST,
    OP_TIME_H, OP_TIME_M, OP_TIME_S, 
    OP_DATE_D, OP_DATE_M, OP_DATE_Y, OP_DATE_W,
    OP_TOK_NUM, OP_TOK_STR, OP_TOK_ID, OP_TOK_OP
};

struct CatToken {
    String value; 
    CatOpCode type;
};

struct Variable {
    int type = 0; // 0=INT, 1=FLOAT, 2=STR
    int intVal = 0;
    float floatVal = 0.0;
    String strVal = "";
};

struct CatBlock {
    int startTokenIndex;
    String type; 
};

class CatSharpInterpreter {
private:
    std::map<String, Variable> variables;
    std::map<String, int> functions; 
    std::vector<CatToken> tokens;      
    std::vector<CatBlock> controlStack; 
    int pos = 0;
    bool running = false;
    
    uint8_t cnt_click_ok = 0;
    uint8_t cnt_click_left = 0;
    uint8_t cnt_click_right = 0;
    bool buf_hold_ok = false, buf_hold_left = false, buf_hold_right = false;
    
    GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_I2C>* _oled;

    void serviceButtons() {
        right.tick(); left.tick(); ok.tick();
        
        if (ok.isClick() && cnt_click_ok < 255) cnt_click_ok++;
        if (left.isClick() && cnt_click_left < 255) cnt_click_left++;
        if (right.isClick() && cnt_click_right < 255) cnt_click_right++;
        
        if (ok.isHold()) buf_hold_ok = true;
        if (left.isHold()) buf_hold_left = true;
        if (right.isHold()) buf_hold_right = true;
    }

    bool isNum(String s) {
        if (s.length() == 0) return false;
        bool dot = false;
        for (unsigned int i = 0; i < s.length(); i++) {
            if (i == 0 && s[i] == '-') { if (s.length() == 1) return false; continue; }
            if (s[i] == '.') { if (dot) return false; dot = true; } 
            else if (!isDigit(s[i])) return false;
        }
        return true;
    }

    void tokenize(String code) {
        tokens.clear();
        String current = "";
        bool inStr = false;
        bool inComment = false;

        auto pushToken = [&](String str, bool isStringConst) {
            CatToken t;
            t.value = str;
            if (isStringConst) {
                t.type = OP_TOK_STR;
            } else if (isNum(str)) {
                t.type = OP_TOK_NUM;
            } else if (String("+-*/%<>=!").indexOf(str[0]) != -1) {
                t.type = OP_TOK_OP;
            } else {
                if (str == "int") t.type = OP_VAR_INT;
                else if (str == "float") t.type = OP_VAR_FLOAT;
                else if (str == "text") t.type = OP_VAR_TEXT;
                else if (str == "if") t.type = OP_IF;
                else if (str == "else") t.type = OP_ELSE;
                else if (str == "while") t.type = OP_WHILE;
                else if (str == "end") t.type = OP_END;
                else if (str == "print") t.type = OP_PRINT;
                else if (str == "println") t.type = OP_PRINTLN;
                else if (str == "update") t.type = OP_UPDATE;
                else if (str == "clear") t.type = OP_CLEAR;
                else if (str == "delay") t.type = OP_DELAY;
                else if (str == "draw_pixel") t.type = OP_DRAW_PIXEL;
                else if (str == "draw_line") t.type = OP_DRAW_LINE;
                else if (str == "draw_rect") t.type = OP_DRAW_RECT;
                else if (str == "draw_circle") t.type = OP_DRAW_CIRCLE;
                else if (str == "set_cursor") t.type = OP_SET_CURSOR;
                else if (str == "set_cursorXY") t.type = OP_SET_CURSOR_XY;
                else if (str == "set_scale") t.type = OP_SET_SCALE;
                else if (str == "random") t.type = OP_RANDOM;
                else if (str == "exit") t.type = OP_EXIT;
                else if (str == "click") t.type = OP_CLICK;
                else if (str == "hold") t.type = OP_HOLD;
                else if (str == "press") t.type = OP_PRESS;
                else if (str == "led") t.type = OP_LED;
                
                else if (str == "ui_rama") t.type = OP_UI_RAMA;
                else if (str == "invert") t.type = OP_INVERT;
                else if (str == "contrast") t.type = OP_CONTRAST;
                
                else if (str == "time_h") t.type = OP_TIME_H;
                else if (str == "time_m") t.type = OP_TIME_M;
                else if (str == "time_s") t.type = OP_TIME_S;
                else if (str == "date_d") t.type = OP_DATE_D;
                else if (str == "date_m") t.type = OP_DATE_M;
                else if (str == "date_y") t.type = OP_DATE_Y;
                else if (str == "date_w") t.type = OP_DATE_W;
                
                else t.type = OP_TOK_ID;
            }
            tokens.push_back(t);
        };

        for (unsigned int i = 0; i < code.length(); i++) {
            char c = code[i];
            if (!inStr && c == '/' && i + 1 < code.length() && code[i + 1] == '/') { inComment = true; i++; continue; }
            if (inComment) { if (c == '\n') inComment = false; continue; }
            if (c == '"') {
                if (inStr) { pushToken(current, true); current = ""; }
                inStr = !inStr; continue;
            }
            if (inStr) { current += c; continue; }
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '(' || c == ')' || c == ',' || c == '[' || c == ']') {
                if (current.length() > 0) { pushToken(current, false); current = ""; }
                continue;
            }
            if (String("+-*/%<>=!").indexOf(c) != -1) {
                if (c == '-' && current.length() == 0 && i + 1 < code.length() && isDigit(code[i+1])) { current += c; continue; }
                if (current.length() > 0) { pushToken(current, false); current = ""; }
                String op = String(c);
                if (i+1 < code.length() && String("=").indexOf(code[i+1]) != -1) { op += code[i+1]; i++; }
                pushToken(op, false);
                continue;
            }
            current += c;
        }
        if (current.length() > 0) pushToken(current, false);
    }

    Variable getVal(const CatToken& t) {
        if (t.type == OP_TOK_NUM) {
             Variable v;
             if (t.value.indexOf('.') != -1) { v.type = 1; v.floatVal = t.value.toFloat(); }
             else { v.type = 0; v.intVal = t.value.toInt(); }
             return v;
        }
        if (t.type == OP_TOK_STR) { Variable v; v.type = 2; v.strVal = t.value; return v; } 
        if (variables.count(t.value)) return variables[t.value];
        return Variable();
    }

    Variable evalExpr() {
        if (pos >= tokens.size()) return Variable();
        
        CatToken t = tokens[pos];
        Variable res;

        if (t.type >= OP_TIME_H && t.type <= OP_DATE_W) {
            Datime dt = rtc.getTime();
            res.type = 0; // INT
            if (t.type == OP_TIME_H) res.intVal = dt.hour;
            else if (t.type == OP_TIME_M) res.intVal = dt.minute;
            else if (t.type == OP_TIME_S) res.intVal = dt.second;
            else if (t.type == OP_DATE_D) res.intVal = dt.day;
            else if (t.type == OP_DATE_M) res.intVal = dt.month;
            else if (t.type == OP_DATE_Y) res.intVal = dt.year;
            pos++;
        }
        else if (t.type == OP_CLICK || t.type == OP_HOLD || t.type == OP_PRESS) {
            CatOpCode cmd = t.type;
            pos++; if (pos >= tokens.size()) return Variable();
            String btnName = tokens[pos].value;
            pos++;
            
            int btnID = -1; 
            if (btnName == "ok") btnID = 0;
            else if (btnName == "left") btnID = 1;  
            else if (btnName == "right") btnID = 2; 

            res.type = 0; res.intVal = 0;
            if (btnID != -1) {
                if (cmd == OP_CLICK) {
                    if (btnID == 0 && cnt_click_ok > 0) { res.intVal = 1; cnt_click_ok--; }
                    if (btnID == 1 && cnt_click_right > 0) { res.intVal = 1; cnt_click_right--; } // Inv
                    if (btnID == 2 && cnt_click_left > 0) { res.intVal = 1; cnt_click_left--; }   // Inv
                } 
                else if (cmd == OP_HOLD) {
                    if (btnID == 0) { res.intVal = buf_hold_ok; buf_hold_ok = false; }
                    if (btnID == 1) { res.intVal = buf_hold_right; buf_hold_right = false; }
                    if (btnID == 2) { res.intVal = buf_hold_left; buf_hold_left = false; }
                } 
                else { // PRESS
                    if (btnID == 0) res.intVal = ok.isPress();
                    if (btnID == 1) res.intVal = right.isPress();
                    if (btnID == 2) res.intVal = left.isPress();
                }
            }
        } else {
            res = getVal(t);
            pos++;
        }
        
        while (pos < tokens.size() && tokens[pos].type == OP_TOK_OP) {
            String op = tokens[pos].value;
            pos++; if (pos >= tokens.size()) break;
            
            Variable next;
            //проверка если следующий операнд это время или кнопка
            if ((tokens[pos].type >= OP_TIME_H && tokens[pos].type <= OP_DATE_W) ||
                (tokens[pos].type == OP_CLICK || tokens[pos].type == OP_HOLD || tokens[pos].type == OP_PRESS)) {
                 next = evalExpr();
            } else {
                 next = getVal(tokens[pos]);
                 pos++;
            }

            if (op == "+") res.intVal += next.intVal;
            else if (op == "-") res.intVal -= next.intVal;
            else if (op == "*") res.intVal *= next.intVal;
            else if (op == "/") { if(next.intVal!=0) res.intVal /= next.intVal; }
            else if (op == "==") res.intVal = (res.intVal == next.intVal);
            else if (op == ">") res.intVal = (res.intVal > next.intVal);
            else if (op == "<") res.intVal = (res.intVal < next.intVal);
            else if (op == "!=") res.intVal = (res.intVal != next.intVal);
        }
        
        pos--; 
        return res;
    }

    void skipBlock(bool stopAtElse = false) {
        int depth = 1;
        while (pos + 1 < tokens.size() && depth > 0) {
            pos++;
            CatOpCode t = tokens[pos].type;
            if (t == OP_IF || t == OP_WHILE) depth++;
            if (t == OP_END) depth--;
            if (stopAtElse && depth == 1 && t == OP_ELSE) return;
        }
    }

public:
    CatSharpInterpreter(GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_I2C>* oled) {
        _oled = oled;
    }

    void load(String code) {
        tokenize(code);
        functions.clear();
    }

    void run() {
        running = true;
        pos = 0;
        controlStack.clear();
        variables.clear();
        
        ok.resetStates(); left.resetStates(); right.resetStates();
        ok.setDebounce(20); left.setDebounce(20); right.setDebounce(20);
        ok.setTimeout(500); left.setTimeout(500); right.setTimeout(500);

        cnt_click_ok = 0; cnt_click_left = 0; cnt_click_right = 0;
        buf_hold_ok = buf_hold_left = buf_hold_right = false;

        digitalWrite(PIN_LED, LOW);
        Serial.println("Cat# v1.0");
        Wire.setClock(400000L);

        while (pos < tokens.size() && running) {
            serviceButtons(); 
            
            CatToken& t = tokens[pos];
            
            switch (t.type) {
                case OP_VAR_INT:
                case OP_VAR_FLOAT:
                case OP_VAR_TEXT: {
                    CatOpCode type = t.type; pos++;
                    if(pos >= tokens.size()) break;
                    String name = tokens[pos].value; pos++;
                    Variable val = evalExpr();
                    if (type == OP_VAR_INT) val.type = 0;
                    else if (type == OP_VAR_FLOAT) val.type = 1;
                    else val.type = 2;
                    variables[name] = val;
                    break;
                }
                
                case OP_TOK_ID: {
                    if (variables.count(t.value)) {
                        String varName = t.value; pos++;
                        if(pos >= tokens.size()) break;
                        String op = tokens[pos].value;
                        
                        if (op == "=") { pos++; Variable val = evalExpr(); variables[varName] = val; }
                        else if (op == "+") { pos++; Variable val = evalExpr(); variables[varName].intVal += val.intVal; }
                        else if (op == "-") { pos++; Variable val = evalExpr(); variables[varName].intVal -= val.intVal; }
                    }
                    break;
                }

                case OP_IF: {
                    pos++;
                    Variable cond = evalExpr();
                    if (pos < tokens.size() && tokens[pos].value != "then") pos--; 
                    controlStack.push_back({0, "if"});
                    if (cond.intVal == 0) {
                        skipBlock(true);
                        if (tokens[pos].type == OP_END) pos--;
                    }
                    break;
                }

                case OP_ELSE: skipBlock(); pos--; break;

                case OP_WHILE: {
                    int startPos = pos;
                    pos++;
                    Variable cond = evalExpr();
                    if (pos < tokens.size() && tokens[pos].value == "do") pos++;
                    
                    if (cond.intVal != 0) {
                        controlStack.push_back({startPos, "while"});
                    } else {
                        skipBlock();
                        pos--;
                    }
                    break;
                }

                case OP_END:
                    if (!controlStack.empty()) {
                        CatBlock b = controlStack.back();
                        controlStack.pop_back();
                        if (b.type == "while") pos = b.startTokenIndex - 1;
                    } else {
                        running = false;
                    }
                    break;

                case OP_PRINT:   { pos++; Variable v = evalExpr(); if(v.type==0)_oled->print(v.intVal); else _oled->print(v.strVal); break; }
                case OP_PRINTLN: { pos++; Variable v = evalExpr(); if(v.type==0)_oled->print(v.intVal); else _oled->print(v.strVal); _oled->println(); break; }
                case OP_UPDATE:  _oled->update(); break;
                case OP_CLEAR:   _oled->clear(); break;
                
                case OP_DELAY: {
                    pos++; Variable v = evalExpr();
                    unsigned long s = millis();
                    while(millis() - s < v.intVal) { serviceButtons(); yield(); }
                    break;
                }

                case OP_DRAW_PIXEL: { pos++; Variable x=evalExpr(); pos++; Variable y=evalExpr(); _oled->dot(x.intVal, y.intVal); break; }
                case OP_DRAW_LINE:  { pos++; Variable x1=evalExpr(); pos++; Variable y1=evalExpr(); pos++; Variable x2=evalExpr(); pos++; Variable y2=evalExpr(); _oled->line(x1.intVal, y1.intVal, x2.intVal, y2.intVal); break; }
                case OP_DRAW_RECT:  { pos++; Variable x=evalExpr(); pos++; Variable y=evalExpr(); pos++; Variable w=evalExpr(); pos++; Variable h=evalExpr(); _oled->rect(x.intVal - w.intVal/2, y.intVal - h.intVal/2, w.intVal, h.intVal, OLED_STROKE); break; }
                case OP_DRAW_CIRCLE:{ pos++; Variable x=evalExpr(); pos++; Variable y=evalExpr(); pos++; Variable r=evalExpr(); _oled->circle(x.intVal, y.intVal, r.intVal, OLED_STROKE); break; }
                
                case OP_SET_CURSOR: { pos++; Variable x=evalExpr(); pos++; Variable y=evalExpr(); _oled->setCursor(x.intVal, y.intVal); break; }
                case OP_SET_CURSOR_XY: { pos++; Variable x=evalExpr(); pos++; Variable y=evalExpr(); _oled->setCursorXY(x.intVal, y.intVal); break; }
                case OP_SET_SCALE:  { pos++; Variable s=evalExpr(); _oled->setScale(s.intVal); break; }
                
                case OP_RANDOM: {
                    pos++; Variable mn=evalExpr(); pos++; Variable mx=evalExpr();
                    pos++; 
                    if(pos < tokens.size()) {
                        String rv = tokens[pos].value;
                        Variable r; r.type=0; r.intVal=random(mn.intVal, mx.intVal);
                        variables[rv] = r;
                    }
                    break;
                }
                
                case OP_UI_RAMA: {
                    pos++; 
                    Variable title = evalExpr(); 
                    int bat = 1, line = 1, clr = 1, tm = 1;
                    auto isNextArg = [&]() {
                        if (pos + 1 >= tokens.size()) return false;
                        CatOpCode t = tokens[pos + 1].type;
                        return (t == OP_TOK_NUM || t == OP_TOK_ID || t == OP_TOK_STR || 
                                t == OP_CLICK || t == OP_HOLD || t == OP_PRESS || 
                                (t >= OP_TIME_H && t <= OP_DATE_W));
                    };
                    if (isNextArg()) { pos++; bat = evalExpr().intVal; }
                    if (isNextArg()) { pos++; line = evalExpr().intVal; }
                    if (isNextArg()) { pos++; clr = evalExpr().intVal; }
                    if (isNextArg()) { pos++; tm = evalExpr().intVal; }
                    ui_rama(title.strVal.c_str(), bat, line, clr, tm);
                    break;
                }
                
                case OP_INVERT: { pos++; Variable v = evalExpr(); _oled->invertDisplay(v.intVal); break; }
                case OP_CONTRAST: { pos++; Variable v = evalExpr(); _oled->setContrast(v.intVal); break; }
                case OP_LED: { pos++; Variable v = evalExpr(); digitalWrite(PIN_LED, v.intVal ? HIGH : LOW); break; }

                case OP_EXIT: running = false; break;
                
                default: break;
            }
            
            pos++;
            yield();
        }
        
        _oled->invertDisplay(false);
        _oled->setContrast(255);
        digitalWrite(PIN_LED, LOW);
        Wire.setClock(100000L);
    }
};

void createExitApp(CustomApp& app) {
    app.filename = "EXIT";
    app.name = "Выход";
    //копируем битмап выхода
    memcpy_P(app.icon, exit_bitmap_24x24, 72);
    app.hasIcon = true;
    app.isValid = true;
}
//парс файла
bool parseCatFile(const String& filename, CustomApp& app) {
    app.filename = filename;
    app.hasIcon = false;
    app.name = "";
    app.isValid = false;
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        return false;
    }
    
    String fileContent = file.readString();
    file.close();
    
    // парсим иконку
    int iconStart = fileContent.indexOf("#icon {");
    if (iconStart != -1) {
        int iconEnd = fileContent.indexOf("};", iconStart);
        if (iconEnd != -1) {
            String iconData = fileContent.substring(iconStart + 7, iconEnd);
            iconData.trim();
            
            // парсим байты иконки
            int byteCount = 0;
            int startPos = 0;
            
            while (startPos < iconData.length() && byteCount < 72) {
                // ищем следующий 0x
                int hexStart = iconData.indexOf("0x", startPos);
                if (hexStart == -1) break;
                
                // извлекаем hex значение
                int commaPos = iconData.indexOf(",", hexStart);
                if (commaPos == -1) commaPos = iconData.length();
                
                String hexStr = iconData.substring(hexStart + 2, commaPos);
                hexStr.trim();
                
                // конвертируем hex в байт
                app.icon[byteCount] = strtoul(hexStr.c_str(), NULL, 16);
                byteCount++;
                
                startPos = commaPos + 1;
            }
            
            if (byteCount == 72) {
                app.hasIcon = true;
            }
        }
    }
    
    // парсим название
    int nameStart = fileContent.indexOf("#name \"");
    if (nameStart != -1) {
        int nameEnd = fileContent.indexOf("\"", nameStart + 7);
        if (nameEnd != -1) {
            app.name = fileContent.substring(nameStart + 7, nameEnd);
            app.isValid = !app.name.isEmpty();
        }
    }
    
    return app.isValid;
}
CustomApp getAppByIndex(int index) {
    CustomApp app;
    int totalItems = catAppCount + 1;
    
    index = (index + totalItems) % totalItems;

    if (index == catAppCount) {
        createExitApp(app);
    } else {
        if (catFilenames != nullptr) {
            parseCatFile(catFilenames[index], app);
        } else {
            app.isValid = false;
        }
    }
    return app;
}

void drawAppsStatic() {
    Datime dt = rtc.getTime();
    
    oled.setScale(1);
    
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", dt.hour, dt.minute);
    oled.setCursorXY(95, 2);
    oled.print(timeStr);
    oled.fastLineH(10, 94, 128, OLED_STROKE);
    oled.line(89, 1, 94, 10, OLED_STROKE);
    
    oled.setCursorXY(3,2);
    oled.print("Apps");
    oled.line(40, 1, 35, 10, OLED_STROKE);
    oled.fastLineH(10, 1, 35, OLED_STROKE);
}

void drawAppWidget(int x, CustomApp& app, bool isCenter) {
    if (x < -40 || x > 140) return;

    int dist = abs(x - 48);
    int y_offset = (dist * dist) / 350; 
    int y = 16 + y_offset; 

    oled.roundRect(x, y, x + 31, y + 31, OLED_STROKE);
    
    int bmp_x = x + 4;
    int bmp_y = y + 4;
    
    if (app.isValid) {
        if (app.hasIcon) {
            oled.drawBitmap(bmp_x, bmp_y, app.icon, 24, 24);
        } else {
            oled.drawBitmap(bmp_x, bmp_y, unkown_file_24x24, 24, 24);
        }
    } else {
        oled.drawBitmap(bmp_x, bmp_y, unkown_file_24x24, 24, 24);
    }

    if (isCenter && !app.name.isEmpty() && dist < 12) {
        oled.setScale(1);
        
        int textLen = getRealStringLength(app.name.c_str());
        int textWidth = textLen * 6;
        int textX = x + (32 - textWidth) / 2; 
        
        if (textX < 0) textX = 0;
        
        oled.setCursorXY(textX, y + 34);
        oled.print(app.name);
    }
}
int getCatFiles() {
    if (catFilenames != nullptr) {
        delete[] catFilenames;
        catFilenames = nullptr;
    }
    
    File root = LittleFS.open("/");
    int count = 0;
    
    // считаем количество файлов .cat
    File file = root.openNextFile();
    while (file) {
        String filename = file.name();
        if (filename.endsWith(".cat")) {
            count++;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    
    if (count == 0) {
        return 0;
    }
    
    // выделяем память и заполняем массив
    catFilenames = new String[count];
    root = LittleFS.open("/");
    
    int index = 0;
    file = root.openNextFile();
    while (file && index < count) {
        String filename = file.name();
        if (filename.endsWith(".cat")) {
            catFilenames[index] = "/" + filename;
            index++;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    
    return count;
}
void custom_apps_menu() {
    reset_buttons();

    catAppCount = getCatFiles();
    currentCatAppIndex = 0; 
    int totalItems = catAppCount + 1; 

    //кеширование
    int prevIdx = (currentCatAppIndex - 1 + totalItems) % totalItems;
    int nextIdx = (currentCatAppIndex + 1) % totalItems;

    CustomApp appPrev = getAppByIndex(prevIdx);
    CustomApp appCurr = getAppByIndex(currentCatAppIndex);
    CustomApp appNext = getAppByIndex(nextIdx);

    bool needRedraw = true;
    uint8_t last_minute = rtc.getTime().minute;
    
    while (true) {
        buttons_tick();
        
        Datime dt = rtc.getTime();
        if (dt.minute != last_minute) { last_minute = dt.minute; needRedraw = true; }
        
        // статика
        if (needRedraw) {
            needRedraw = false;
            oled.clear();
            drawAppsStatic(); // фон

            // драв из памяти
            drawAppWidget(8, appPrev, false);
            drawAppWidget(48, appCurr, true);
            drawAppWidget(88, appNext, false);
            
            oled.rect(0, 0, 127, 63, OLED_STROKE);
            oled.update();
        }
        
        if (left.isClick() || left.isHold()) {
            setCpuFrequencyMhz(160); 
            Wire.setClock(800000L); 

            int nextNextIdx = (currentCatAppIndex + 2) % totalItems;
            CustomApp appNextNext = getAppByIndex(nextNextIdx); 

            int step = ANIMATION_SPEED; 
            for (int offset = 0; offset <= 40; offset += step) {
                oled.clear();
                drawAppsStatic();
                
                drawAppWidget(8 - offset, appPrev, false);
                drawAppWidget(48 - offset, appCurr, offset < 8); 
                drawAppWidget(88 - offset, appNext, false);
                drawAppWidget(128 - offset, appNextNext, false);
                
                oled.rect(0, 0, 127, 63, OLED_STROKE);
                oled.update();
            }

            // сдвиг памяти
            appPrev = appCurr;
            appCurr = appNext;
            appNext = appNextNext;

            currentCatAppIndex = (currentCatAppIndex + 1) % totalItems;
            needRedraw = true;

            Wire.setClock(400000L); 
            setCpuFrequencyMhz(80); 
            
            if (right.isHold()) delay(50);
        }
        
        if (right.isClick() || right.isHold()) {
            setCpuFrequencyMhz(160); 
            Wire.setClock(800000L); 

            int prevPrevIdx = (currentCatAppIndex - 2 + totalItems) % totalItems;
            CustomApp appPrevPrev = getAppByIndex(prevPrevIdx);

            int step = ANIMATION_SPEED;
            for (int offset = 0; offset <= 40; offset += step) {
                oled.clear();
                drawAppsStatic();
                
                drawAppWidget(-32 + offset, appPrevPrev, false);
                drawAppWidget(8 + offset, appPrev, false);
                drawAppWidget(48 + offset, appCurr, offset < 8);
                drawAppWidget(88 + offset, appNext, false);
                
                oled.rect(0, 0, 127, 63, OLED_STROKE);
                oled.update();
            }

            // сдвиг памяти
            appNext = appCurr;
            appCurr = appPrev;
            appPrev = appPrevPrev;

            currentCatAppIndex = (currentCatAppIndex - 1 + totalItems) % totalItems;
            needRedraw = true;

            Wire.setClock(400000L);
            setCpuFrequencyMhz(80);
            
            if (left.isHold()) delay(50);
        }
        
        if (ok.isClick()) {
            //если ВЫХОД
            if (currentCatAppIndex == catAppCount) {
                if (catFilenames != nullptr) { delete[] catFilenames; catFilenames = nullptr; }
                exit(); return;
            } 
            else {
                //запуск!!!
                oled.clear();
                oled.setCursor(10, 3);
                oled.print("Cat# Runtime Core"); // лого
                oled.update();
                
                File file = LittleFS.open(appCurr.filename, "r");
                if (file) {
                    String script = file.readString();
                    file.close();
                    
                    setCpuFrequencyMhz(160); 
                    
                    CatSharpInterpreter interp(&oled);
                    interp.load(script);
                    oled.clear(); 
                    interp.run();
                    
                    setCpuFrequencyMhz(80);
                    
                    reset_buttons();
                    needRedraw = true;
                } else {
                   oled.clear();
                   oled.setCursor(0, 3);
                   oled.print("Ошибка");
                   oled.update();
                   delay(1000);
                   needRedraw = true;
                }
            }
        }
        
        if (ok.isHold()) {
            if (catFilenames != nullptr) { delete[] catFilenames; catFilenames = nullptr; }
            exit(); return;
        }
    }
}
void loop() {
  buttons_tick();
  PWR.tick();

  checkAlarm();
  if (left.isClick()) {
    ledState = !ledState; 
    digitalWrite(PIN_LED, ledState ? HIGH : LOW);
  }
  if (alarmRinging) {
    showAlarmScreen();
  }
  
  if (PWR.isClick()) {
    enterLightSleep();
  }
  if (isSleeping) {
    return;
  }
  
  if (ok.isHold()) {
    toggleAlarm();
    oled.clear();
    oled.setScale(2);
    oled.setCursor(10, 2);
    oled.print("Будильник");
    oled.setCursor(20, 4);
    oled.print(alarmActive ? "ВКЛ" : "ВЫКЛ");
    oled.update();
    delay(1000);
    drawWatchFace();
  }
  
  if (millis() - lastBlink >= 1000) {
    dotsVisible = !dotsVisible;
    lastBlink = millis();
  }
  
  if (millis() - lastUpdate >= 100) {
    drawWatchFace();
    lastUpdate = millis();
  }
  
  // открытие главного меню по ок
  if (ok.isClick() && !alarmRinging) {
    open_graphical_main();
    drawWatchFace();
  }
}

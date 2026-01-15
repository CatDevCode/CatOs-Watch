#ifndef MENU_DATA_H
#define MENU_DATA_H

#include "Arduino.h"

// --- СТРУКТУРЫ ---
struct GraphMenuItem {
  const char* name;       // Имя под иконкой
  const uint8_t* icon;    // Иконка (или nullptr)
  void (*action)();       // Функция (или nullptr для ВЫХОДА/НАЗАД)
};

struct GraphMenu {
  const char* title;      // Заголовок
  uint8_t titleEndPos;    // Длина линии заголовка
  GraphMenuItem* items;   // Массив пунктов
  uint8_t itemCount;      // Количество пунктов
};

// --- ССЫЛКИ НА ФУНКЦИИ (из main.cpp) ---
// Игры и приложения
extern void dinosaurGame();
extern void pet_menu();
extern void rouletteGame();
extern void snakeGame();
extern void custom_apps_menu();
extern void playPong();
// Утилиты
extern void ShowFilesLittleFS(); // Читалка
extern void calc();
extern void stopwatch();
extern void timer();
extern void alarm_menu();

// WiFi
extern void create_settings();   // Загрузка файлов
extern void time_sync_menu();    // Синхронизация времени

// --- ФУНКЦИИ НАВИГАЦИИ (которые мы напишем в main.cpp) ---
void open_graphical_games();
void open_graphical_wifi();
void open_graphical_utils();


// =========================
// === НАСТРОЙКИ МЕНЮ ===
// =========================

// --- 1. МЕНЮ ИГР ---
GraphMenuItem items_Games[] = {
  {"Дино",     dino_icon_24x24,            dinosaurGame},
  {"Пинг-Понг",dino_icon_24x24,            playPong},
  {"Тамогочи", catosgotchi_icon_24x24,     pet_menu},
  {"Рулетка",  nullptr,                    rouletteGame},
  {"Змейка",   nullptr,                    snakeGame},
  {"Назад",    exit_bitmap_24x24,          nullptr}
};
GraphMenu data_GamesMenu = {"Игры", 40, items_Games, 5};


// --- 2. МЕНЮ WIFI ---
GraphMenuItem items_Wifi[] = {
  {"Загрузка", nullptr,           create_settings},
  {"Синхр.Вр", nullptr,           time_sync_menu},
  {"Назад",    exit_bitmap_24x24, nullptr}
};
GraphMenu data_WifiMenu = {"WiFi", 35, items_Wifi, 3};


// --- 3. МЕНЮ УТИЛИТ ---
GraphMenuItem items_Utils[] = {
  {"Читалка",  book_icon_24x24,        ShowFilesLittleFS},
  {"Калькул.", calc_icon_24x24,        calc},
  {"Секундом.",stopwatch_icon_24x24,   stopwatch},
  {"Таймер",   timer_icon_24x24,       timer},
  {"Будильн.", alarm_icon_menu_24x24,  alarm_menu},
  {"Назад",    exit_bitmap_24x24,      nullptr}
};
GraphMenu data_UtilsMenu = {"Утилиты", 60, items_Utils, 6};


// --- 4. ГЛАВНОЕ МЕНЮ ---
GraphMenuItem items_Main[] = {
  {"Игры",       games_24x24,             open_graphical_games}, // Открывает меню игр
  {"Приложения", CatSharp_icon_24x24,     custom_apps_menu},     // Меню .cat файлов
  {"Утилиты",    utilies_icon_24x24,      open_graphical_utils}, // Открывает меню утилит
  {"WiFi",       WiFi_icon_24x24,         open_graphical_wifi},  // Открывает меню WiFi
  {"Выход",      exit2_icon_24x24,        nullptr}     // Выход на циферблат
};
GraphMenu data_MainMenu = {"Меню", 32, items_Main, 5};

#endif
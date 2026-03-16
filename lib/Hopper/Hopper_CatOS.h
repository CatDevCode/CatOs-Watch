#pragma once

#include <Arduino.h>
#include <GyverButton.h>

#ifdef CATOS_SPI_DISPLAY
  #include <GyverOLED.h>
  extern GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_SPI, 5, 16, 17> oled;
  extern GButton up, down, left, ok, right;
#else
  #include <GyverOLED.h>
  extern GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_I2C> oled;
  extern GButton left, ok, right;
  #ifdef CATOS_S3_BUILD
    extern GButton PWR;
  #else
    extern GButton PWR;
  #endif
#endif
extern void buttons_tick();

#define WHITE 1
#define BLACK 0
#define HOPPER_WIDTH 128
#define HOPPER_HEIGHT 64

#define UP_BUTTON 1
#define RIGHT_BUTTON 2
#define LEFT_BUTTON 4
#define DOWN_BUTTON 8
#define A_BUTTON 16
#define B_BUTTON 32

extern void playHopper();

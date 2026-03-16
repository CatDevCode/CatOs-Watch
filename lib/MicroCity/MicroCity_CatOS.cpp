#include <Arduino.h>
#include "MicroCity_CatOS.h"
#include "Game.h"
#include "Interface.h"
#include "GyverButton.h"
#include <GyverOLED.h>
#include <LittleFS.h>

extern void buttons_tick();

#ifdef CATOS_SPI_DISPLAY
extern GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_SPI, 5, 16, 17> oled;
extern GButton up;
extern GButton down;
extern GButton left;
extern GButton ok;
extern GButton right;
#else
extern GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_I2C> oled;
extern GButton left;
extern GButton right;
extern GButton ok;
#endif

uint8_t GetInput() {
    uint8_t input = 0;
#ifdef CATOS_SPI_DISPLAY
    if (right.state()) input |= INPUT_LEFT;
    if (left.state()) input |= INPUT_RIGHT;
    if (up.state()) input |= INPUT_UP;
    if (down.state()) input |= INPUT_DOWN;
    if (ok.state()) input |= INPUT_B;
    if (ok.isHold()) input |= INPUT_A;
#else
    if (left.state()) input |= INPUT_LEFT;
    if (right.state()) input |= INPUT_RIGHT;
    if (ok.state()) input |= INPUT_B;
    if (ok.isHold()) input |= INPUT_A;
#endif
    return input;
}

void playMicroCity() {
    InitGame();
    while (true) {
        buttons_tick();
        if ((left.isHold() && right.state()) || (right.isHold() && left.state())) break;
        TickGame();
        oled.update();
        delay(10);
    }
}

void PutPixel(uint8_t x, uint8_t y, uint8_t colour) {
    oled.dot(x, y, colour);
}

void DrawBitmap(const uint8_t* bmp, uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    oled.drawBitmap(x, y, bmp, w, h);
}

uint8_t* GetPowerGrid() {
    static uint8_t powerGrid[288 + 4608];
    return powerGrid;
}

void SaveCity() {
    File f = LittleFS.open("/city.dat", "w");
    if (f) {
        f.write((uint8_t*)&State, sizeof(State));
        f.close();
    }
}

bool LoadCity() {
    File f = LittleFS.open("/city.dat", "r");
    if (f) {
        f.read((uint8_t*)&State, sizeof(State));
        f.close();
        return true;
    }
    return false;
}

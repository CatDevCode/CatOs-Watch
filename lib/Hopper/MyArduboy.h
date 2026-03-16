#pragma once

#include "Hopper_CatOS.h"

typedef unsigned char buffer_t;

class MyArduboyAudio
{
public:
    void begin() {}
    void on() {}
    void off() {}
    void toggle() {}
    bool enabled() { return false; }
    void saveOnOff() {}
};

class DummyTunes {
public:
    void stopScore() {}
};

class MyArduboy
{
public:
    void    beginNoLogo(void);
    bool    nextFrame(void);
    bool    buttonDown(uint8_t buttons);
    bool    buttonPressed(uint8_t buttons);
    bool    buttonUp(uint8_t buttons);

    void    setFrameRate(uint8_t rate);
    void    clear(void);
    void    display(void);
    void    drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h, uint8_t color);
    void    drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
    void    setCursor(int16_t x, int16_t y);
    size_t  print(const char *p);
    size_t  print(int n);
    size_t  print(const __FlashStringHelper *ifsh);
    buffer_t* getBuffer(void);

    void    setRGBled(uint8_t r, uint8_t g, uint8_t b) {}

    void    setTextColor(uint8_t color);
    void    setTextColor(uint8_t color, uint8_t bg);
    size_t  printEx(int16_t x, int16_t y, const char *p);
    size_t  printEx(int16_t x, int16_t y, const __FlashStringHelper *p);
    size_t  write(uint8_t);
    void    drawRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    void    drawFastVLine2(int16_t x, int16_t y, int8_t h, uint8_t color);
    void    drawFastHLine2(int16_t x, int16_t y, uint8_t w, uint8_t color);
    void    fillRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);

    bool isAudioEnabled(void);
    void setAudioEnabled(bool on);
    void saveAudioOnOff(void);

    MyArduboyAudio audio;
    DummyTunes tunes;
    void    playScore2(const byte *score, uint8_t priority);
    void    stopScore2(void);

private:
    void    myDrawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size);
    void    fillBeltBlack(buffer_t *p, uint8_t d, uint8_t w);
    void    fillBeltWhite(buffer_t *p, uint8_t d, uint8_t w);
    
    void drawPixel(int16_t x, int16_t y, uint8_t color);
    void fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color);
    uint8_t buttonsState();

    uint8_t textcolor = WHITE;
    uint8_t textbg = BLACK;
    uint8_t lastButtonState;
    uint8_t currentButtonState;
    uint8_t playScorePriority;
    
    uint8_t frameRate = 60;
    int16_t cursor_x = 0;
    int16_t cursor_y = 0;
    uint8_t textSize = 1;
    bool textWrap = false;
};

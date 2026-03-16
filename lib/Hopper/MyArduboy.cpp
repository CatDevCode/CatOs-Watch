//небольшая прослойка для arduboy 
#include "MyArduboy.h"

PROGMEM static const uint32_t imgFont[] = {
    0x00000000, 0x00017000, 0x000C00C0, 0x0A7CA7CA, 0x0855F542, 0x19484253, 0x1251F55E, 0x00003000,
    0x00452700, 0x001C9440, 0x0519F314, 0x0411F104, 0x00000420, 0x04104104, 0x00000400, 0x01084210,
    0x0F45145E, 0x0001F040, 0x13555559, 0x0D5D5551, 0x087C928C, 0x0D555557, 0x0D55555E, 0x010C5251,
    0x0F55555E, 0x0F555556, 0x0000A000, 0x0000A400, 0x0028C200, 0x0028A280, 0x00086280, 0x000D5040,
    0x0018E300, 0x1F24929C, 0x0D5D555F, 0x1145149C, 0x0725145F, 0x1155555F, 0x0114515F, 0x1D55545E,
    0x1F10411F, 0x0045F440, 0x07210410, 0x1D18411F, 0x1041041F, 0x1F04F05E, 0x1F04109C, 0x0F45545E,
    0x0314925F, 0x1F45D45E, 0x1B34925F, 0x0D555556, 0x0105F041, 0x0721041F, 0x0108421F, 0x0F41E41F,
    0x1D184317, 0x0109C107, 0x114D5651, 0x0045F000, 0x0001F000, 0x0001F440, 0x000C1080, 0x10410410,
};


void MyArduboy::beginNoLogo(void)
{
}

void MyArduboy::setFrameRate(uint8_t rate) { frameRate = rate; }
void MyArduboy::clear(void) { oled.clear(); }
void MyArduboy::display(void) { oled.update(); }

void MyArduboy::drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h, uint8_t color) {
    if (color == WHITE) {
        oled.drawBitmap(x, y, bitmap, w, h);
    } else {
        if (x >= HOPPER_WIDTH || y >= HOPPER_HEIGHT || x + w <= 0 || y + h <= 0) return;
        buffer_t *buf = getBuffer();
        uint8_t pages = (h + 7) / 8;
        for (uint8_t page = 0; page < pages; page++) {
            for (uint8_t col = 0; col < w; col++) {
                int16_t dx = x + col;
                if (dx < 0 || dx >= HOPPER_WIDTH) continue;
                uint8_t data = pgm_read_byte(bitmap + page * w + col);
                int16_t dy = y + page * 8;
                for (uint8_t bit = 0; bit < 8; bit++) {
                    int16_t py = dy + bit;
                    if (py < 0 || py >= HOPPER_HEIGHT) continue;
                    if (data & (1 << bit)) {
                        uint16_t bufIdx = dx + (py / 8) * HOPPER_WIDTH;
                        buf[bufIdx] &= ~(1 << (py & 7));
                    }
                }
            }
        }
    }
}

void MyArduboy::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color) {
    oled.line(x0, y0, x1, y1);
}

void MyArduboy::setCursor(int16_t x, int16_t y) {
    cursor_x = x;
    cursor_y = y;
}

size_t MyArduboy::print(int n) {
    char buf[16];
    itoa(n, buf, 10);
    return print(buf);
}

size_t MyArduboy::print(const __FlashStringHelper *ifsh) {
    return print(reinterpret_cast<const char *>(ifsh)); 
}

buffer_t* MyArduboy::getBuffer(void) {
    return (buffer_t*)oled._oled_buffer;
}

size_t MyArduboy::print(const char *p)
{
    size_t n = 0;
    while (*p) {
        write(*p++);
        n++;
    }
    return n;
}

bool MyArduboy::nextFrame(void)
{
    bool ret = true;
    if (ret) {
        lastButtonState = currentButtonState;
        currentButtonState = buttonsState();
    }
    return ret;
}

bool MyArduboy::buttonDown(uint8_t buttons)
{
    return currentButtonState & ~lastButtonState & buttons;
}

bool MyArduboy::buttonPressed(uint8_t buttons)
{
    return currentButtonState & buttons;
}


bool MyArduboy::buttonUp(uint8_t buttons)
{
    return ~currentButtonState & lastButtonState & buttons;
}

void MyArduboy::drawPixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= HOPPER_WIDTH || y < 0 || y >= HOPPER_HEIGHT) return;
    oled.dot(x, y, color);
}

void MyArduboy::fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color)
{
    for (int16_t j = y; j < y + h; j++) {
        for (int16_t i = x; i < x + w; i++) {
            if (i >= 0 && i < HOPPER_WIDTH && j >= 0 && j < HOPPER_HEIGHT) {
                oled.dot(i, j, color);
            }
        }
    }
}

uint8_t MyArduboy::buttonsState() { 
    uint8_t state = 0;
#ifdef CATOS_SPI_DISPLAY
    if (up.state()) state |= UP_BUTTON;
    if (down.state()) state |= DOWN_BUTTON;
    if (left.state()) state |= LEFT_BUTTON;
    if (right.state()) state |= RIGHT_BUTTON;
    if (ok.state()) state |= A_BUTTON | B_BUTTON;
#else
    if (left.state()) state |= LEFT_BUTTON;
    if (right.state()) state |= RIGHT_BUTTON;
    if (ok.state()) state |= A_BUTTON | B_BUTTON;
#endif
    return state;
}

/*----------------------------------------------------------------------------*/

void MyArduboy::setTextColor(uint8_t color)
{
    setTextColor(color, (color == BLACK) ? WHITE : BLACK);
}

void MyArduboy::setTextColor(uint8_t color, uint8_t bg)
{
    textcolor = color;
    textbg = bg;
}

size_t MyArduboy::printEx(int16_t x, int16_t y, const char *p)
{
    setCursor(x, y);
    return print(p);
}

size_t MyArduboy::printEx(int16_t x, int16_t y, const __FlashStringHelper *p)
{
    setCursor(x, y);
    return print(p);
}

size_t MyArduboy::write(uint8_t c)
{
    if (c == '\n') {
        cursor_y += textSize * 6;
        cursor_x = 0;
    } else if (c >= ' ' && c <= '_') {
        myDrawChar(cursor_x, cursor_y, c, textcolor, textbg, textSize);
        cursor_x += textSize * 6;
        if (textWrap && (cursor_x > (HOPPER_WIDTH - textSize * 6))) write('\n');
    }
    return 1;
}

void MyArduboy::myDrawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size)
{
    bool draw_bg = bg != color;

    if (x >= HOPPER_WIDTH || y >= HOPPER_HEIGHT || x + 5 * size < 0 || y + 6 * size < 0) return;
    uint32_t ptn = imgFont[c - ' '];
    if (size == 1) {
        for (int8_t i = 0; i < 6; i++) {
            for (int8_t j = 0; j < 6; j++) {
                bool draw_fg = ptn & 0x1;
                if (draw_fg || draw_bg) {
                    drawPixel(x + i, y + j, (draw_fg) ? color : bg);
                }
                ptn >>= 1;
            }
        }
    } else {
        for (int8_t i = 0; i < 6; i++) {
            for (int8_t j = 0; j < 6; j++) {
                bool draw_fg = ptn & 0x1;
                if (draw_fg || draw_bg) {
                    fillRect(x + i * size, y + j * size, size, size, (draw_fg) ? color : bg);
                }
                ptn >>= 1;
            }
        }
    }
}

void MyArduboy::drawRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color)
{
    drawFastHLine2(x, y, w, color);
    drawFastHLine2(x, y + h - 1, w, color);
    drawFastVLine2(x, y + 1, h - 2, color);
    drawFastVLine2(x + w - 1, y + 1, h - 2, color);
}

void MyArduboy::drawFastVLine2(int16_t x, int16_t y, int8_t h, uint8_t color)
{
    if (x < 0 || x >= HOPPER_WIDTH) return;
    for (int16_t py = y; py < y + h; py++) {
        if (py >= 0 && py < HOPPER_HEIGHT) oled.dot(x, py, color);
    }
}

void MyArduboy::drawFastHLine2(int16_t x, int16_t y, uint8_t w, uint8_t color)
{
    if (y < 0 || y >= HOPPER_HEIGHT) return;
    for (int16_t px = x; px < x + w; px++) {
        if (px >= 0 && px < HOPPER_WIDTH) oled.dot(px, y, color);
    }
}

void MyArduboy::fillRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color)
{
    for (int16_t py = y; py < y + h; py++) {
        for (int16_t px = x; px < x + w; px++) {
            if (px >= 0 && px < HOPPER_WIDTH && py >= 0 && py < HOPPER_HEIGHT) {
                oled.dot(px, py, color);
            }
        }
    }
}

void MyArduboy::fillBeltBlack(buffer_t *p, uint8_t d, uint8_t w) {}
void MyArduboy::fillBeltWhite(buffer_t *p, uint8_t d, uint8_t w) {}

bool MyArduboy::isAudioEnabled(void)
{
    return audio.enabled();
}

void MyArduboy::setAudioEnabled(bool on)
{
}

void MyArduboy::saveAudioOnOff(void)
{
}

void MyArduboy::playScore2(const byte *score, uint8_t priority)
{
}

void MyArduboy::stopScore2(void)
{
}

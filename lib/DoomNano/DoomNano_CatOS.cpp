// doom-nano port for CatOS
// (костыли) порт с ардуино

#include <Arduino.h>
#include "DoomNano_CatOS.h"
#include "dn_constants.h"
#include "dn_level.h"
#include "dn_sprites.h"
#include "dn_entities.h"
#include "dn_types.h"
#include <GyverOLED.h>
#include "GyverButton.h"

// объекты
extern void buttons_tick();

#ifdef CATOS_SPI_DISPLAY
extern GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_SPI, 5, 16, 17> oled;
extern GButton up, down, left, ok, right;
#else
extern GyverOLED<SSD1306_128x64, OLED_BUFFER, OLED_I2C> oled;
extern GButton left, ok, right;
extern GButton PWR;
#endif

// макросы
#define dn_swap(a, b) do { typeof(a) temp = a; a = b; b = temp; } while (0)
const static uint8_t dn_bit_mask[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
#define dn_read_bit(b, n) (b & dn_bit_mask[n] ? 1 : 0)

// буферы
static uint8_t dn_display_buf[SCREEN_WIDTH * (SCREEN_HEIGHT / 8)];
static uint8_t dn_zbuffer[ZBUFFER_SIZE];

static double dn_delta = 1;
static uint32_t dn_lastFrameTime = 0;

// состояние
static uint8_t dn_scene = INTRO;
static bool dn_exit_scene = false;
static bool dn_invert_screen = false;
static uint8_t dn_flash_screen = 0;
static bool dn_game_running = true; 

static Player dn_player;
static Entity dn_entity[MAX_ENTITIES];
static StaticEntity dn_static_entity[MAX_STATIC_ENTITIES];
static uint8_t dn_num_entities = 0;
static uint8_t dn_num_static_entities = 0;

static void dn_jumpTo(uint8_t target_scene);
static void dn_initializeLevel(const uint8_t level[]);
static uint8_t dn_getBlockAt(const uint8_t level[], uint8_t x, uint8_t y);
static bool dn_isSpawned(UID uid);
static void dn_spawnEntity(uint8_t type, uint8_t x, uint8_t y);
static void dn_spawnFireball(double x, double y);
static void dn_removeEntity(UID uid, bool makeStatic = false);
static UID dn_detectCollision(const uint8_t level[], Coords *pos, double rx, double ry, bool only_walls = false);
static void dn_fire();
static UID dn_updatePosition(const uint8_t level[], Coords *pos, double rx, double ry, bool only_walls = false);
static void dn_updateEntities(const uint8_t level[]);
static void dn_renderMap(const uint8_t level[], double view_height);
static void dn_renderEntities(double view_height);
static void dn_renderGun(uint8_t gun_pos, double amount_jogging);
static void dn_renderHud();
static void dn_updateHud();
static Coords dn_translateIntoView(Coords *pos);

// ввод
static bool dn_input_fire() {
  return ok.state();
}

static bool dn_input_up() {
#ifdef CATOS_SPI_DISPLAY
  return up.state(); // 5 кнопок
#else
  return ok.state();
#endif
}

static bool dn_input_down() {
#ifdef CATOS_SPI_DISPLAY
  return down.state();
#else
  return false;
#endif
}

static bool dn_input_left() {
#ifdef CATOS_SPI_DISPLAY
  return right.state();
#else
  return left.state();
#endif
}

static bool dn_input_right() {
#ifdef CATOS_SPI_DISPLAY
  return left.state();
#else
  return right.state();
#endif
}

// отрисовка (через буфер)
static void dn_drawByte(uint8_t x, uint8_t y, uint8_t b) {
  dn_display_buf[(y / 8) * SCREEN_WIDTH + x] = b;
}

static bool dn_getGradientPixel(uint8_t x, uint8_t y, uint8_t i) {
  if (i == 0) return 0;
  if (i >= GRADIENT_COUNT - 1) return 1;

  uint8_t index = max(0, min(GRADIENT_COUNT - 1, (int)i)) * GRADIENT_WIDTH * GRADIENT_HEIGHT
                  + y * GRADIENT_WIDTH % (GRADIENT_WIDTH * GRADIENT_HEIGHT)
                  + x / GRADIENT_HEIGHT % GRADIENT_WIDTH;
  return dn_read_bit(pgm_read_byte(dn_gradient + index), x % 8);
}

static void dn_drawPixel(int8_t x, int8_t y, bool color, bool raycasterViewport = false) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= (raycasterViewport ? RENDER_HEIGHT : SCREEN_HEIGHT)) {
    return;
  }
  if (color) {
    dn_display_buf[x + (y / 8) * SCREEN_WIDTH] |= (1 << (y & 7));
  } else {
    dn_display_buf[x + (y / 8) * SCREEN_WIDTH] &= ~(1 << (y & 7));
  }
}

static void dn_drawVLine(uint8_t x, int8_t start_y, int8_t end_y, uint8_t intensity) {
  int8_t lower_y = max((int)min((int)start_y, (int)end_y), 0);
  int8_t higher_y = min((int)max((int)start_y, (int)end_y), (int)(RENDER_HEIGHT - 1));
  uint8_t c;
  uint8_t bp;
  uint8_t b;

  for (c = 0; c < RES_DIVIDER; c++) {
    int8_t y = lower_y;
    b = 0;
    while (y <= higher_y) {
      bp = y % 8;
      b = b | dn_getGradientPixel(x + c, y, intensity) << bp;
      if (bp == 7) {
        dn_drawByte(x + c, y, b);
        b = 0;
      }
      y++;
    }
    if (bp != 7) {
      dn_drawByte(x + c, y - 1, b);
    }
  }
}

static void dn_drawSprite(
  int8_t x, int8_t y,
  const uint8_t bitmap[], const uint8_t mask[],
  int16_t w, int16_t h,
  uint8_t sprite,
  double distance
) {
  uint8_t tw = (double) w / distance;
  uint8_t th = (double) h / distance;
  uint8_t byte_width = w / 8;
  uint8_t pixel_size = max(1, (int)(1.0 / distance));
  uint16_t sprite_offset = byte_width * h * sprite;

  bool pixel;
  bool maskPixel;

  if (dn_zbuffer[min(max((int)x, 0), ZBUFFER_SIZE - 1) / Z_RES_DIVIDER] < distance * DISTANCE_MULTIPLIER) {
    return;
  }

  for (uint8_t ty = 0; ty < th; ty += pixel_size) {
    if (y + ty < 0 || y + ty >= RENDER_HEIGHT) continue;
    uint8_t sy = ty * distance;
    for (uint8_t tx = 0; tx < tw; tx += pixel_size) {
      uint8_t sx = tx * distance;
      uint16_t byte_offset = sprite_offset + sy * byte_width + sx / 8;
      if (x + tx < 0 || x + tx >= SCREEN_WIDTH) continue;

      maskPixel = dn_read_bit(pgm_read_byte(mask + byte_offset), sx % 8);
      if (maskPixel) {
        pixel = dn_read_bit(pgm_read_byte(bitmap + byte_offset), sx % 8);
        for (uint8_t ox = 0; ox < pixel_size; ox++) {
          for (uint8_t oy = 0; oy < pixel_size; oy++) {
            dn_drawPixel(x + tx + ox, y + ty + oy, pixel, true);
          }
        }
      }
    }
  }
}

static void dn_drawChar(int8_t x, int8_t y, char ch) {
  uint8_t c = 0;
  const char* charMap = DN_CHAR_MAP;
  while (charMap[c] != ch && charMap[c] != '\0') c++;

  uint8_t bOffset = c / 2;
  for (uint8_t line = 0; line < DN_CHAR_HEIGHT; line++) {
    uint8_t b = pgm_read_byte(dn_bmp_font + (line * bmp_font_width + bOffset));
    for (uint8_t n = 0; n < DN_CHAR_WIDTH; n++)
      if (dn_read_bit(b, (c % 2 == 0 ? 0 : 4) + n))
        dn_drawPixel(x + n, y + line, 1, false);
  }
}

static void dn_drawText(int8_t x, int8_t y, const char *txt, uint8_t space = 1) {
  uint8_t pos = x;
  uint8_t i = 0;
  char ch;
  while ((ch = txt[i]) != '\0') {
    dn_drawChar(pos, y, ch);
    i++;
    pos += DN_CHAR_WIDTH + space;
    if (pos > SCREEN_WIDTH) return;
  }
}

static void dn_drawTextNum(uint8_t x, uint8_t y, uint8_t num) {
  char buf[4];
  itoa(num, buf, 10);
  dn_drawText(x, y, buf);
}

static void dn_fadeScreen(uint8_t intensity, bool color = 0) {
  for (uint8_t x = 0; x < SCREEN_WIDTH; x++) {
    for (uint8_t y = 0; y < SCREEN_HEIGHT; y++) {
      if (dn_getGradientPixel(x, y, intensity))
        dn_drawPixel(x, y, color, false);
    }
  }
}

static void dn_displayFrame() {
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    for (int p = 0; p < (SCREEN_HEIGHT / 8); p++) {
      oled._oled_buffer[p + x * 8] = dn_display_buf[x + p * SCREEN_WIDTH];
    }
  }
  oled.update();
}

// фпс
static void dn_fps() {
  while (millis() - dn_lastFrameTime < FRAME_TIME);
  dn_delta = (double)(millis() - dn_lastFrameTime) / FRAME_TIME;
  dn_lastFrameTime = millis();
}

static double dn_getActualFps() {
  return 1000 / (FRAME_TIME * dn_delta);
}

static void dn_jumpTo(uint8_t target_scene) {
  dn_scene = target_scene;
  dn_exit_scene = true;
}

// уровни и сущности
static uint8_t dn_getBlockAt(const uint8_t level[], uint8_t x, uint8_t y) {
  if (x >= LEVEL_WIDTH || y >= LEVEL_HEIGHT) {
    return E_FLOOR;
  }
  return pgm_read_byte(level + (((LEVEL_HEIGHT - 1 - y) * LEVEL_WIDTH + x) / 2))
         >> (!(x % 2) * 4)
         & 0b1111;
}

static void dn_initializeLevel(const uint8_t level[]) {
  for (uint8_t y = LEVEL_HEIGHT - 1; y > 0; y--) {
    for (uint8_t x = 0; x < LEVEL_WIDTH; x++) {
      uint8_t block = dn_getBlockAt(level, x, y);
      if (block == E_PLAYER) {
        dn_player = create_player(x, y);
        return;
      }
    }
  }
}

static bool dn_isSpawned(UID uid) {
  for (uint8_t i = 0; i < dn_num_entities; i++) {
    if (dn_entity[i].uid == uid) return true;
  }
  return false;
}

static bool dn_isStatic(UID uid) {
  for (uint8_t i = 0; i < dn_num_static_entities; i++) {
    if (dn_static_entity[i].uid == uid) return true;
  }
  return false;
}

static void dn_spawnEntity(uint8_t type, uint8_t x, uint8_t y) {
  if (dn_num_entities >= MAX_ENTITIES) return;

  switch (type) {
    case E_ENEMY:
      dn_entity[dn_num_entities] = create_enemy(x, y);
      dn_num_entities++;
      break;
    case E_KEY:
      dn_entity[dn_num_entities] = create_key(x, y);
      dn_num_entities++;
      break;
    case E_MEDIKIT:
      dn_entity[dn_num_entities] = create_medikit(x, y);
      dn_num_entities++;
      break;
  }
}

static void dn_spawnFireball(double x, double y) {
  if (dn_num_entities >= MAX_ENTITIES) return;

  UID uid = create_uid(E_FIREBALL, x, y);
  if (dn_isSpawned(uid)) return;

  int16_t dir = FIREBALL_ANGLES + atan2(y - dn_player.pos.y, x - dn_player.pos.x) / PI * FIREBALL_ANGLES;
  if (dir < 0) dir += FIREBALL_ANGLES * 2;
  dn_entity[dn_num_entities] = create_fireball(x, y, dir);
  dn_num_entities++;
}

static void dn_removeEntity(UID uid, bool makeStatic) {
  uint8_t i = 0;
  bool found = false;
  while (i < dn_num_entities) {
    if (!found && dn_entity[i].uid == uid) {
      found = true;
      dn_num_entities--;
    }
    if (found) {
      dn_entity[i] = dn_entity[i + 1];
    }
    i++;
  }
}

static UID dn_detectCollision(const uint8_t level[], Coords *pos, double relative_x, double relative_y, bool only_walls) {
  uint8_t round_x = int(pos->x + relative_x);
  uint8_t round_y = int(pos->y + relative_y);
  uint8_t block = dn_getBlockAt(level, round_x, round_y);

  if (block == E_WALL) {
    return create_uid(block, round_x, round_y);
  }

  if (only_walls) return UID_null;

  for (uint8_t i = 0; i < dn_num_entities; i++) {
    if (&(dn_entity[i].pos) == pos) continue;
    uint8_t type = uid_get_type(dn_entity[i].uid);
    if (type != E_ENEMY || dn_entity[i].state == S_DEAD || dn_entity[i].state == S_HIDDEN) continue;

    Coords new_coords = { dn_entity[i].pos.x - relative_x, dn_entity[i].pos.y - relative_y };
    uint8_t distance = coords_distance(pos, &new_coords);

    if (distance < ENEMY_COLLIDER_DIST && distance < dn_entity[i].distance) {
      return dn_entity[i].uid;
    }
  }
  return UID_null;
}

static void dn_fire() {
  for (uint8_t i = 0; i < dn_num_entities; i++) {
    if (uid_get_type(dn_entity[i].uid) != E_ENEMY || dn_entity[i].state == S_DEAD || dn_entity[i].state == S_HIDDEN) continue;

    Coords transform = dn_translateIntoView(&(dn_entity[i].pos));
    if (abs(transform.x) < 20 && transform.y > 0) {
      uint8_t damage = (double)min((int)GUN_MAX_DAMAGE, (int)(GUN_MAX_DAMAGE / (abs(transform.x) * dn_entity[i].distance) / 5));
      if (damage > 0) {
        dn_entity[i].health = max(0, dn_entity[i].health - damage);
        dn_entity[i].state = S_HIT;
        dn_entity[i].timer = 4;
      }
    }
  }
}

static UID dn_updatePosition(const uint8_t level[], Coords *pos, double relative_x, double relative_y, bool only_walls) {
  UID collide_x = dn_detectCollision(level, pos, relative_x, 0, only_walls);
  UID collide_y = dn_detectCollision(level, pos, 0, relative_y, only_walls);

  if (!collide_x) pos->x += relative_x;
  if (!collide_y) pos->y += relative_y;

  return collide_x || collide_y || UID_null;
}

static void dn_updateEntities(const uint8_t level[]) {
  uint8_t i = 0;
  while (i < dn_num_entities) {
    dn_entity[i].distance = coords_distance(&(dn_player.pos), &(dn_entity[i].pos));
    if (dn_entity[i].timer > 0) dn_entity[i].timer--;

    if (dn_entity[i].distance > MAX_ENTITY_DISTANCE) {
      dn_removeEntity(dn_entity[i].uid);
      continue;
    }

    if (dn_entity[i].state == S_HIDDEN) { i++; continue; }

    uint8_t type = uid_get_type(dn_entity[i].uid);

    switch (type) {
      case E_ENEMY: {
        if (dn_entity[i].health == 0) {
          if (dn_entity[i].state != S_DEAD) {
            dn_entity[i].state = S_DEAD;
            dn_entity[i].timer = 6;
          }
        } else if (dn_entity[i].state == S_HIT) {
          if (dn_entity[i].timer == 0) {
            dn_entity[i].state = S_ALERT;
            dn_entity[i].timer = 40;
          }
        } else if (dn_entity[i].state == S_FIRING) {
          if (dn_entity[i].timer == 0) {
            dn_entity[i].state = S_ALERT;
            dn_entity[i].timer = 40;
          }
        } else {
          if (dn_entity[i].distance > ENEMY_MELEE_DIST && dn_entity[i].distance < MAX_ENEMY_VIEW) {
            if (dn_entity[i].state != S_ALERT) {
              dn_entity[i].state = S_ALERT;
              dn_entity[i].timer = 20;
            } else {
              if (dn_entity[i].timer == 0) {
                dn_spawnFireball(dn_entity[i].pos.x, dn_entity[i].pos.y);
                dn_entity[i].state = S_FIRING;
                dn_entity[i].timer = 6;
              } else {
                dn_updatePosition(level, &(dn_entity[i].pos),
                  dn_sign(dn_player.pos.x, dn_entity[i].pos.x) * ENEMY_SPEED * dn_delta,
                  dn_sign(dn_player.pos.y, dn_entity[i].pos.y) * ENEMY_SPEED * dn_delta,
                  true);
              }
            }
          } else if (dn_entity[i].distance <= ENEMY_MELEE_DIST) {
            if (dn_entity[i].state != S_MELEE) {
              dn_entity[i].state = S_MELEE;
              dn_entity[i].timer = 10;
            } else if (dn_entity[i].timer == 0) {
              dn_player.health = max(0, dn_player.health - ENEMY_MELEE_DAMAGE);
              dn_entity[i].timer = 14;
              dn_flash_screen = 1;
              dn_updateHud();
            }
          } else {
            dn_entity[i].state = S_STAND;
          }
        }
        break;
      }

      case E_FIREBALL: {
        if (dn_entity[i].distance < FIREBALL_COLLIDER_DIST) {
          dn_player.health = max(0, dn_player.health - ENEMY_FIREBALL_DAMAGE);
          dn_flash_screen = 1;
          dn_updateHud();
          dn_removeEntity(dn_entity[i].uid);
          continue;
        } else {
          UID collided = dn_updatePosition(level, &(dn_entity[i].pos),
            cos((double) dn_entity[i].health / FIREBALL_ANGLES * PI) * FIREBALL_SPEED,
            sin((double) dn_entity[i].health / FIREBALL_ANGLES * PI) * FIREBALL_SPEED,
            true);
          if (collided) {
            dn_removeEntity(dn_entity[i].uid);
            continue;
          }
        }
        break;
      }

      case E_MEDIKIT: {
        if (dn_entity[i].distance < ITEM_COLLIDER_DIST) {
          dn_entity[i].state = S_HIDDEN;
          dn_player.health = min(100, dn_player.health + 50);
          dn_updateHud();
          dn_flash_screen = 1;
        }
        break;
      }

      case E_KEY: {
        if (dn_entity[i].distance < ITEM_COLLIDER_DIST) {
          dn_entity[i].state = S_HIDDEN;
          dn_player.keys++;
          dn_updateHud();
          dn_flash_screen = 1;
        }
        break;
      }
    }
    i++;
  }
}


static void dn_renderMap(const uint8_t level[], double view_height) {
  UID last_uid;

  for (uint8_t x = 0; x < SCREEN_WIDTH; x += RES_DIVIDER) {
    double camera_x = 2 * (double) x / SCREEN_WIDTH - 1;
    double ray_x = dn_player.dir.x + dn_player.plane.x * camera_x;
    double ray_y = dn_player.dir.y + dn_player.plane.y * camera_x;
    uint8_t map_x = uint8_t(dn_player.pos.x);
    uint8_t map_y = uint8_t(dn_player.pos.y);
    Coords map_coords = { dn_player.pos.x, dn_player.pos.y };
    double delta_x = abs(1 / ray_x);
    double delta_y = abs(1 / ray_y);

    int8_t step_x, step_y;
    double side_x, side_y;

    if (ray_x < 0) { step_x = -1; side_x = (dn_player.pos.x - map_x) * delta_x; }
    else { step_x = 1; side_x = (map_x + 1.0 - dn_player.pos.x) * delta_x; }

    if (ray_y < 0) { step_y = -1; side_y = (dn_player.pos.y - map_y) * delta_y; }
    else { step_y = 1; side_y = (map_y + 1.0 - dn_player.pos.y) * delta_y; }

    uint8_t depth = 0;
    bool hit = 0;
    bool side;
    while (!hit && depth < MAX_RENDER_DEPTH) {
      if (side_x < side_y) {
        side_x += delta_x; map_x += step_x; side = 0;
      } else {
        side_y += delta_y; map_y += step_y; side = 1;
      }

      uint8_t block = dn_getBlockAt(level, map_x, map_y);
      if (block == E_WALL) {
        hit = 1;
      } else {
        if (block == E_ENEMY || (block & 0b00001000)) {
          if (coords_distance(&(dn_player.pos), &map_coords) < MAX_ENTITY_DISTANCE) {
            UID uid = create_uid(block, map_x, map_y);
            if (last_uid != uid && !dn_isSpawned(uid)) {
              dn_spawnEntity(block, map_x, map_y);
              last_uid = uid;
            }
          }
        }
      }
      depth++;
    }

    if (hit) {
      double distance;
      if (side == 0)
        distance = max(1.0, (map_x - dn_player.pos.x + (1 - step_x) / 2) / ray_x);
      else
        distance = max(1.0, (map_y - dn_player.pos.y + (1 - step_y) / 2) / ray_y);

      dn_zbuffer[x / Z_RES_DIVIDER] = min((int)(distance * DISTANCE_MULTIPLIER), 255);

      uint8_t line_height = RENDER_HEIGHT / distance;

      dn_drawVLine(
        x,
        view_height / distance - line_height / 2 + RENDER_HEIGHT / 2,
        view_height / distance + line_height / 2 + RENDER_HEIGHT / 2,
        GRADIENT_COUNT - int(distance / MAX_RENDER_DEPTH * GRADIENT_COUNT) - side * 2
      );
    }
  }
}

// сортировка врагов
static void dn_sortEntities() {
  uint8_t gap = dn_num_entities;
  bool swapped = false;
  while (gap > 1 || swapped) {
    gap = (gap * 10) / 13;
    if (gap == 9 || gap == 10) gap = 11;
    if (gap < 1) gap = 1;
    swapped = false;
    for (uint8_t i = 0; i < dn_num_entities - gap; i++) {
      uint8_t j = i + gap;
      if (dn_entity[i].distance < dn_entity[j].distance) {
        dn_swap(dn_entity[i], dn_entity[j]);
        swapped = true;
      }
    }
  }
}

static Coords dn_translateIntoView(Coords *pos) {
  double sprite_x = pos->x - dn_player.pos.x;
  double sprite_y = pos->y - dn_player.pos.y;
  double inv_det = 1.0 / (dn_player.plane.x * dn_player.dir.y - dn_player.dir.x * dn_player.plane.y);
  double transform_x = inv_det * (dn_player.dir.y * sprite_x - dn_player.dir.x * sprite_y);
  double transform_y = inv_det * (- dn_player.plane.y * sprite_x + dn_player.plane.x * sprite_y);
  return { transform_x, transform_y };
}

static void dn_renderEntities(double view_height) {
  dn_sortEntities();

  for (uint8_t i = 0; i < dn_num_entities; i++) {
    if (dn_entity[i].state == S_HIDDEN) continue;

    Coords transform = dn_translateIntoView(&(dn_entity[i].pos));
    if (transform.y <= 0.1 || transform.y > MAX_SPRITE_DEPTH) continue;

    int16_t sprite_screen_x = HALF_WIDTH * (1.0 + transform.x / transform.y);
    int8_t sprite_screen_y = RENDER_HEIGHT / 2 + view_height / transform.y;
    uint8_t type = uid_get_type(dn_entity[i].uid);

    if (sprite_screen_x < - HALF_WIDTH || sprite_screen_x > SCREEN_WIDTH + HALF_WIDTH) continue;

    switch (type) {
      case E_ENEMY: {
        uint8_t sprite;
        if (dn_entity[i].state == S_ALERT) sprite = int(millis() / 500) % 2;
        else if (dn_entity[i].state == S_FIRING) sprite = 2;
        else if (dn_entity[i].state == S_HIT) sprite = 3;
        else if (dn_entity[i].state == S_MELEE) sprite = dn_entity[i].timer > 10 ? 2 : 1;
        else if (dn_entity[i].state == S_DEAD) sprite = dn_entity[i].timer > 0 ? 3 : 4;
        else sprite = 0;

        dn_drawSprite(
          sprite_screen_x - BMP_IMP_WIDTH * .5 / transform.y,
          sprite_screen_y - 8 / transform.y,
          dn_bmp_imp_bits, dn_bmp_imp_mask,
          BMP_IMP_WIDTH, BMP_IMP_HEIGHT, sprite, transform.y);
        break;
      }

      case E_FIREBALL: {
        dn_drawSprite(
          sprite_screen_x - BMP_FIREBALL_WIDTH / 2 / transform.y,
          sprite_screen_y - BMP_FIREBALL_HEIGHT / 2 / transform.y,
          dn_bmp_fireball_bits, dn_bmp_fireball_mask,
          BMP_FIREBALL_WIDTH, BMP_FIREBALL_HEIGHT, 0, transform.y);
        break;
      }

      case E_MEDIKIT: {
        dn_drawSprite(
          sprite_screen_x - BMP_ITEMS_WIDTH / 2 / transform.y,
          sprite_screen_y + 5 / transform.y,
          dn_bmp_items_bits, dn_bmp_items_mask,
          BMP_ITEMS_WIDTH, BMP_ITEMS_HEIGHT, 0, transform.y);
        break;
      }

      case E_KEY: {
        dn_drawSprite(
          sprite_screen_x - BMP_ITEMS_WIDTH / 2 / transform.y,
          sprite_screen_y + 5 / transform.y,
          dn_bmp_items_bits, dn_bmp_items_mask,
          BMP_ITEMS_WIDTH, BMP_ITEMS_HEIGHT, 1, transform.y);
        break;
      }
    }
  }
}

static void dn_renderGun(uint8_t gun_pos, double amount_jogging) {
  char x = 48 + sin((double) millis() * JOGGING_SPEED) * 10 * amount_jogging;
  char y = RENDER_HEIGHT - gun_pos + abs(cos((double) millis() * JOGGING_SPEED)) * 8 * amount_jogging;

  if (gun_pos > GUN_SHOT_POS - 2) {
    for (int16_t fy = 0; fy < BMP_FIRE_HEIGHT; fy++) {
      for (int16_t fx = 0; fx < BMP_FIRE_WIDTH; fx++) {
        uint16_t byteIdx = fy * (BMP_FIRE_WIDTH / 8) + fx / 8;
        if (dn_read_bit(pgm_read_byte(dn_bmp_fire_bits + byteIdx), fx % 8))
          dn_drawPixel(x + 6 + fx, y - 11 + fy, 1, false);
      }
    }
  }

  uint8_t clip_height = max(0, min((int)(y + BMP_GUN_HEIGHT), (int)RENDER_HEIGHT) - (int)y);

  for (int16_t gy = 0; gy < clip_height; gy++) {
    for (int16_t gx = 0; gx < BMP_GUN_WIDTH; gx++) {
      uint16_t byteIdx = gy * (BMP_GUN_WIDTH / 8) + gx / 8;
      if (dn_read_bit(pgm_read_byte(dn_bmp_gun_mask + byteIdx), gx % 8))
        dn_drawPixel(x + gx, y + gy, 0, false);
      if (dn_read_bit(pgm_read_byte(dn_bmp_gun_bits + byteIdx), gx % 8))
        dn_drawPixel(x + gx, y + gy, 1, false);
    }
  }
}

static void dn_renderHud() {
  dn_drawText(2, 58, "{}", 0);
  dn_drawText(40, 58, "[]", 0);
  dn_updateHud();
}

static void dn_updateHud() {
  for (int cx = 12; cx < 27; cx++)
    for (int cy = 58; cy < 64; cy++)
      dn_drawPixel(cx, cy, 0, false);
  for (int cx = 50; cx < 55; cx++)
    for (int cy = 58; cy < 64; cy++)
      dn_drawPixel(cx, cy, 0, false);

  dn_drawTextNum(12, 58, dn_player.health);
  dn_drawTextNum(50, 58, dn_player.keys);
}

static void dn_renderStats() {
  for (int cx = 58; cx < 128; cx++)
    for (int cy = 58; cy < 64; cy++)
      dn_drawPixel(cx, cy, 0, false);
  dn_drawTextNum(114, 58, int(dn_getActualFps()));
  dn_drawTextNum(82, 58, dn_num_entities);
}

static void dn_loopIntro() {
  memset(dn_display_buf, 0, sizeof(dn_display_buf));

  for (int16_t ly = 0; ly < BMP_LOGO_HEIGHT; ly++) {
    for (int16_t lx = 0; lx < BMP_LOGO_WIDTH; lx++) {
      uint16_t byteIdx = ly * (BMP_LOGO_WIDTH / 8) + lx / 8;
      if (dn_read_bit(pgm_read_byte(dn_bmp_logo_bits + byteIdx), lx % 8))
        dn_drawPixel((SCREEN_WIDTH - BMP_LOGO_WIDTH) / 2 + lx, (SCREEN_HEIGHT - BMP_LOGO_HEIGHT) / 3 + ly, 1, false);
    }
  }

  dn_drawText(SCREEN_WIDTH / 2 - 25, SCREEN_HEIGHT * .8, "PRESS FIRE");
  dn_displayFrame();

  delay(500);

  while (!dn_exit_scene && dn_game_running) {
    buttons_tick();
    yield();

#ifdef CATOS_SPI_DISPLAY
#else
    if (PWR.isClick()) { dn_game_running = false; return; }
#endif
    if (left.isHold() && right.state()) { dn_game_running = false; return; }

    if (dn_input_fire()) dn_jumpTo(GAME_PLAY);
    delay(20);
  }
}


static void dn_loopGamePlay() {
  bool gun_fired = false;
  uint8_t gun_pos = 0;
  double rot_speed;
  double old_dir_x;
  double old_plane_x;
  double view_height = 0;
  double jogging = 0;
  uint8_t fade = GRADIENT_COUNT - 1;

  dn_num_entities = 0;
  dn_num_static_entities = 0;
  dn_initializeLevel(sto_level_1);

  do {
    dn_fps();

    memset(dn_display_buf, 0, SCREEN_WIDTH * (RENDER_HEIGHT / 8));

    buttons_tick();
    yield();

#ifdef CATOS_SPI_DISPLAY
#else
    if (PWR.isClick()) { dn_game_running = false; return; }
#endif
    if (left.isHold() && right.state()) { dn_game_running = false; return; }

    if (dn_player.health > 0) {
      if (dn_input_up()) {
        dn_player.velocity += (MOV_SPEED - dn_player.velocity) * .4;
        jogging = abs(dn_player.velocity) * MOV_SPEED_INV;
      } else {
        dn_player.velocity *= .5;
        jogging = abs(dn_player.velocity) * MOV_SPEED_INV;
      }

      if (dn_input_right()) {
        rot_speed = ROT_SPEED * dn_delta;
        old_dir_x = dn_player.dir.x;
        dn_player.dir.x = dn_player.dir.x * cos(-rot_speed) - dn_player.dir.y * sin(-rot_speed);
        dn_player.dir.y = old_dir_x * sin(-rot_speed) + dn_player.dir.y * cos(-rot_speed);
        old_plane_x = dn_player.plane.x;
        dn_player.plane.x = dn_player.plane.x * cos(-rot_speed) - dn_player.plane.y * sin(-rot_speed);
        dn_player.plane.y = old_plane_x * sin(-rot_speed) + dn_player.plane.y * cos(-rot_speed);
      } else if (dn_input_left()) {
        rot_speed = ROT_SPEED * dn_delta;
        old_dir_x = dn_player.dir.x;
        dn_player.dir.x = dn_player.dir.x * cos(rot_speed) - dn_player.dir.y * sin(rot_speed);
        dn_player.dir.y = old_dir_x * sin(rot_speed) + dn_player.dir.y * cos(rot_speed);
        old_plane_x = dn_player.plane.x;
        dn_player.plane.x = dn_player.plane.x * cos(rot_speed) - dn_player.plane.y * sin(rot_speed);
        dn_player.plane.y = old_plane_x * sin(rot_speed) + dn_player.plane.y * cos(rot_speed);
      }

      view_height = abs(sin((double) millis() * JOGGING_SPEED)) * 6 * jogging;

      if (gun_pos > GUN_TARGET_POS) {
        gun_pos -= 1;
      } else if (gun_pos < GUN_TARGET_POS) {
        gun_pos += 2;
      } else if (!gun_fired && dn_input_fire()) {
        gun_pos = GUN_SHOT_POS;
        gun_fired = true;
        dn_fire();
      } else if (gun_fired && !dn_input_fire()) {
        gun_fired = false;
      }
    } else {
      if (view_height > -10) view_height--;
      else if (dn_input_fire()) dn_jumpTo(INTRO);
      if (gun_pos > 1) gun_pos -= 2;
    }

    if (abs(dn_player.velocity) > 0.003) {
      dn_updatePosition(sto_level_1, &(dn_player.pos),
        dn_player.dir.x * dn_player.velocity * dn_delta,
        dn_player.dir.y * dn_player.velocity * dn_delta);
    } else {
      dn_player.velocity = 0;
    }

    dn_updateEntities(sto_level_1);
    dn_renderMap(sto_level_1, view_height);
    dn_renderEntities(view_height);
    dn_renderGun(gun_pos, jogging);

    if (fade > 0) {
      dn_fadeScreen(fade);
      fade--;
      if (fade == 0) dn_renderHud();
    } else {
      dn_renderStats();
    }

    if (dn_flash_screen > 0) {
      dn_invert_screen = !dn_invert_screen;
      dn_flash_screen--;
    } else if (dn_invert_screen) {
      dn_invert_screen = false;
    }

    oled.invertDisplay(dn_invert_screen);
    dn_displayFrame();

  } while (!dn_exit_scene && dn_game_running);
}


void playDoomNano() {
  dn_scene = INTRO;
  dn_exit_scene = false;
  dn_invert_screen = false;
  dn_flash_screen = 0;
  dn_game_running = true;
  dn_num_entities = 0;
  dn_num_static_entities = 0;
  dn_lastFrameTime = millis();

  memset(dn_display_buf, 0, sizeof(dn_display_buf));
  memset(dn_zbuffer, 0xFF, ZBUFFER_SIZE);

  while (dn_game_running) {
    switch (dn_scene) {
      case INTRO:
        dn_loopIntro();
        break;
      case GAME_PLAY:
        dn_loopGamePlay();
        break;
    }

    if (!dn_game_running) break;

    for (uint8_t i = 0; i < GRADIENT_COUNT; i++) {
      dn_fadeScreen(i, 0);
      dn_displayFrame();
      delay(40);
    }
    dn_exit_scene = false;
  }

  oled.invertDisplay(false);
  oled.clear();
  oled.update();
}

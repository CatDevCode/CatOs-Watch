#ifndef _dn_types_h
#define _dn_types_h

#include <stdint.h>

#define UID_null  0

// Entity types
#define E_FLOOR             0x0
#define E_WALL              0xF
#define E_PLAYER            0x1
#define E_ENEMY             0x2
#define E_DOOR              0x4
#define E_LOCKEDDOOR        0x5
#define E_EXIT              0x7
// collectable entities >= 0x8
#define E_MEDIKIT           0x8
#define E_KEY               0x9
#define E_FIREBALL          0xA

typedef uint16_t UID;
typedef uint8_t  EType;

struct Coords {
  double x;
  double y;
};

UID create_uid(EType type, uint8_t x, uint8_t y);
EType uid_get_type(UID uid);

Coords create_coords(double x, double y);
uint8_t coords_distance(Coords* a, Coords* b);

#endif

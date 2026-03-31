#ifndef _dn_constants_h
#define _dn_constants_h

#include <stdint.h>


#define OPTIMIZE_SSD1306

#define FRAME_TIME          66.666666 
#define RES_DIVIDER         2          
#define Z_RES_DIVIDER       2        
#define DISTANCE_MULTIPLIER 20
#define MAX_RENDER_DEPTH    12
#define MAX_SPRITE_DEPTH    8

#define ZBUFFER_SIZE        (SCREEN_WIDTH / Z_RES_DIVIDER)

#define LEVEL_WIDTH_BASE    6
#define LEVEL_WIDTH         (1 << LEVEL_WIDTH_BASE)
#define LEVEL_HEIGHT        57
#define LEVEL_SIZE          (LEVEL_WIDTH / 2 * LEVEL_HEIGHT)

#define INTRO                 0
#define GAME_PLAY             1

#define GUN_TARGET_POS        18
#define GUN_SHOT_POS          (GUN_TARGET_POS + 4)

#define ROT_SPEED             .12
#define MOV_SPEED             .2
#define MOV_SPEED_INV         5 
#define JOGGING_SPEED         .005
#define ENEMY_SPEED           .02
#define FIREBALL_SPEED        .2
#define FIREBALL_ANGLES       45

#define MAX_ENTITIES          10
#define MAX_STATIC_ENTITIES   28

#define MAX_ENTITY_DISTANCE   200
#define MAX_ENEMY_VIEW        80
#define ITEM_COLLIDER_DIST    6
#define ENEMY_COLLIDER_DIST   4
#define FIREBALL_COLLIDER_DIST 2
#define ENEMY_MELEE_DIST      6
#define WALL_COLLIDER_DIST    .2

#define ENEMY_MELEE_DAMAGE    8
#define ENEMY_FIREBALL_DAMAGE 20
#define GUN_MAX_DAMAGE        15

constexpr uint8_t SCREEN_WIDTH     =  128;
constexpr uint8_t SCREEN_HEIGHT    =  64;
constexpr uint8_t HALF_WIDTH       =  SCREEN_WIDTH/2;
constexpr uint8_t RENDER_HEIGHT    =  56;
constexpr uint8_t HALF_HEIGHT      =  SCREEN_HEIGHT/2;

#endif

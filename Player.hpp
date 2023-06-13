#ifndef ADHTP_PLAYER_HDR
#define ADHTP_PLAYER_HDR

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_render.h>

#include "math.hpp"
#include "types.hpp"
#include "Map.hpp"

enum Direction {
    None,
    Left,
    Right,
};

struct Player {
    const struct {
        int WIDTH   = 8;
        int HEIGHT  = 12;
    } SIZE;

    // position
    struct {
        int x = 10;
        int y = 10;
    } pos;

    // velocity
    Vector2D vel = Vector2D(0, 0);

    Direction direction = None;

    SDL_Colour colour;
    byte player_num;
    bool should_predict; // should predict the movement
    
    bool is_on_ground = false;
    bool has_jumped = false;

    void place(int x, int y, float vel_x, float vel_y);
    void move(Map &map);
    void handle_event(SDL_Event& event);
    void render(SDL_Renderer* renderer) const;
};

#endif // ADHTP_PLAYER_HDR

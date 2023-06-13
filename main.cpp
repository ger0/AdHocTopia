#include <SDL2/SDL_events.h>
#include <SDL2/SDL_pixels.h>
#include <cstdlib>
#include <cstring>

#include <unordered_map>
#include <vector>
#include <array>

#include <SDL2/SDL.h>

#include "math.hpp"
#include "types.hpp"
#include "networking.hpp"

constexpr uint PORT = 2113;
constexpr uint MAP_PORT = 2114;

constexpr double TICK_RATE      = 32;
constexpr double TICK_MSEC_DUR  = 1'000 / TICK_RATE;

uint SEED = 0;

enum CellType: byte {
    EMPTY   = 0x00,
    WALL    = 0xff,
    START   = 0xf0,
    FINISH  = 0x0f,
};

enum GameState {
    Initializing,
    Drawing,
    Connecting,
    Playing,
    Ending,
};

SDL_Colour get_cell_colour(CellType cell) {
    switch (cell) {
        case CellType::START:
            return {0,255,0,255};
        case CellType::FINISH:  
            return {255,0,0,255};
        case CellType::WALL:
            return {100,100,100,255};
        case CellType::EMPTY:
            return {0,0,0,255};
        default:
            return {255,0,255,255};
    }
}

struct Map {
    static constexpr int WIDTH  = 800;
    static constexpr int HEIGHT = 600;
    static constexpr int SIZE   = WIDTH * HEIGHT;
    std::array<byte, SIZE> data;

    Map() {
        data.fill(0);
    }

    SDL_Texture *_texture   = nullptr;
    bool        _is_drawing = false;
    CellType    _brush_type = CellType::EMPTY;

    inline byte &at(const int x, const int y) {
        return data.at(x + y * WIDTH);
    }
    inline byte at_bnd(const int x, const int y) const {
        if (x <= 0 || x >= WIDTH - 1|| y <= 0 || y >= HEIGHT - 1) return CellType::WALL;
        else return data.at(x + y * WIDTH);
    }

    void _write_at(const int x, const int y, const int size) {
        for (int iy = y; iy < y + size; iy++) {
            for (int ix = x; ix < x + size; ix++) {
                if (ix >= WIDTH || ix < 0 || iy >= HEIGHT || iy < 0) continue;
                this->at(ix, iy) = _brush_type;
            }
        }
    }
    void _draw_at(const int x, const int y, const int size) {
        // Set the target texture
        SDL_SetRenderTarget(SDL_GetRenderer(SDL_GetWindowFromID(1)), _texture);

        // Draw
        SDL_Rect rect = {x, y, size, size};
        SDL_Color col = get_cell_colour(_brush_type);
        SDL_SetRenderDrawColor(
            SDL_GetRenderer(SDL_GetWindowFromID(1)), col.r, col.g, col.g, col.a);
        SDL_RenderFillRect(
            SDL_GetRenderer(SDL_GetWindowFromID(1)), &rect);

        // Reset the target to the default rendering target (the window)
        SDL_SetRenderTarget(SDL_GetRenderer(SDL_GetWindowFromID(1)), NULL);
    }

    Vector2D refl_vector(Vector2D const &vect, const float mx, const float my) {
        const int x = mx + 0.5f;
        const int y = my + 0.5f;
        float x0 = ((Map*)this)->at_bnd(x-1, y-1) / 3.f
                 + ((Map*)this)->at_bnd(x-1, y  ) / 3.f
                 + ((Map*)this)->at_bnd(x-1, y+1) / 3.f;

        float x1 = ((Map*)this)->at_bnd(x+1, y-1) / 3.f
                 + ((Map*)this)->at_bnd(x+1, y  ) / 3.f
                 + ((Map*)this)->at_bnd(x+1, y+1) / 3.f;

        float y0 = ((Map*)this)->at_bnd(x-1, y+1) / 3.f
                 + ((Map*)this)->at_bnd(x  , y+1) / 3.f
                 + ((Map*)this)->at_bnd(x+1, y+1) / 3.f;

        float y1 = ((Map*)this)->at_bnd(x-1, y-1) / 3.f
                 + ((Map*)this)->at_bnd(x  , y-1) / 3.f
                 + ((Map*)this)->at_bnd(x+1, y-1) / 3.f;

        // gradient
        Vector2D surf_grad = {x1 - x0, y1 - y0};
        surf_grad.normalize();
        Vector2D norm_vect = vect;
        norm_vect.normalize();
        
        return reflect(norm_vect, surf_grad);
    }

    void handle_event(SDL_Event &event) {
        int x, y;

        const auto& ksymbl  = event.key.keysym.sym;
        const auto& embttn  = event.button;

        switch (event.type) {
        case SDL_KEYDOWN:
            if (ksymbl == SDLK_s) {
                _brush_type = CellType::START;
            } else if (ksymbl == SDLK_f) {
                _brush_type = CellType::FINISH;
            } else if (ksymbl == SDLK_w) {
                _brush_type = CellType::WALL;
            } else {
                _brush_type = CellType::EMPTY;
            }
            return;
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (embttn.button != SDL_BUTTON_LEFT) break;
            _is_drawing = true;
            x = embttn.x;
            y = embttn.y;
            break;
        case SDL_MOUSEBUTTONUP:
            if (embttn.button != SDL_BUTTON_LEFT) break;
            _is_drawing = false;
            return;
            break;
        case SDL_MOUSEMOTION: 
            x = event.motion.x;
            y = event.motion.y;
            break;
        } 
        if (_is_drawing && _brush_type != EMPTY) {
            constexpr int size = 18;
            this->_draw_at(x, y, size);
            this->_write_at(x, y, size);
        }
    }
} MAP;

enum Direction {
    None,
    Left,
    Right,
};

struct Player {
    static constexpr float JUM_CAP  = 5.f;
    static constexpr float VEL_CAP  = 2.f; //cap
    static constexpr float ACCEL    = 0.3f;
    static constexpr float DECCEL   = 0.85f;
    static constexpr float GRAVITY  = 0.3f; 
    const struct {
        int WIDTH   = 8;
        int HEIGHT  = 12;
    } SIZE;

    byte player_num;
    bool should_predict; // should predict the movement
    SDL_Colour colour;
    
    // position
    struct {
        int x = 10;
        int y = 10;
    } pos;
    // velocity
    Vector2D vel = Vector2D(0, 0);

    Direction direction = None;

    void place(int x, int y, int dx, int dy) {
        this->pos.x = x;
        this->pos.y = y;
        this->vel.x = dx;
        this->vel.y = dy;
    }

    //uint bounces = 0;
    bool is_on_ground = false;
    bool has_jumped = false;

    bool unstuck_walls() {
        for (int off = 0; off < SIZE.HEIGHT; ++off) {
            if (MAP.at_bnd(pos.x, pos.y - off) == CellType::EMPTY) {
                pos.y = pos.y - off;
                return true;
            }
        }
        return false;
    }
    void move_down() {
        for (int off = 0; off > -SIZE.HEIGHT; --off) {
            if (MAP.at_bnd(pos.x, pos.y - off) == CellType::EMPTY) {
                pos.y = pos.y - off;
                return;
            }
        }
    }

    // [TODO] Refactor this nightmare
    void move_flight() {
        vel.y += GRAVITY;
    
        int prev_x = pos.x;
        int prev_y = pos.y;

        int curr_x, curr_y;

        // MESSY BOUNDARY CHECKING
        Vector2D step_vel = vel;
        step_vel.normalize();

        // a partial dist
        float step_x = prev_x;
        float step_y = prev_y;

        // max distance to the last possible point
        Vector2D distance(
            (pos.x + vel.x) - pos.x, (pos.y + vel.y) - pos.y
        );
        float distance_len = distance.length();

        Vector2D distance_taken(0.f, 0.f);
        float distance_remain = distance_len - 0.f;

        // the cell
        byte step_cell;
        do {
            curr_x = (int)(step_x + 0.5f);
            curr_y = (int)(step_y + 0.5f);

            step_cell = MAP.at_bnd(curr_x, curr_y);
            if (step_cell == CellType::WALL) {
                //LOG_ERR("WALL!");
                break;
            }
            step_x += step_vel.x;
            step_y += step_vel.y;

            distance_taken.x = step_x - pos.x;
            distance_taken.y = step_y - pos.y;

            distance_remain = distance_len - distance_taken.length();
        } while (distance_remain > 0.f);

        Vector2D refl_vector = MAP.refl_vector(vel, step_x, step_y);

        pos.x = (int)(step_x + 0.5f);
        pos.y = (int)(step_y + 0.5f);

        // Collision with wall, move the player back
        if (step_cell == CellType::WALL) {
            if (vel.y > 0.f) {
                unstuck_walls(); // todo
                has_jumped = false;
                //++bounces;
            } //else bounces = 0;
            // [TODO] fix
            else {
                pos.x = prev_x + vel.x + 0.5f;
                pos.y = prev_y + vel.y + 0.5f;
            }
            vel.y = -refl_vector.y * DECCEL;
            vel.x =  refl_vector.x;
        }
    } 

    void move() {
        // is on ground?
        byte curr_cell = MAP.at_bnd(pos.x, pos.y);
        // if standing on a wall 
        if (MAP.at_bnd(pos.x, pos.y + 1) == CellType::WALL 
                && curr_cell != CellType::WALL
                && !has_jumped) { //&& bounces >= abs(vel.y / 3)) {
            //bounces = 0;
            is_on_ground = true;
        } 
        // if in the air 
        else {
            is_on_ground = false;
        }

        // Handle movements:
        switch (direction) {
        case Left:
            vel.x = vel.x <= -VEL_CAP ? -VEL_CAP : vel.x - ACCEL;
            break;
        case Right:
            vel.x = vel.x >= VEL_CAP ? VEL_CAP : vel.x + ACCEL;
            break;
        case None:
            if (vel.x > -0.01f && vel.x < 0.01f) {
                vel.x = 0.f;
            } else {
                vel.x *= DECCEL;
            }
            break;
        };
        if (!is_on_ground) {
            move_flight();
            return;
        }
        has_jumped = false; 
        // else is walking
        bool success = true;
        pos.x += vel.x + 0.5f; 
        vel.y = 0.f;

        curr_cell = MAP.at_bnd(pos.x, pos.y);
        if (curr_cell == CellType::WALL) {
            success = unstuck_walls();
        } else {
            move_down();
        }
        if (!success) {
            vel.x = -vel.x * DECCEL;
            pos.x += vel.x + 0.5f;
        }
    }

    void handle_event(SDL_Event& event) {
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            switch (event.key.keysym.sym) {
            case SDLK_LEFT:
                if (direction == Right) direction = None;
                else                    direction = Left;
                break;
            case SDLK_RIGHT:
                if (direction == Left)  direction = None;
                else                    direction = Right;
                break;
            case SDLK_UP:
                if (!has_jumped) {
                    vel.y -= JUM_CAP;
                    has_jumped = true;
                }
                break;
            }
        } else if (event.type == SDL_KEYUP && event.key.repeat == 0) {
            switch (event.key.keysym.sym) {
            case SDLK_LEFT:
                if (direction == Left)  direction = None;
                else                    direction = Right;
                break;
            case SDLK_RIGHT:
                if (direction == Right) direction = None;
                else                    direction = Left;
                break;
            }
        }
    }

    void render(SDL_Renderer* renderer) const {
        auto mid_w = SIZE.WIDTH  / 2;
        auto mid_h = SIZE.HEIGHT / 2;
        SDL_Rect rect = {pos.x - mid_w, pos.y - mid_h, mid_w, mid_h};
        const auto& c = colour;
        SDL_SetRenderDrawColor(renderer, 255, 75, 0, 255);
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(renderer, &rect);
    }
};

GameState poll_events(GameState state, SDL_Event &event, Player &player) {
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            return GameState::Ending;
        }
        if (state == Playing) {
            player.handle_event(event);
        }
        else if (state == Drawing) {
            MAP.handle_event(event);
        } 
        if (event.type == SDL_KEYDOWN 
            && event.key.keysym.sym == SDLK_SPACE) {
            if (state == Drawing) state = Connecting;
            LOG_DBG("Trying to connect to other players...", state);
        }
    }
    return state;
}

GameState poll_packets(GameState state, std::unordered_map<byte, Player> &enemies) {
    auto packets = networking::poll();

    // [TODO]: Refactor
    for (const auto &pkt: packets) {
        // if (pkt.player_num == player_num) continue;
        // updating the position of a player
        if (pkt.opcode == networking::Opcode::Coord) {
            //if (!enemies.contains(pkt.player_num)) continue;
            auto [x, y]     = pkt.payload.move.coord;
            auto [dx, dy]   = pkt.payload.move.d_vel;
            auto& enemy = enemies[pkt.player_num];
            enemy.place(x, y, dx, dy);
            enemy.should_predict = false;
        } 
        // adding a new player
        else if (pkt.opcode == networking::Opcode::Hello) {
            // sending THIS PLAYER'S id with ACK
            networking::ack_to_player(pkt.player_num);
            if (enemies.contains(pkt.player_num)) continue;
            Player enemy;
            enemy.colour = {
                .r = byte(155 + SEED % 100),
                .g = byte((SEED / 256) % 256),
                .b = byte((SEED / (256 * 256)) % 256),
                .a = byte(255)
            };
            enemies.emplace(pkt.player_num, enemy);
            LOG("Player: {} connected to the game!", pkt.player_num);
        }
        else if (pkt.opcode == networking::Opcode::Ack) {
            LOG("Player: {} accepted to the game!", pkt.player_num);
            state = Playing;
        }
    }
    return state;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        LOG("Usage: {} <device> <essid> <ip_address>", argv[0]);
        return EXIT_FAILURE;
    }
    GameState game_state = Initializing;
    const char net_msk[16]  = "255.255.255.0";
    const char bd_addr[16]  = "15.0.0.255";
    char ip_addr[16]        = "15.0.0.";
    strcat(ip_addr, argv[3]);
    const byte player_num = atoi(argv[3]);

    auto cfg = networking::NetConfig {
        .device     = argv[1],
        .essid      = argv[2],
        .ip_addr    = ip_addr,
        .net_msk    = net_msk,
        .bd_addr    = bd_addr,
        .port       = PORT
    };
    defer {networking::destroy();};
    if (!networking::setup(cfg)) {
        perror("What"); 
        return EXIT_FAILURE;
    }

    // -------------------------- sdl  init ---------------------------
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow(
        argv[0], 
        SDL_WINDOWPOS_UNDEFINED, 
        SDL_WINDOWPOS_UNDEFINED,
        MAP.WIDTH, MAP.HEIGHT, SDL_WINDOW_SHOWN
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, 
        -1, 
        SDL_RENDERER_ACCELERATED
    );
    SDL_Texture* map_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        MAP.WIDTH, MAP.HEIGHT 
    );
    MAP._texture = map_texture;

    defer {SDL_DestroyTexture(map_texture);};
    defer {SDL_DestroyRenderer(renderer);};
    defer {SDL_DestroyWindow(window);};
    defer {SDL_Quit();};

    // Clear the texture to a specific color
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    // Reset the target to the default rendering target (the window)
    SDL_SetRenderTarget(renderer, NULL);

    Player player;
    std::unordered_map<byte, Player> enemies;
    player.colour = {255, 255, 255, 255};
    player.player_num = player_num;


    srand(time(NULL));
    SEED = rand();

    // -------------------------- main loop ---------------------------
    SDL_Event event;
    game_state = Drawing;

    u64 prev_tick = SDL_GetTicks64();
    u64 curr_tick = prev_tick;
    u64 delta_time;

    constexpr u64 fps_cap = 240;
    constexpr u64 frame_min_dur = 1'000 / fps_cap;
    u64 prev_frame = SDL_GetTicks64();
    u64 delta_frame;

    // [TODO] Refactor
    while (game_state != GameState::Ending) {
        game_state = poll_events(game_state, event, player);
        game_state = poll_packets(game_state, enemies);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, map_texture, NULL, NULL);
        if (game_state == Playing) {
            player.move();
            for (auto& [_, enemy]: enemies) {
                if (enemy.should_predict) enemy.move();
                else enemy.should_predict = true;
                enemy.render(renderer);
            }
            player.render(renderer);
        }
        SDL_RenderPresent(renderer);

        // tick
        networking::Packet pkt = {
            .opcode     = networking::Opcode::Coord,
            .player_num = player_num,
            .seq        = 0,
            .payload    = {player.pos.x, player.pos.y, 
                player.vel.x, player.vel.y},
        };

        curr_tick = SDL_GetTicks64();
        delta_time = curr_tick - prev_tick;
        if ((double)delta_time >= TICK_MSEC_DUR) {
            if (game_state == Playing) {
                networking::broadcast(pkt);
            }
            else if (game_state == Connecting) {
                pkt.opcode = networking::Opcode::Hello;
                networking::broadcast(pkt);
            }
            prev_tick = curr_tick;
        }

        delta_frame = curr_tick - prev_frame;
        if (delta_frame < frame_min_dur) {
            SDL_Delay(uint(frame_min_dur - delta_frame));
        }
        prev_frame = curr_tick;
    }
    return EXIT_SUCCESS;
}

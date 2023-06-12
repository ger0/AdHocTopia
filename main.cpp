#include <SDL2/SDL_events.h>
#include <SDL2/SDL_pixels.h>
#include <cstdlib>
#include <cstring>

#include <unordered_map>
#include <vector>
#include <array>

//#include <net/if.h>

#include <SDL2/SDL.h>

#include "types.hpp"
#include "networking.hpp"

constexpr uint PORT = 2113;
constexpr uint MAP_PORT = 2114;

constexpr SDL_PixelFormatEnum TEXTURE_FORMAT = SDL_PIXELFORMAT_RGBA8888;

constexpr double TICK_RATE      = 32;
constexpr double TICK_MSEC_DUR  = 1'000 / TICK_RATE;

enum CellType: byte {
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
} GAME_STATE;

SDL_Colour get_cell_colour(CellType cell) {
    switch (cell) {
        case CellType::START:
            return {0,255,0,255};
        case CellType::FINISH:  
            return {255,0,0,255};
        case CellType::WALL:
            return {150,150,150,255};
        default:
            return {255,0,255,255};
    }
}

struct Map {
    static constexpr int WIDTH  = 800;
    static constexpr int HEIGHT = 600;
    static constexpr int SIZE   = WIDTH * HEIGHT;
    std::array<byte, SIZE> data;

    void write_at(const int x, const int y, const byte value, const int size) {
        for (int iy = y; iy < y + size; iy++) {
            for (int ix = x; ix < x + size; ix++) {
                auto idx = ix + iy * HEIGHT;
                if (idx >= SIZE || idx < 0) continue;
                data.at(ix + iy * HEIGHT) = value;
            }
        }
    }
    Map() {
        data.fill(0);
    }
    void handle_event(SDL_Texture* texture, SDL_Event &event) {
        int x, y;
        
        // static vars
        static bool _is_drawing;
        static CellType _brush_type;

        const auto& ksymbl  = event.key.keysym.sym;
        const auto& embttn  = event.button;

        switch (event.type) {
        case SDL_KEYDOWN:
            if (ksymbl == SDLK_s) {
                _brush_type = CellType::START;
            } else if (ksymbl == SDLK_f) {
                _brush_type = CellType::FINISH;
            } else {
                _brush_type = CellType::WALL;
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

        if (!_is_drawing) return;

        constexpr int size = 10;

        // Set the target texture
        SDL_SetRenderTarget(SDL_GetRenderer(SDL_GetWindowFromID(1)), texture);

        // Draw
        SDL_Rect rect = {x, y, size, size};
        SDL_Color col = get_cell_colour(_brush_type);
        SDL_SetRenderDrawColor(
            SDL_GetRenderer(SDL_GetWindowFromID(1)), col.r, col.g, col.g, col.a);
        SDL_RenderFillRect(
            SDL_GetRenderer(SDL_GetWindowFromID(1)), &rect);
        this->write_at(x, y, _brush_type, size);

        // Reset the target to the default rendering target (the window)
        SDL_SetRenderTarget(SDL_GetRenderer(SDL_GetWindowFromID(1)), NULL);
    }
} MAP;

struct Player {
    byte player_num;
    bool should_predict; // should predict the movement
    SDL_Colour colour;

    const struct {
        int WIDTH   = 10;
        int HEIGHT  = 20;
    } SIZE;

    static const uint VELOCITY = 10;
    
    int x = 0;
    int y = 0;

    // velocity
    int vel_x = 0;
    int vel_y = 0;

    void place(int x, int y, int dx, int dy) {
        this->x = x;
        this->y = y;
        this->vel_x = dx;
        this->vel_y = dy;
    }

    void move() {
        x += vel_x;
        if (x < 0 || x + SIZE.WIDTH > MAP.WIDTH) {
            x -= vel_x;
        }
        y += vel_y;
        if (y < 0 || y + SIZE.HEIGHT > MAP.HEIGHT) {
            y -= vel_y;
        }
    }
    // [TODO] add acceleration and get rid of those switches
    void handle_event(SDL_Event& event) {
        if (event.type == SDL_KEYDOWN 
            && event.key.repeat == 0) {
            switch (event.key.keysym.sym) {
            case SDLK_LEFT:
                vel_x -= VELOCITY;
                break;
            case SDLK_RIGHT:
                vel_x += VELOCITY;
                break;
            case SDLK_UP:
                vel_y -= VELOCITY;
                break;
            case SDLK_DOWN:
                vel_y += VELOCITY;
                break;
            }
        } else if (event.type == SDL_KEYUP 
                && event.key.repeat == 0) {
            switch (event.key.keysym.sym) {
            case SDLK_LEFT:
                vel_x += VELOCITY;
                break;
            case SDLK_RIGHT:
                vel_x -= VELOCITY;
                break;
            case SDLK_UP:
                vel_y += VELOCITY;
                break;
            case SDLK_DOWN:
                vel_y -= VELOCITY;
                break;
            }
        }
    }
    void render(SDL_Renderer* renderer) const {
        SDL_Rect rect = { x, y, SIZE.WIDTH, SIZE.HEIGHT };
        const auto& c = colour;
        SDL_SetRenderDrawColor(renderer, 255, 75, 0, 255);
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(renderer, &rect);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        LOG("Usage: {} <device> <essid> <ip_address>", argv[0]);
        return EXIT_FAILURE;
    }
    GAME_STATE = Initializing;
    const char net_msk[16]    = "255.255.255.0";
    const char bd_addr[16]    = "15.0.0.255";
    char ip_addr[16]    = "15.0.0.";
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
        argv[0], SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        MAP.WIDTH, MAP.HEIGHT, SDL_WINDOW_SHOWN
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* map_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        MAP.WIDTH, MAP.HEIGHT 
    );

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

    GAME_STATE = Drawing;

    srand(time(NULL));
    uint seed = rand();

    // -------------------------- main loop ---------------------------
    SDL_Event event;
    bool is_running = true;

    u64 prev_tick = SDL_GetTicks64();
    u64 curr_tick = prev_tick;
    u64 delta_time;

    constexpr u64 fps_cap = 240;
    constexpr u64 frame_min_dur = 1'000 / fps_cap;
    u64 prev_frame = SDL_GetTicks64();
    u64 delta_frame;

    while (is_running) {
        // [TODO]: Refactor
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                is_running = false;
            }
            if (GAME_STATE == Playing) {
                player.handle_event(event);
            }
            else if (GAME_STATE == Drawing) {
                MAP.handle_event(map_texture, event);
            } 
            if (event.type == SDL_KEYDOWN 
                && event.key.keysym.sym == SDLK_SPACE) {
                if (GAME_STATE == Drawing) GAME_STATE = Connecting;
                LOG("Trying to connect to other players...", GAME_STATE);
            }
        }
        auto packets = networking::poll();
        // [TODO]: Refactor
        for (const auto &pkt: packets) {
            // if (pkt.player_num == player_num) continue;
            // updating the position of a player
            if (pkt.opcode == networking::Opcode::Coord) {
                //if (!enemies.contains(pkt.player_num)) continue;
                auto [x, y]     = pkt.payload.move.coord;
                auto [dx, dy]   = pkt.payload.move.d_vel;
                LOG_DBG("Received coords: {}, {}", x, y);
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
                    .r = byte(155 + seed % 100),
                    .g = byte((seed / 256) % 256),
                    .b = byte((seed / (256 * 256)) % 256),
                    .a = byte(255)
                };
                enemies.emplace(pkt.player_num, enemy);
                LOG("Player: {} connected to the game!", pkt.player_num);
            }
            // possible bug?
            else if (pkt.opcode == networking::Opcode::Ack) {
                LOG("Received ACK from player {}", pkt.player_num);
                LOG("ACK: enemy {} mine {}", pkt.player_num, player_num);
                GAME_STATE = Playing;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, map_texture, NULL, NULL);
        if (GAME_STATE == Playing) {
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
            .payload    = {player.x, player.y, 
                player.vel_x, player.vel_y},
        };

        curr_tick = SDL_GetTicks64();
        delta_time = curr_tick - prev_tick;
        if ((double)delta_time >= TICK_MSEC_DUR) {
            LOG_DBG("Broadcasting Data...");
            if (GAME_STATE == Playing) {
                networking::broadcast(pkt);
            }
            else if (GAME_STATE == Connecting) {
                pkt.opcode = networking::Opcode::Hello;
                networking::broadcast(pkt);
            }
            prev_tick = curr_tick;
        }

        delta_frame = curr_tick - prev_frame;
        if (delta_frame < frame_min_dur) {
            SDL_Delay(uint(frame_min_dur - delta_frame));
            prev_frame = curr_tick;
        }
    }
    return EXIT_SUCCESS;
}

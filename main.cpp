#include <SDL2/SDL_events.h>
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

constexpr double TICK_RATE      = 32;
constexpr double TICK_MSEC_DUR  = 1'000 / TICK_RATE;

struct Map {
    static constexpr int WIDTH  = 1280;
    static constexpr int HEIGHT = 720;
} MAP;

void draw_on_texture(SDL_Texture* texture, SDL_Event &event) {
    int x, y;
    static bool _is_drawing;
    if (event.type == SDL_MOUSEBUTTONDOWN
        && event.button.button == SDL_BUTTON_LEFT) {
        _is_drawing = true;
        x = event.button.x;
        y = event.button.y;
    } else if (event.type == SDL_MOUSEBUTTONUP 
        && event.button.button == SDL_BUTTON_LEFT) {
        _is_drawing = false;
    } else if (event.type == SDL_MOUSEMOTION) {
        x = event.motion.x;
        y = event.motion.y;
    } 
    if (!_is_drawing) return;

    // Set the target texture
    SDL_SetRenderTarget(SDL_GetRenderer(SDL_GetWindowFromID(1)), texture);

    // Draw on the texture
    SDL_Rect rect = {x, y, 10, 10 };
    SDL_SetRenderDrawColor(
        SDL_GetRenderer(SDL_GetWindowFromID(1)), 175, 175, 175, 255);
    SDL_RenderFillRect(
        SDL_GetRenderer(SDL_GetWindowFromID(1)), &rect);

    // Reset the target to the default rendering target (the window)
    SDL_SetRenderTarget(SDL_GetRenderer(SDL_GetWindowFromID(1)), NULL);
}

enum GameState {
    Initializing,
    Drawing,
    Connecting,
    Playing,
    Ending
} GAME_STATE;

struct Player {
    byte player_num;
    bool should_predict; // should predict the movement
    struct {
        uint r = 0;
        uint g = 0;
        uint b = 0;
    } colour;

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
    void handle_event(SDL_Event& event) {
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
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
        }
        else if (event.type == SDL_KEYUP && event.key.repeat == 0) {
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
    player.colour = {255, 255, 255};
    player.player_num = player_num;

    GAME_STATE = Drawing;

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
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) is_running = false;
            if (GAME_STATE == Playing) player.handle_event(event);
            else if (GAME_STATE == Drawing) {
                draw_on_texture(map_texture, event);
            } 
            if (event.type == SDL_KEYDOWN 
                && event.key.keysym.sym == SDLK_SPACE) {
                GAME_STATE = GAME_STATE == Drawing ? Connecting : Playing;
                LOG("Changing state {}...", GAME_STATE);
            }
        }
        player.move();

        auto packets = networking::poll();
        // [TODO]: Refactor
        for (const auto &pkt: packets) {
            if (pkt.opcode == networking::Opcode::Coord) {
                if (pkt.player_num == player_num) continue;
                auto [x, y]     = pkt.payload.move.coord;
                auto [dx, dy]   = pkt.payload.move.d_vel;
                LOG_DBG("Received coords: {}, {}", x, y);
                auto& enemy = enemies[pkt.player_num];
                enemy.place(x, y, dx, dy);
                enemy.should_predict = false;
            } 
            else if (pkt.opcode == networking::Opcode::Hello) {
                networking::ack_to_player(pkt.player_num);
                if (enemies.contains(pkt.player_num)) continue;
                uint seed = rand();
                Player enemy;
                enemy.colour = {
                    .r = seed % 256,
                    .g = (seed / 256) % 256,
                    .b = (seed / (256 * 256)) % 256
                };
                enemies.emplace(pkt.player_num, enemy);
                LOG("Player: {} connected to the game!", pkt.player_num);
            }
            // possible bug?
            else if (pkt.opcode == networking::Opcode::Ack) {
                GAME_STATE = Playing;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, map_texture, NULL, NULL);
        if (GAME_STATE == Playing) {
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

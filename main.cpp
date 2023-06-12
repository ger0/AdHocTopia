#include <SDL2/SDL_events.h>
#include <cstdlib>
#include <cstring>

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
    static constexpr int WIDTH  = 800;
    static constexpr int HEIGHT = 600;
} MAP;

struct Player {
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

    void place(int x, int y) {
        this->x = x;
        this->y = y;
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
    void render(SDL_Renderer* renderer) {
        SDL_Rect playerRect = { x, y, SIZE.WIDTH, SIZE.HEIGHT };
        const auto& c = colour;
        SDL_SetRenderDrawColor(renderer, 255, 75, 0, 255);
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(renderer, &playerRect);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        LOG("Usage: {} <device> <essid> <ip_address>", argv[0]);
        return EXIT_FAILURE;
    }
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

    defer {SDL_DestroyRenderer(renderer);};
    defer {SDL_DestroyWindow(window);};
    defer {SDL_Quit();};

    Player player;
    player.colour = {0, 100, 255};
    Player enemy;
    enemy.colour = {255, 0, 0};

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
            player.handle_event(event);
        }
        player.move();
        auto packets = networking::poll();
        for (const auto &pkt: packets) {
            if (pkt.opcode != networking::Opcode::Coord) continue;
            if (pkt.player_num == player_num) continue;
            auto [x, y] = pkt.payload.coord;
            LOG_DBG("Received coords: {}, {}", x, y);
            enemy.place(x, y);
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        player.render(renderer);
        enemy.render(renderer);
        SDL_RenderPresent(renderer);

        // tick
        networking::Packet pkt = {
            .opcode     = networking::Opcode::Coord,
            .player_num = player_num,
            .payload    = {player.x, player.y}
        };

        curr_tick = SDL_GetTicks64();
        delta_time = curr_tick - prev_tick;
        if ((double)delta_time >= TICK_MSEC_DUR) {
            LOG_DBG("Broadcasting Data...");
            networking::broadcast(pkt);
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

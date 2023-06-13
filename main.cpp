#include <SDL2/SDL_events.h>
#include <SDL2/SDL_pixels.h>
#include <cstdlib>
#include <cstring>

#include <unordered_map>
#include <vector>
#include <array>

#include <SDL2/SDL.h>

#include "math.hpp"
#include "Map.hpp"
#include "types.hpp"
#include "networking.hpp"
#include "Player.hpp"

constexpr uint PORT = 2113;
constexpr uint MAP_PORT = 2114;

constexpr double TICK_RATE      = 32;
constexpr double TICK_MSEC_DUR  = 1'000 / TICK_RATE;

uint SEED = 0;

enum GameState {
    Initializing,
    Drawing,
    Connecting,
    Playing,
    Ending,
};

GameState poll_events(GameState state, Map &map, SDL_Event &event, Player &player) {
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            return GameState::Ending;
        }
        if (state == Playing) {
            player.handle_event(event);
        }
        else if (state == Drawing) {
            map.handle_event(event);
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
    Map map;

    // -------------------------- sdl  init ---------------------------
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow(
        argv[0], 
        SDL_WINDOWPOS_UNDEFINED, 
        SDL_WINDOWPOS_UNDEFINED,
        Map::WIDTH, Map::HEIGHT, SDL_WINDOW_SHOWN
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
        Map::WIDTH, Map::HEIGHT 
    );
    map._texture = map_texture;

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
        game_state = poll_events(game_state, map, event, player);
        game_state = poll_packets(game_state, enemies);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, map_texture, NULL, NULL);
        if (game_state == Playing) {
            player.move(map);
            for (auto& [_, enemy]: enemies) {
                if (enemy.should_predict) enemy.move(map);
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

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

using PlayersHMap = std::unordered_map<byte, Player>;

constexpr uint PORT = 2113;
constexpr uint MAP_PORT = 2114;

constexpr double TICK_RATE      = 32;
constexpr double TICK_MSEC_DUR  = 1'000 / TICK_RATE;

static byte PLAYER_NUM;
static uint SEED = 0;

// ------------- global variables --------------

static Player player;
static PlayersHMap enemies;
static Map map;

static enum GameState {
    Initializing,   // network conf, sdl setup...
    Drawing,        // drawing the map
    Connecting,     // trying to connect to all players
    Streaming,      // streaming the map to all users
    Playing,        // playing the game
    Ending,         // finishing
} game_state;

void poll_events(SDL_Event &event) {
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            game_state = GameState::Ending;
        }
        if (game_state == Playing) {
            player.handle_event(event);
        }
        else if (game_state == Drawing) {
            map.handle_event(event);
        } 
        if (event.type == SDL_KEYDOWN 
            && event.key.keysym.sym == SDLK_SPACE) {
            if (game_state == Drawing) game_state = Connecting;
            LOG_DBG("Game state {}...", game_state);
        }
    }
}

bool is_everyone_ready(PlayersHMap enemies) {
    for (auto& [pl_num, enemy]: enemies) {
        if (!enemy.ready_to_play) return false;
    }
    return true;
}

void start_tcp_stream(const networking::Packet& pkt) {
    if (PLAYER_NUM > pkt.player_num) {
        uint buff_size = pkt.payload.map_buff_size;
        networking::connect_to_player(pkt.player_num, buff_size);
    } else {
        networking::set_tcp_buffer(map.data.data(), Map::SIZE);
        networking::listen_to_players();
        if (is_everyone_ready(enemies)) {
            //
        }
        LOG("Player: {} accepted to the game!", pkt.player_num);
    }
    game_state = Streaming;
}


void poll_packets() {
    auto packets = networking::poll();

    // [TODO]: Refactor
    for (const auto &pkt: packets) {
        /* updating the position of a player    *
         * sets the state to PLAYING            */
        if (pkt.opcode == networking::Opcode::Coord) {
            // if (!enemies.contains(pkt.player_num)) continue;
            auto [x, y]     = pkt.payload.move.coord;
            auto [dx, dy]   = pkt.payload.move.d_vel;
            auto& enemy = enemies[pkt.player_num];
            enemy.set_new_data(x, y, dx, dy);
            enemy.should_predict = false;

            game_state = Playing;
        } 
        // adding a new player
        else if (pkt.opcode == networking::Opcode::Hello) {

            // sending TO pkt.player_num - id the ACK
            networking::ack_to_player(pkt.player_num, PLAYER_NUM, Map::SIZE);
            
            // already exists
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
        // STATE IS CONNECTING 
        else if (game_state == Connecting && pkt.opcode == networking::Opcode::Ack) {
            start_tcp_stream(pkt);
        } if (pkt.opcode == networking::Opcode::Done_TCP) {
            // copy the buffer data to the map
            auto tcp_buff = networking::return_tcp_buffer();
            map.update(tcp_buff);
        }
    }
}

void render_clear(SDL_Renderer *renderer, SDL_Texture *map_texture) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, map_texture, NULL, NULL);
}

void send_udp_packets() {
    networking::Packet pkt = {
        .opcode     = networking::Opcode::Coord,
        .player_num = PLAYER_NUM,
        .seq        = 0,
        .payload    = {player.pos.x, player.pos.y, 
            player.vel.x, player.vel.y},
    };
    if (game_state == Playing) {
        networking::broadcast(pkt);
    }
    else if (game_state == Connecting) {
        pkt.opcode = networking::Opcode::Hello;
        networking::broadcast(pkt);
    } //else if
}

void display_players(SDL_Renderer *renderer) {
    player.update_position(map);
    for (auto& [_, enemy]: enemies) {
        if (enemy.should_predict) enemy.update_position(map);
        else enemy.should_predict = true;
        enemy.render(renderer);
    }
    player.render(renderer);
}


int main(int argc, char* argv[]) {
    if (argc < 5) {
        LOG("Usage: {} <device> <essid> <player_id 1-254>\
<player_count 0-255>", argv[0]);
        return EXIT_FAILURE;
    }
    game_state = Initializing;
    const char net_msk[16]  = "255.255.255.0";
    const char bd_addr[16]  = "15.0.0.255";
    char ip_addr[16]        = "15.0.0.";
    strcat(ip_addr, argv[3]);
    PLAYER_NUM = atoi(argv[3]);
    const uint player_cnt = (uint)atoi(argv[4]);

    auto cfg = networking::NetConfig {
        .device     = argv[1],
        .essid      = argv[2],
        .ip_addr    = ip_addr,
        .net_msk    = net_msk,
        .bd_addr    = bd_addr,
        .pr_numb    = PLAYER_NUM,
        .port       = PORT,
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
    player.colour = {255, 255, 255, 255};
    player.player_num = PLAYER_NUM;


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
        poll_events(event);
        poll_packets();

        render_clear(renderer, map_texture);
        if (game_state == Playing) {
            display_players(renderer);
        }
        SDL_RenderPresent(renderer);

        // tick synchro
        curr_tick = SDL_GetTicks64();
        delta_time = curr_tick - prev_tick;
        if ((double)delta_time >= TICK_MSEC_DUR) {
            prev_tick = curr_tick;
            send_udp_packets();
        }
        // rendering synchro
        delta_frame = curr_tick - prev_frame;
        if (delta_frame < frame_min_dur) {
            SDL_Delay(uint(frame_min_dur - delta_frame));
        }
        prev_frame = curr_tick;
    }
    return EXIT_SUCCESS;
}

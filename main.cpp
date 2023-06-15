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

enum GameState {
    Initializing,   // network conf, sdl setup...       -> ---
    Drawing,        // drawing the map                  -> ---
    Connecting,     // trying to connect to all players -> HELLO
    Streaming,      // streaming the map to all users   -> ACK
    Awaiting,       // Waiting for others               -> TCP
    Ready,          // waiting untill the min(id) starts-> TCP
    Playing,        // playing the game                 -> CoordSend
    Ending,         // finishing                        -> FIN
};

using PlayersHMap = std::unordered_map<byte, Player>;
using PlayersStates = std::unordered_map<byte, GameState>;

constexpr uint PORT = 2113;

// tick rate
constexpr double TICK_RATE      = 32;
constexpr double TICK_MSEC_DUR  = 1'000 / TICK_RATE;

static byte PLAYER_NUM;     // this player's number (based on id)
static byte N_PLAYERS;     // how many players should connect
static uint SEED = 0;
static byte SMALLEST_PLAYER_NUM;
static bool STARTED_LISTENING = false;

static bool PLAYING_SETUP;
static u64 PLAY_CLOCK;

// ------------- global variables --------------

static Player player;
static PlayersHMap enemies;
static Map map;

static GameState        game_state;
static PlayersStates    enemyies_states;

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

bool is_every_enemy(GameState expected) {
    if (enemyies_states.size() < N_PLAYERS) return false;
    for (auto& [_, state]: enemyies_states) {
        if (state == expected) return true;
    }
    return false;
}

void start_tcp_listening() {
    LOG_DBG("Started listening on TCP");
    networking::set_tcp_buffer(map.data.data(), Map::SIZE);
    networking::listen_to_players();
}

void start_tcp_reading(byte player_num, const uint byte_count) {
    networking::connect_to_player(player_num, byte_count);
    game_state = Streaming;
}

void change_game_state_up(GameState& prev, GameState new_state) {
    if (new_state > prev) {
        prev = new_state;
    }
}

void change_enemy_state(byte enemy, GameState new_state) {
    if (enemyies_states.find(enemy) == enemyies_states.end()) {
        enemyies_states.emplace(enemy, new_state);
    }
    change_game_state_up(enemyies_states.at(enemy), new_state);
}


void once_setup_playing_state() {
    if (PLAYING_SETUP) return;

    PLAY_CLOCK = SDL_GetTicks64();
    PLAYING_SETUP = true;

    if (!map.start_initialised) {
        return;
    }
    const auto& [x, y] = map.start_point;
    for (auto &[_, enemy]: enemies) {
        enemy.set_new_data(x, y, 0, 0);
    }
    player.set_new_data(x, y, 0, 0);
}

void poll_packets() {
    auto packets = networking::poll();

    // [TODO]: Refactor
    for (const auto &pkt: packets) {
        /* updating the position of a player    *
         * sets the state to PLAYING            */
        if (pkt.opcode == networking::Opcode::Coord) {
            auto [x, y]     = pkt.payload.move.coord;
            auto [dx, dy]   = pkt.payload.move.d_vel;
            auto& enemy = enemies[pkt.player_num];
            enemy.set_new_data(x, y, dx, dy);
            enemy.should_predict = false;

            change_game_state_up(game_state, Playing);
            once_setup_playing_state();
        } 
        // adding a new player
        else if (game_state != Drawing && pkt.opcode == networking::Opcode::Hello) {
            // already exists
            if (enemies.find(pkt.player_num) != enemies.end()) continue;

            change_enemy_state(pkt.player_num, GameState::Connecting);
            if (pkt.player_num < SMALLEST_PLAYER_NUM) SMALLEST_PLAYER_NUM = pkt.player_num;
             
            LOG("Player: {} connected to the game!", pkt.player_num);
            // when everyone has connected, change state 
            if (is_every_enemy(GameState::Connecting)) {
                LOG("All players are in the Connecting state");
                change_game_state_up(game_state, Streaming);

                // the map's owner starts accepting on the TCP connection wayy before others
                if (PLAYER_NUM == SMALLEST_PLAYER_NUM && STARTED_LISTENING == false) {
                    start_tcp_listening();
                    STARTED_LISTENING = true;
                }
            }

            Player enemy;
            enemy.pos = {Map::WIDTH / 2, Map::HEIGHT / 2};
            enemy.colour = {
                .r = byte(155 + SEED % 100),
                .g = byte((SEED / 256) % 256),
                .b = byte((SEED / (256 * 256)) % 256),
                .a = byte(255)
            };
            enemies.emplace(pkt.player_num, enemy);
        }
        // the lowest id sent ACK
        else if (pkt.opcode == networking::Opcode::Ack) {
            LOG("Player: {} accepted to the game!", pkt.player_num);
            change_enemy_state(pkt.player_num, GameState::Streaming);
            if (pkt.player_num < SMALLEST_PLAYER_NUM) SMALLEST_PLAYER_NUM = pkt.player_num;
            // If we failed to receive all Hellos
            change_game_state_up(game_state, Streaming);
            if (pkt.player_num <= SMALLEST_PLAYER_NUM){
                SMALLEST_PLAYER_NUM = pkt.player_num;
                LOG("Connecting to sender... {}", pkt.player_num);
                start_tcp_reading(SMALLEST_PLAYER_NUM, pkt.payload.map_buff_size);
            }
        } 
        // finished TCP operations on some socket
        if (pkt.opcode == networking::Opcode::Done_TCP) {
            // copy the buffer data to the map
            change_enemy_state(pkt.player_num, GameState::Ready);

            // if we are the owner of a map
            if (PLAYER_NUM == SMALLEST_PLAYER_NUM) {
                change_game_state_up(game_state, Ready);

                // check if everyone finished map stream 
                if (is_every_enemy(GameState::Ready)) {
                    change_game_state_up(game_state, Playing);
                    once_setup_playing_state();
                }
            }
            // otherwise load the map into the game
            else if (PLAYER_NUM != SMALLEST_PLAYER_NUM) {
                auto tcp_buff = networking::return_tcp_buffer();
                map.update(tcp_buff);
                change_game_state_up(game_state, Ready);
            }
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
    else if (game_state == Connecting || game_state == Streaming) {
        pkt.opcode = networking::Opcode::Hello;
        pkt.player_num = PLAYER_NUM;
        networking::broadcast(pkt);
    } 
    if (game_state == Streaming) {
        // Make others connect to the map's owner
        LOG_DBG("this: {} small: {}; SENDING ACK TO PLAYERS...", PLAYER_NUM, SMALLEST_PLAYER_NUM);
        if (PLAYER_NUM == SMALLEST_PLAYER_NUM) {
            pkt.opcode = networking::Opcode::Ack;
            pkt.payload.map_buff_size = map.SIZE;
            networking::broadcast(pkt);
        }
    }
}

void display_players(SDL_Renderer *renderer) {
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
    // check if true......
    PLAYER_NUM  = atoi(argv[3]);
    SMALLEST_PLAYER_NUM = PLAYER_NUM;
    N_PLAYERS   = (byte)atoi(argv[4]);

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

    player.pos = {Map::WIDTH / 2, Map::HEIGHT / 2};

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
            player.update_position(map);
            auto const& x = player.pos.x;
            auto const& y = player.pos.y;

            if (map.at_bnd(x, y) == FINISH) {
                PLAY_CLOCK = SDL_GetTicks64() - PLAY_CLOCK;
                LOG(" --------------------------------------------- ");
                LOG("       TIME ELAPSED (msec): {}", PLAY_CLOCK);
                LOG(" --------------------------------------------- ");
                change_game_state_up(game_state, Ending);
            }

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

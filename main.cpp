#include <SDL2/SDL_events.h>
#include <cstdlib>
#include <cstring>

#include <vector>
#include <array>

#include <fmt/ranges.h>
#include <fmt/core.h>

#include <arpa/inet.h>
#include <linux/wireless.h>

//#include <net/if.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <SDL2/SDL.h>

#define LOG(...) \
    fmt::print(stdout, "{}\n", fmt::format(__VA_ARGS__))
#define LOG_ERR(...) \
    fmt::print(stderr, "\033[1;31m{}\033[0m\n", fmt::format(__VA_ARGS__))

#define DEBUG
#ifdef DEBUG 
#define LOG_DBG(...) \
    fmt::print(stderr, "\033[1;33m{}\033[0m\n", fmt::format(__VA_ARGS__))
#else 
#define LOG_DBG(...) ;
#endif

#ifndef defer
struct defer_dummy {};
template <class F> struct deferrer { F f; ~deferrer() { f(); } };
template <class F> deferrer<F> operator*(defer_dummy, F f) { return {f}; }
#define DEFER_(LINE) zz_defer##LINE
#define DEFER(LINE) DEFER_(LINE)
#define defer auto DEFER(__LINE__) = defer_dummy{} *[&]()
#endif // defer

using uint  = unsigned int;
using byte  = unsigned char;
using c_str = const char*;

constexpr uint PORT = 2113;
constexpr uint MAX_BUFFER_SIZE = 1024;

struct Map {
    static constexpr int WIDTH     = 800;
    static constexpr int HEIGHT    = 600;
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
    static const uint VELOCITY = 1;
    
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

// setup adhoc mode and essid
bool setup_wlan(int sock, c_str device, c_str essid) {
    struct iwreq iwr; // wireless  settings
    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, device, IFNAMSIZ);

    memset(&iwr.u, 0, sizeof(iwr.u));
    iwr.u.mode |= IW_MODE_ADHOC;
    if (ioctl(sock, SIOCSIWMODE, &iwr)) {
        LOG_ERR("Failed to set the wireless interface to ad-hoc mode");
        return false;
    }

    iwr.u.essid.pointer = (caddr_t)essid;
    iwr.u.essid.length = strlen(essid);
    iwr.u.essid.flags = 1;
    if (ioctl(sock, SIOCSIWESSID, &iwr)) {
        LOG_ERR("Failed to set the ESSID");
        return false;
    }
    return true;
}
// setup ip_address, netmask and a few flags
bool setup_interface(int sock, c_str device, c_str ip_addr, c_str net_mask) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, device, IFNAMSIZ);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr)) {
        LOG_ERR("Failed to retrieve interface flags");
        return false;
    }
    // ip address 
    sockaddr_in sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = 0;
    if (inet_pton(AF_INET, ip_addr, &(sock_addr.sin_addr)) <= 0) {
        LOG_ERR("Invalid IP address");
		return false;
    }
    memcpy(&ifr.ifr_addr, &sock_addr, sizeof(sock_addr));
    if (ioctl(sock, SIOCSIFADDR, &ifr)) {
        LOG_ERR("Failed to set interface IP address");
		return false;
    }

    // subnet mask
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = 0;
    if (inet_pton(AF_INET, net_mask, &(sock_addr.sin_addr)) <= 0) {
        LOG_ERR("Invalid net mask");
		return false;
    }
    memcpy(&ifr.ifr_netmask, &sock_addr, sizeof(sock_addr));
    if (ioctl(sock, SIOCSIFNETMASK, &ifr)) {
        LOG_ERR("Failed to set net mask");
		return false;
    }

    // set the interface UP
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr)) {
        LOG_ERR("Failed to set interface flags");
		return false;
    }
    return true;
}

bool create_adhoc_network(c_str device, c_str essid, c_str ip_addr, c_str net_mask) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERR("Failed to create socket");
        return false;
    }
    // close socket opon returning
    defer {close(sock);};
    if (!setup_wlan(sock, device, essid)) return false;
    if (!setup_interface(sock, device, ip_addr, net_mask)) return false;
    LOG_DBG("Ad hoc network created successfully!");
    return true;
}

bool enable_sock_broadcast(int sfd) {
    int broadcastEnable = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_BROADCAST, 
                   &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        LOG_ERR("Failed to set socket options");
        perror("What");
        return false;
    }
    return true;
}

bool bind_addr(int sfd, c_str device, sockaddr_in &sin_addr) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

    // bind a specific device to the socket ( might not be used later on )
    if (setsockopt(sfd, SOL_SOCKET, SO_BINDTODEVICE,
                   (void *)&ifr, sizeof(ifr)) < 0) {
        LOG_ERR("Failed to bind device to the socket");
        return false;
    }
    int reuseAddr = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
        LOG_ERR("Failed to set SO_REUSEADDR");
        return false;
    }
    if (bind(sfd, (sockaddr*)&sin_addr, sizeof(sin_addr)) == -1) {
        LOG_ERR("Error during socket binding");
		return false;
    };
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        LOG("Usage: {} <device> <essid> <ip_address>", argv[0]);
        return EXIT_FAILURE;
    }
    c_str device        = argv[1];
    c_str essid         = argv[2];
    char net_msk[16]    = "255.255.255.0";
    char ip_addr[16]    = "15.0.0.";
    char bd_addr[16]    = "15.0.0.255";
    strcat(ip_addr, argv[3]);
    auto this_addr = inet_addr(ip_addr);

    if (!create_adhoc_network(device, essid, ip_addr, net_msk)) {
        perror("What"); return EXIT_FAILURE;
    }

    sockaddr_in broadcast = {
        .sin_family     = AF_INET,
        .sin_port       = htons(PORT),
        .sin_addr       = {inet_addr(bd_addr)}
    };
    sockaddr_in s_addr = {
        .sin_family     = AF_INET,
        .sin_port       = htons(PORT),
        .sin_addr       = {in_addr_t{INADDR_ANY}},
    };
    socklen_t sl = sizeof(s_addr);

    int sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    defer {close(sfd);};

    if(!bind_addr(sfd, device, s_addr)) {
        perror("What"); return EXIT_FAILURE;
    };

    if (!enable_sock_broadcast(sfd)) {
        exit(EXIT_FAILURE);
    }

    char buf[32] = "Hello World!";
    int coord[2] = {0, 0};
    int e_coord[2] = {0, 0};
    if (argc == 5) {
        strcpy(buf, argv[4]);
    }

    // -------------------------- sdl  init ---------------------------
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow(
        argv[0], SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        MAP.WIDTH, MAP.HEIGHT, SDL_WINDOW_SHOWN
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    Player player;
    player.colour = {0, 100, 255};
    Player enemy;
    enemy.colour = {255, 0, 0};
    defer {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    };

    // -------------------------- main loop ---------------------------
    constexpr uint MAX_EVENTS = 8;
    epoll_event ev, events[MAX_EVENTS];
    int nfds, rc;

    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        LOG_ERR("Epoll creation failed");
        perror("What"); return EXIT_FAILURE;
    }
    defer {close(epollfd);};

    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sfd, &ev) == -1) {
        LOG_ERR("Epoll_ctl failed");
        perror("What"); return EXIT_FAILURE;
    }

    // set the sfd to be nonblocking for epoll
    int flags = fcntl(sfd, F_GETFL, 0);
    fcntl(sfd, F_SETFL, flags | O_NONBLOCK);
    SDL_Event event;
    bool is_running = true;

    while (is_running) {
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) is_running = false;
            player.handle_event(event);
        }
        player.move();
        coord[0] = player.x;
        coord[1] = player.y;
        sendto(sfd, coord, sizeof(int) * 2, 0, (sockaddr*)&broadcast, sl);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        player.render(renderer);

        nfds = epoll_wait(epollfd, events, MAX_EVENTS, 24);
        if (nfds == -1) {
            LOG_ERR("Epoll_wait failed");
            perror("What"); return EXIT_FAILURE;
        }
        for (int i = 0; i < nfds; ++i) {
            while (true) {
                rc = recvfrom(sfd, e_coord, sizeof(int) * 2, 0, (sockaddr*)&s_addr, &sl);
                if (rc == 0) {
                    break;
                } if (rc != sizeof(int) * 2) {
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        // No more data available for now
                        break;
                    } else {
                        perror("Failed to receive");
                        break;
                    }
                } else {
                    if (s_addr.sin_addr.s_addr != this_addr) {
                        int x = e_coord[0];
                        int y = e_coord[1];
                        LOG_DBG("Enemy moved to: {}, {}", x, y);
                        enemy.place(x, y);
                    }
                }
            }
        }
        enemy.render(renderer);
        SDL_RenderPresent(renderer);
    }
    return EXIT_SUCCESS;
}

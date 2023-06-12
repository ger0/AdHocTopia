#include "types.hpp"
#include "networking.hpp"

#include <unordered_map>

#include <cstdlib>
#include <cstring>

#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <linux/wireless.h>

namespace networking {


std::unordered_map<byte, sockaddr_in> player_addrs;

bool setup_wlan(int sock);
bool setup_interface(int sock);

// epoll
static constexpr uint MAX_EVENTS = 8;
static epoll_event ev, events[MAX_EVENTS];

static int sfd = -1;
static socklen_t sl = 0;

static int epollfd = -1;

static sockaddr_in broadcast_addr;
static sockaddr_in this_addr;

static NetConfig config;

void broadcast(Packet &pkt) {
    Packet ns_pkt = {
        .opcode     = pkt.opcode,
        .player_num = pkt.player_num,
        .seq        = htonl(pkt.seq),
        .payload    = pkt.payload
    };

    uint payload_size = sizeof(ns_pkt.payload) / sizeof(uint);
    uint *buff = (uint*)&ns_pkt.payload;
    for (uint i = 0; i < payload_size; ++i) {
        buff[i] = htonl(buff[i]);
    }

    int rv = sendto(sfd, (void*)&ns_pkt, sizeof(ns_pkt), 0, (sockaddr*)&broadcast_addr, sl);
    if (rv <= 0) {
        LOG_ERR("Did not send the entire packet: {}", rv);
        perror("what");
    }
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

bool create_epoll() {
    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        LOG_ERR("Epoll creation failed");
        perror("What"); return EXIT_FAILURE;
    }

    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sfd, &ev) == -1) {
        LOG_ERR("Epoll_ctl failed");
        perror("What"); 
        return false;
    }

    // set the sfd to be nonblocking for epoll
    int flags = fcntl(sfd, F_GETFL, 0);
    if (fcntl(sfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERR("Nonblocking socket error");
        perror("What");
        return false;
    }
    return true;
}

bool setup(NetConfig &cfg) {
    config = cfg;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERR("Failed to create socket");
        return false;
    }
    // close socket opon returning
    defer {close(sock);};
    if (!setup_wlan(sock))      return false;
    if (!setup_interface(sock)) return false;
    LOG_DBG("Ad hoc network created successfully!");

    broadcast_addr = {
        .sin_family     = AF_INET,
        .sin_port       = htons(config.port),
        .sin_addr       = {inet_addr(config.bd_addr)}
    };
    this_addr = {
        .sin_family     = AF_INET,
        .sin_port       = htons(config.port),
        .sin_addr       = {in_addr_t{INADDR_ANY}},
    };

    sl = sizeof(this_addr);
    sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sfd < 0) {
        LOG_ERR("Socket error!");
        perror("What");
        return false;
    }
    if (!enable_sock_broadcast(sfd)) {
        return false;
    }
    if(!bind_addr(sfd, config.device, this_addr)) {
        perror("What"); 
        return false;
    };
    if (!create_epoll()) {
        return false;
    }
    return true;
}

void destroy() {
    if (sfd >= 0)        close(sfd);
    if (epollfd >= 0)    close(epollfd);
}

// setup adhoc mode and essid
bool setup_wlan(int sock) {
    const auto& cfg = config;
    struct iwreq iwr; // wireless  settings
    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, cfg.device, IFNAMSIZ);

    memset(&iwr.u, 0, sizeof(iwr.u));
    iwr.u.mode |= IW_MODE_ADHOC;
    if (ioctl(sock, SIOCSIWMODE, &iwr)) {
        LOG_ERR("Failed to set the wireless interface to ad-hoc mode");
        return false;
    }

    iwr.u.essid.pointer = (caddr_t)cfg.essid;
    iwr.u.essid.length = strlen(cfg.essid);
    iwr.u.essid.flags = 1;
    if (ioctl(sock, SIOCSIWESSID, &iwr)) {
        LOG_ERR("Failed to set the ESSID");
        return false;
    }
    return true;
}
// setup ip_address, netmask and a few flags
bool setup_interface(int sock) {
    const auto& cfg = config;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, cfg.device, IFNAMSIZ);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr)) {
        LOG_ERR("Failed to retrieve interface flags");
        return false;
    }
    // ip address 
    sockaddr_in sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = 0;
    if (inet_pton(AF_INET, cfg.ip_addr, &(sock_addr.sin_addr)) <= 0) {
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
    if (inet_pton(AF_INET, cfg.net_msk, &(sock_addr.sin_addr)) <= 0) {
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

Packet ntohpkt(Packet &pkt) {
    Packet h_pkt = {
        .opcode     = pkt.opcode,
        .player_num = pkt.player_num,
        .seq        = ntohl(pkt.seq),
        .payload    = pkt.payload
    };

    uint payload_size = sizeof(h_pkt.payload) / sizeof(uint);
    uint *buff = (uint*)&h_pkt.payload;
    for (uint i = 0; i < payload_size; ++i) {
        buff[i] = ntohl(buff[i]);
    }
    return h_pkt;
};

void ack_to_player(byte player_num) {
    Packet pkt;
    pkt.opcode = Opcode::Ack;
    const auto& s_addr = player_addrs[player_num];
    int rv = sendto(sfd, (void*)&pkt, sizeof(pkt), 0, (sockaddr*)&s_addr, sl);
    if (rv != sizeof(pkt)) {
        LOG_ERR("Not enough bytes sent!!! {}", rv);
    }
}

std::vector<Packet> poll() {
    std::vector<Packet> packets;
    Packet pkt;
    int nfds = epoll_wait(epollfd, events, MAX_EVENTS, 24);
    if (nfds == -1) {
        LOG_ERR("Epoll_wait failed");
        perror("What"); 
        pkt.opcode = Opcode::Malformed;
        return packets;
    }
    sockaddr_in s_addr;
    constexpr uint SIZE_PKT = sizeof(pkt);
    for (int i = 0; i < nfds; ++i) {
        while (true) {
            int rc = recvfrom(sfd, &pkt, SIZE_PKT, 0, (sockaddr*)&s_addr, &sl);
            if (rc == 0) {
                break;
            } else if (rc != SIZE_PKT) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    // No more data available for now
                    break;
                } else {
                    perror("Failed to receive");
                    break;
                }
            } // else receive the packet
            if (s_addr.sin_addr.s_addr != this_addr.sin_addr.s_addr) {
                pkt = ntohpkt(pkt);
                packets.push_back(pkt);
                if (!player_addrs.contains(pkt.player_num)) {
                    player_addrs.emplace(pkt.player_num, s_addr);
                }
            }
        }
    }
    return packets;
}
}

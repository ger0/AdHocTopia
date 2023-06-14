#include "types.hpp"
#include "networking.hpp"

#include <unistd.h>
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

bool setup_wlan(int sock);
bool setup_interface(int sock);

constexpr uint SIZE_PKT = sizeof(Packet);
constexpr uint MAX_BUFF_SIZE = 1024;

/* player_num           -> player_sockaddr_in       */
static std::unordered_map<byte, sockaddr_in>   player_addrs;

/* sockaddr.in_addr_t   -> player_tcp_sfd           *
 * [for bitmap transmission]                        */
static std::unordered_map<in_addr_t, int>      tcp_socks;

/* socket filedescriptor-> what part of the         *
 * bitmap has been sent so far                      */
static std::unordered_map<in_addr_t, size_t>   bitmap_bytes_sent; 


static constexpr uint MAX_EVENTS = 8;
static epoll_event ev, events[MAX_EVENTS];

static int udp_sfd = -1;

static int      tcp_sfd = -1;
static bool     is_tcp_listening    = false;
static bool     is_tcp_reading      = false;

/* used to point to bytes in the TCP stream buffer  */
static size_t   tcp_buffer_pointer = 0;
static size_t   tcp_buffer_size;

/* buffer assigned to receive bytes of a map th.TCP */
static std::vector<byte> tcp_buffer;

static socklen_t sl = 0;

static int epollfd = -1;

static sockaddr_in  broadcast_addr;
static sockaddr_in  this_addr;
static sockaddr_in  tcp_this_addr;
static in_addr_t    local_addr;

static NetConfig config;
// network to hardware
Packet ntohpkt(Packet &pkt) {
    Packet h_pkt = {
        .opcode             = pkt.opcode,
        .player_num         = pkt.player_num,
        .dest_num           = pkt.dest_num,
        .seq                = ntohl(pkt.seq),
        .payload            = pkt.payload
    };

    uint payload_size = sizeof(h_pkt.payload) / sizeof(uint);
    uint *buff = (uint*)&h_pkt.payload;
    for (uint i = 0; i < payload_size; ++i) {
        buff[i] = ntohl(buff[i]);
    }
    return h_pkt;
};
// hardware to network
Packet htonpkt(Packet &pkt) {
    Packet n_pkt = {
        .opcode             = pkt.opcode,
        .player_num         = pkt.player_num,
        .dest_num           = pkt.dest_num,
        .seq                = htonl(pkt.seq),
        .payload            = pkt.payload
    };

    uint payload_size = sizeof(n_pkt.payload) / sizeof(uint);
    uint *buff = (uint*)&n_pkt.payload;
    for (uint i = 0; i < payload_size; ++i) {
        buff[i] = htonl(buff[i]);
    }
    return n_pkt;
};

// TCP READING
bool connect_to_player(byte player_num, uint byte_count) {
    if (!player_addrs.contains(player_num)) {
        LOG_ERR("FATAL: CANNOT CONNECT TO NONEXISTING PLAYER!!!");
        return false;
    }
    // retr addr from recvd UDP packets 
    auto addr = player_addrs[player_num];
    // the rest of the addr is identical
    addr.sin_port = htons(config.port - 1);

    ev.events = EPOLLIN;
    ev.data.fd = tcp_sfd;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, tcp_sfd, &ev) == -1) {
        LOG_ERR("Failed to modify TCP socket in epoll.");
        perror("What");
        return false;
    }

     if (connect(tcp_sfd, (sockaddr *)&addr, sizeof(addr))) {
        if (errno == EINPROGRESS) {
            LOG_DBG("Connecting to TCP sender...");
        } else {
            LOG_ERR("Failed to connect to TCP sender.");
            perror("What");
            return false;
        }
    }
    is_tcp_reading = true;
    tcp_buffer_size = byte_count;
    tcp_buffer = std::vector<byte>(byte_count);
    return true; 
}

void broadcast(Packet &pkt) {
    Packet n_pkt = htonpkt(pkt);
    int rv = sendto(udp_sfd, (void*)&n_pkt, sizeof(n_pkt), 0, (sockaddr*)&broadcast_addr, sl);
    if (rv <= 0) {
        LOG_ERR("Did not send the entire packet: {}", rv);
        perror("what");
    }
}

bool bind_addr(c_str device) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

    // bind a specific device to the socket ( might not be used later on )
    if (setsockopt(udp_sfd, SOL_SOCKET, SO_BINDTODEVICE,
                   (void *)&ifr, sizeof(ifr)) < 0) {
        LOG_ERR("Failed to bind device to the UDP socket");
        return false;
    }
    if (setsockopt(tcp_sfd, SOL_SOCKET, SO_BINDTODEVICE,
                   (void *)&ifr, sizeof(ifr)) < 0) {
        LOG_ERR("Failed to bind device to the TCP socket");
        return false;
    }

    int reuseAddr = 1;
    if (setsockopt(udp_sfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
        LOG_ERR("Failed to set SO_REUSEADDR for UDP sock");
        return false;
    }
    if (setsockopt(tcp_sfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
        LOG_ERR("Failed to set SO_REUSEADDR for TCP sock");
        return false;
    }

    // UDP binding
    if (bind(udp_sfd, (sockaddr*)&this_addr, sizeof(this_addr)) == -1) {
        LOG_ERR("Error during socket binding");
		return false;
    };
    // TCP binding
    if (bind(tcp_sfd, (sockaddr*)&tcp_this_addr, sizeof(tcp_this_addr)) == -1) {
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
    
    // set the sfd to be nonblocking for epoll
    // UDP
    int flags = fcntl(udp_sfd, F_GETFL, 0);
    if (fcntl(udp_sfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERR("Nonblocking socket error for UDP sock");
        perror("What");
        return false;
    }
    // TCP
    flags = fcntl(tcp_sfd, F_GETFL, 0);
    if (fcntl(tcp_sfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERR("Nonblocking socket error for TCP sock");
        perror("What");
        return false;
    }    
    /* if (listen(tcp_sfd, (int)sizeof(Packet::player_num) - 2) == -1) {
        LOG_ERR("Failed to listen on TCP sock");
        perror("What");
        return false;
    } */

    // ADDING UDP SOCKET TO EPOLL
    ev.events = EPOLLIN;
    ev.data.fd = udp_sfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, udp_sfd, &ev) == -1) {
        LOG_ERR("Epoll_ctl failed when adding UDP sock");
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

    // UDP broadcast address
    broadcast_addr = {
        .sin_family     = AF_INET,
        .sin_port       = htons(config.port),
        .sin_addr       = {inet_addr(config.bd_addr)}
    };
    // UDP address to receive requests from
    this_addr = {
        .sin_family     = AF_INET,
        .sin_port       = htons(config.port),
        .sin_addr       = {in_addr_t{INADDR_ANY}},
    };
    // TCP address to accept connections from PORT = PORT - 1
    tcp_this_addr = {
        .sin_family     = AF_INET,
        .sin_port       = htons(config.port - 1),
        .sin_addr       = {in_addr_t{INADDR_ANY}},
    };

    sl = sizeof(this_addr);
    udp_sfd = socket(AF_INET, SOCK_DGRAM,   IPPROTO_UDP);
    tcp_sfd = socket(AF_INET, SOCK_STREAM,  IPPROTO_TCP);
    if (udp_sfd < 0) {
        LOG_ERR("Socket error!");
        perror("What");
        return false;
    }
    if (!enable_sock_broadcast(udp_sfd)) {
        return false;
    }
    if(!bind_addr(config.device)) {
        perror("What"); 
        return false;
    };
    if (!create_epoll()) {
        return false;
    }
    return true;
}

void destroy() {
    LOG_DBG("Cleaning up networking resources...");
    if (udp_sfd >= 0)    close(udp_sfd);
    if (tcp_sfd >= 0)    close(tcp_sfd);
    if (epollfd >= 0)    close(epollfd);
    for (auto& [_, sock]: tcp_socks) {
        close(sock);
    }
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
    local_addr = sock_addr.sin_addr.s_addr;

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

void ack_to_player(byte player_num, byte from_num) {
    Packet pkt;
    pkt.opcode = Opcode::Ack;
    pkt.player_num  = from_num;
    pkt.dest_num    = player_num;
    pkt = htonpkt(pkt);

    const auto& s_addr = player_addrs[player_num];
    int rv = sendto(udp_sfd, (void*)&pkt, sizeof(pkt), 0, (sockaddr*)&s_addr, sl);
    if (rv != sizeof(pkt)) {
        LOG_ERR("Not enough bytes sent!!! {}", rv);
    }
}

void recv_udp_packets(std::vector<Packet> &packets) {
    sockaddr_in s_addr;
    Packet pkt;

    while (true) {
        int rc = recvfrom(udp_sfd, &pkt, SIZE_PKT, 0, (sockaddr*)&s_addr, &sl);
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
        } 
        if (s_addr.sin_addr.s_addr == local_addr) break;

        // else receive the packet if its not from this address
        pkt = ntohpkt(pkt);
        packets.push_back(pkt);
        if (!player_addrs.contains(pkt.player_num)) {
            LOG_DBG("Local addr: {}", local_addr);
            LOG_DBG("Added player's: {} address: {} to the list of known players", 
                    pkt.player_num, s_addr.sin_addr.s_addr);

            // add the player_num to list
            player_addrs.emplace(pkt.player_num, s_addr);
        }
    }
}

bool start_tcp_listening() {
    if (is_tcp_listening) {
        LOG_ERR("TCP IS ALREADY LISTENING");
        return false;
    } 

    // starts listening
    if (listen(tcp_sfd, (int)sizeof(Packet::player_num) - 2) == -1) {
        LOG_ERR("Failed to start listening on a TCP sock");
        perror("What");
        return false;
        // CREATE A MALFORMED PACKET TO INFORM THE PROGRAM ABOUT THE FAILURE!!!!!!
    }

    // ADDING TCP SOCKET TO EPOLL
    ev.events = EPOLLIN;
    ev.data.fd = tcp_sfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, tcp_sfd, &ev) == -1) {
        LOG_ERR("Epoll_ctl failed when adding TCP sock");
        perror("What"); 
        return false;
    }
    LOG_DBG("Listening for connections on TCP sock");
    is_tcp_listening = true;
    return true;
}

void accept_tcp_conns(std::vector<Packet> &packets) {
    sockaddr_in cs_addr;
    socklen_t c_slen = sizeof(cs_addr);
    int c_sfd = accept(tcp_sfd, (sockaddr *)&cs_addr, &c_slen);
    if (c_sfd == -1) {
        LOG_ERR("Failed to accept TCP client connection");
        perror("What");
        return;
    }

    // Add client socket to epoll
    epoll_event event;
    event.events = EPOLLOUT;
    event.data.fd = c_sfd;
    // double-check later on
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, tcp_sfd, &event) == -1) {
        LOG_ERR("Failed to add client TCP socket to epoll.");
        perror("What");
        return;
    }
    LOG_DBG("New TCP client connected.");
    in_addr_t caddr;
    tcp_socks[caddr] = c_sfd;
    bitmap_bytes_sent[caddr] = 0;
}

void write_tcp_buffer(int fd) {
    // how many bytes were written so far?
    auto &read_count = bitmap_bytes_sent[fd];
    int read_reamin = tcp_buffer_size - read_count;
}

// uses a newly generated fd by epoll
void read_tcp_buffer(int fd) {
    // tcp_buffer_pointer
    int curr_read;
    size_t remaining = tcp_buffer_size - tcp_buffer_pointer;
    void *buff_point = tcp_buffer.data() + tcp_buffer_pointer;
    while (tcp_buffer_pointer < tcp_buffer_size) {
        curr_read = recv(fd, buff_point, remaining, 0);
        if (curr_read > 0) {
            tcp_buffer_pointer += curr_read;
            remaining -= curr_read;
            LOG_DBG("Received {} bytes, remaining {} on TCP stream", curr_read, remaining);

            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                LOG_DBG("Waiting for more on TCP stream...");
                break;
            }
        }
        else if (curr_read == 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                LOG_DBG("Waiting for more on TCP stream...");
                break;
            } else {
                LOG_ERR("Error in Reading TCP");
                perror("What");
            }
            return;
            // send failure msg to main prog and handle th 
        } else if (curr_read == -1) {
            LOG_ERR("Error in the TCP Connection...");
            perror("What");
            return;
        }
    }
}

std::vector<Packet> poll() {
    std::vector<Packet> packets;
    int num_events = epoll_wait(epollfd, events, MAX_EVENTS, 24);
    if (num_events == -1) {
        if (EINTR) {
            LOG_DBG("Epoll skipping, program interrupted");
        } else {
            LOG_ERR("PANIC: Epoll wait failed");
            perror("What"); 
            exit(EXIT_FAILURE);
        }
        return packets;
    }
    for (int i = 0; i < num_events; ++i) {
        int fd = events[i].data.fd;
        if (fd == udp_sfd) {
            recv_udp_packets(packets);
        } else if (fd == tcp_sfd && is_tcp_listening) {
            accept_tcp_conns(packets);
        } else if (is_tcp_reading) read_tcp_buffer(fd); 
    }
    return packets;
}
}

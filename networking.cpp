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
bool set_iface_down(int sock);

constexpr uint SIZE_PKT = sizeof(Packet);

struct PlayerEntry {
    byte        player_num;

    int         tcp_sock            = -1;
    uint        tcp_bytes_sent      = 0;
    uint        tcp_buffer_size     = 0;
    sockaddr_in saddr_in; 
    size_t      last_seq            = 0;
};

/* player_num -> player_info entry                  */
static std::unordered_map<byte, PlayerEntry>    player_entries;

static constexpr uint MAX_EVENTS = 8;
static epoll_event ev, events[MAX_EVENTS];

static uint     THIS_SEQ_NUM = 0;

static int      send_udp_sfd = -1;
static int      recv_udp_sfd = -1;

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
static sockaddr_in  tcp_this_addr;
static in_addr_t    local_addr;

static NetConfig config;
// network to hardware
Packet ntohpkt(Packet &pkt) {
    Packet h_pkt = {
        .opcode             = pkt.opcode,
        .player_num         = pkt.player_num,
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
    if (!player_entries.contains(player_num)) {
        LOG_ERR("FATAL: CANNOT CONNECT TO NONEXISTING PLAYER!!!");
        return false;
    }
    if (is_tcp_reading) {
        LOG_DBG("Cannot connect twice to the same TCP sender");
        return false;
    }
    // retr addr from recvd UDP packets 
    auto addr = player_entries.at(player_num).saddr_in;
    // the rest of the addr is identical
    addr.sin_port = htons(config.port - 1);

    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = tcp_sfd;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, tcp_sfd, &ev) == -1) {
        LOG_ERR("Failed to add TCP socket to epoll.");
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
    LOG_DBG("Connected, bytes to read from TCP sender: {}.", byte_count);
    tcp_buffer = std::vector<byte>(byte_count);
    return true; 
}

void broadcast(Packet &pkt) {
    ++THIS_SEQ_NUM;
    pkt.seq = THIS_SEQ_NUM;
    Packet n_pkt = htonpkt(pkt);
    socklen_t slen = sizeof(broadcast_addr);
    int rv = sendto(send_udp_sfd, (void*)&n_pkt, sizeof(n_pkt), 0, (sockaddr*)&broadcast_addr, slen);
    if (rv < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        LOG_ERR("Failed to send: {}", rv);
        perror("what");
    }
}

bool bind_addr(c_str device) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));

    // bind a specific device to the socket ( might not be used later on )
    if (setsockopt(send_udp_sfd, SOL_SOCKET, SO_BINDTODEVICE,
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
    if (setsockopt(send_udp_sfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
        LOG_ERR("Failed to set SO_REUSEADDR for UDP sock");
        return false;
    }
    if (setsockopt(recv_udp_sfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
        LOG_ERR("Failed to set SO_REUSEADDR for Recv UDP sock");
        return false;
    }
    if (setsockopt(tcp_sfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
        LOG_ERR("Failed to set SO_REUSEADDR for TCP sock");
        return false;
    }
    // RECV UDP binding
    if (bind(recv_udp_sfd, (sockaddr*)&broadcast_addr, sizeof(broadcast_addr)) == -1) {
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
    int flags = fcntl(send_udp_sfd, F_GETFL, 0);
    if (fcntl(send_udp_sfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERR("Nonblocking socket error for UDP sock");
        perror("What");
        return false;
    }
    // RECV UDP
    flags = fcntl(recv_udp_sfd, F_GETFL, 0);
    if (fcntl(recv_udp_sfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERR("Nonblocking socket error for RECV UDP sock");
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

    // ADDING UDP SOCKET TO EPOLL
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = recv_udp_sfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, recv_udp_sfd, &ev) == -1) {
        LOG_ERR("Epoll_ctl failed when adding Recv UDP sock");
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
    if (!set_iface_down(sock))  return false;
    if (!setup_wlan(sock))      return false;
    if (!setup_interface(sock)) return false;
    LOG_DBG("Ad hoc network created successfully!");

    // UDP broadcast address
    broadcast_addr = {
        .sin_family     = AF_INET,
        .sin_port       = htons(config.port),
        .sin_addr       = {inet_addr(config.bd_addr)}
    };
    // TCP address to accept connections from PORT = PORT - 1
    tcp_this_addr = {
        .sin_family     = AF_INET,
        .sin_port       = htons(config.port - 1),
        .sin_addr       = {in_addr_t{INADDR_ANY}},
    };

    sl = sizeof(sockaddr_in);
    send_udp_sfd         = socket(AF_INET, SOCK_DGRAM,   IPPROTO_UDP);
    if (send_udp_sfd < 0) {
        LOG_ERR("Socket udp error!");
        perror("What");
        return false;
    }
    recv_udp_sfd    = socket(AF_INET, SOCK_DGRAM,   IPPROTO_UDP);
    if (recv_udp_sfd < 0) {
        LOG_ERR("Socket recv_udp error!");
        perror("What");
        return false;
    }
    tcp_sfd         = socket(AF_INET, SOCK_STREAM,  IPPROTO_TCP);
    if (tcp_sfd < 0) {
        LOG_ERR("Socket tcp error!");
        perror("What");
        return false;
    }
    if (!enable_sock_broadcast(send_udp_sfd)) {
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
    if (send_udp_sfd >= 0)  close(send_udp_sfd);
    if (recv_udp_sfd >= 0)  close(recv_udp_sfd);
    if (tcp_sfd >= 0)       close(tcp_sfd);
    if (epollfd >= 0)       close(epollfd);
    for (auto& [_, info]: player_entries) {
        close(info.tcp_sock);
    }
}

// setup adhoc mode and essid
bool setup_wlan(int sock) {
    const auto& cfg = config;
    struct iwreq iwr; // wireless  settings

    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, cfg.device, IFNAMSIZ);

    memset(&iwr.u, 0, sizeof(iwr.u));
    iwr.u.mode = IW_MODE_ADHOC;
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
bool set_iface_down(int sock) {
    const auto& cfg = config;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, cfg.device, IFNAMSIZ);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr)) {
        LOG_ERR("Failed to retrieve interface flags");
        return false;
    }
    // set the interface DOWN
    ifr.ifr_flags = ~IFF_UP;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr)) {
        LOG_ERR("Failed to set interface DOWN");
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

bool set_tcp_buffer(byte* byte_ptr, size_t size) {
    tcp_buffer_size = size;
    tcp_buffer = std::vector<byte>(size);
    memcpy(tcp_buffer.data(), byte_ptr, size);
    return true;
}

void recv_udp_packets(int fd, std::vector<Packet> &packets) {
    sockaddr_in s_addr;
    Packet pkt;
    socklen_t sockl = sizeof(s_addr);

    while (true) {
        int rc = recvfrom(fd, &pkt, SIZE_PKT, 0, (sockaddr*)&s_addr, &sockl);
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
        if (!player_entries.contains(pkt.player_num)) {
            LOG_DBG("Local addr: {}", local_addr);
            LOG_DBG("Added player's: {} address: {} to the list of known players", 
                    pkt.player_num, s_addr.sin_addr.s_addr);

            // add the player_num to list
            player_entries.emplace(pkt.player_num, PlayerEntry{.saddr_in = s_addr});
        }

        // send the packet to the game if the most recent one received
        auto &last_seq = player_entries.at(pkt.player_num).last_seq;
        if (pkt.seq >= last_seq) {
            packets.push_back(pkt);
            last_seq = pkt.seq;
        }
    }
}

bool listen_to_players() {
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
    ev.events = EPOLLIN | EPOLLET;
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

void accept_tcp_conns(int fd) {
    sockaddr_in cs_addr;
    socklen_t c_slen = sizeof(cs_addr);
    int c_sock = accept(fd, (sockaddr *)&cs_addr, &c_slen);
    if (c_sock == -1) {
        LOG_ERR("Failed to accept TCP client connection");
        perror("What");
        return;
    }

    // Add client socket to epoll
    epoll_event event;
    event.events = EPOLLOUT | EPOLLET;
    event.data.fd = c_sock;
    // double-check later on
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, c_sock, &event) == -1) {
        LOG_ERR("Failed to add client TCP socket to epoll.");
        perror("What");
        return;
    }
    LOG_DBG("New TCP client connected. fd: [{}]", c_sock);

    // set values in the player_entries
    // find by caddr, because the address is the only thing we know so far
    for (auto& [player_num, info]: player_entries) {
        if (info.saddr_in.sin_addr.s_addr == cs_addr.sin_addr.s_addr) {
            info.tcp_sock       = c_sock;
            info.tcp_bytes_sent = 0;
        }
        break;
    }
}

bool write_tcp_buffer(PlayerEntry &info, std::vector<Packet>& packets) {
    LOG_DBG("Trying to send on TCP stream... fd: {}", info.tcp_sock);
    // how many bytes were written so far?
    auto &write_point = info.tcp_bytes_sent;
    int write_remain = tcp_buffer_size - write_point;
    void *buff = tcp_buffer.data() + write_point;
    LOG_DBG("remaining bytes to send {}", write_remain);

    while (write_remain > 0) {
        int rc = send(info.tcp_sock, buff, write_remain, 0);
        if (rc > 0) {
            write_point += rc;
            write_remain -= rc;
            LOG_DBG("Sent {} bytes, remaining {} on TCP stream", rc, write_remain);

            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                LOG_DBG("Waiting for sending on TCP stream...");
                break;
            }
        } else if (rc == 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                LOG_DBG("Waiting for sending on TCP stream...");
                break;
            } else {
                LOG_ERR("Error in Sending TCP");
                perror("What");
            }
            return false;
            // send failure msg to main prog and handle th 
        } else if (rc == -1) {
            LOG_ERR("Error in the TCP Connection...");
            perror("What");
            return false;
        }
    }
    if (write_remain == 0) {
        packets.push_back(Packet {
            .opcode = Opcode::Done_TCP,
            .player_num = info.player_num,
            .seq = 0,
        });
        return true;
    }
    return false;
}

// uses a newly generated fd by epoll
void read_tcp_buffer(int fd, std::vector<Packet>& packets) {
    // tcp_buffer_pointer
    int curr_read;
    size_t remaining = tcp_buffer_size - tcp_buffer_pointer;
    void *buff_point = tcp_buffer.data() + tcp_buffer_pointer;
    LOG_DBG("remaining {} on TCP stream", remaining);
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
                LOG_DBG("READ 0 ON TCP STREAM");
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
    if (remaining == 0) {
        // send to itself
        packets.push_back(Packet {
            .opcode = Opcode::Done_TCP,
            .player_num = config.pr_numb,
            .seq = 0,
        });
    }
}

std::vector<byte> return_tcp_buffer() {
    return tcp_buffer;
}

int player_num_to_tcp_sock(int tcp_sock) {
    for (auto& [num, info]: player_entries) {
        if (info.tcp_sock == tcp_sock) return num;
    }
    return 0;
}

std::vector<Packet> poll() {
    std::vector<Packet> packets;
    int num_events = epoll_wait(epollfd, events, MAX_EVENTS, 24);
    if (num_events == -1) {
        if (errno == EINTR) {
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
        if (fd == recv_udp_sfd) {
            recv_udp_packets(fd, packets);
        } 
        else if (fd == tcp_sfd && is_tcp_listening) {
            accept_tcp_conns(fd);
        } 
        else if (fd == tcp_sfd && is_tcp_reading) {
            read_tcp_buffer(fd, packets);
        } 
        else if (int p_num = player_num_to_tcp_sock(fd); p_num != 0) {
            write_tcp_buffer(player_entries[p_num], packets);
        }
    }
    return packets;
}
}

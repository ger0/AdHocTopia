#ifndef ADHTP_NETWORK_HDR
#define ADHTP_NETWORK_HDR

#include "types.hpp"
#include <netinet/in.h>
#include <vector>

namespace networking {

enum Opcode: byte {
    Hello       = 0,
    Ask_map     = 1,
    Send_map    = 2,
    Coord       = 3,
    Malformed   = 99
};

union Data {
    i32 coord[2];
};

struct Packet {
    Opcode  opcode;
    byte    player_num;

    byte    __padding__[2] = {0,0};

    uint    seq; 
    Data    payload;
};

struct NetConfig {
    c_str   device;
    c_str   essid;

    c_str   ip_addr;
    c_str   net_msk;
    c_str   bd_addr;

    uint    port;
};

bool setup(NetConfig &config);
void destroy();
void broadcast(Packet &pkt);
std::vector<Packet> poll();
};
#endif //ADHTP_NETWORK_HDR

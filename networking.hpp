#ifndef ADHTP_NETWORK_HDR
#define ADHTP_NETWORK_HDR

#include "types.hpp"
#include <netinet/in.h>
#include <vector>

namespace networking {

enum Opcode: byte {
    Hello       = 0xF0 ,
    Ack         = 0x01,
    Ask_map     = 0x02,
    Send_map    = 0x03,
    Coord       = 0x04,
    Malformed   = 0xFF 
};

union Data {
    struct {
        i32     coord[2];
        float   d_vel[2];
    } move;
    struct {
        uint    map_buff_size; // size of buffored data [for TCP]
    };
};

struct Packet {
    Opcode  opcode;
    byte    player_num;

    byte    dest_num; // ACK specific;; 0 - all
    byte    __padding__[1] = {0};

    uint    seq; 
    Data    payload;
};

struct NetConfig {
    c_str   device;
    c_str   essid;

    c_str   ip_addr;
    c_str   net_msk;
    c_str   bd_addr;
    byte    pr_numb;

    uint    port;
};

bool setup(NetConfig &config);
void destroy();
void broadcast(Packet &pkt);
void ack_to_player(byte num_to, byte num_from);
bool set_tcp_buffer(byte* byte_ptr, size_t size);
// connects to player and sets a buffer to requested size
bool connect_to_player(byte player_num, uint byte_count);
bool start_tcp_listening();
std::vector<Packet> poll();

};
#endif //ADHTP_NETWORK_HDR

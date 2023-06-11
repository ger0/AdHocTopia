#include <cstdlib>
#include <vector>
#include <array>
#include <fmt/ranges.h>
#include <fmt/core.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/wireless.h>

using uint  = unsigned int;
using byte  = unsigned char;
using c_str = const char*;

constexpr uint PORT = 2113;
constexpr uint MAX_BUFFER_SIZE = 1024;

struct Map {
    static constexpr uint WIDTH     = 800;
    static constexpr uint HEIGHT    = 600;
} map;

#define LOG(...) \
    fmt::print(stdout, "{}\n", fmt::format(__VA_ARGS__))
#define LOG_ERR(...) \
    fmt::print(stderr, "\033[1;31m{}\033[0m\n", fmt::format(__VA_ARGS__))

#ifdef DEBUG 
#define LOG_DBG(...) \
    fmt::print(stderr, "\033[1;33m{}\033[1;33m\n", fmt::format(__VA_ARGS__))
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

bool create_adhoc_network(c_str device, c_str essid, c_str ip_addr) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERR("Failed to create socket");
        return false;
    }
    // close socket opon returning
    defer {close(sock);};
    
    struct ifreq ifr; // interface settings
    struct iwreq iwr; // wireless  settings
    memset(&ifr, 0, sizeof(ifr));
    memset(&iwr, 0, sizeof(iwr));
    strncpy(ifr.ifr_name, device, IFNAMSIZ);
    strncpy(iwr.ifr_ifrn.ifrn_name, device, IFNAMSIZ);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr)) {
        LOG_ERR("Failed to retrieve interface flags");
        return false;
    }

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

    // set up the ip address 
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
    if (ioctl(sock, SIOCGIFFLAGS, &ifr)) {
        LOG_ERR("Failed to retrieve interface flags");
		return false;
    }

    // set the interface UP
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr)) {
        LOG_ERR("Failed to set interface flags");
		return false;
    }
    LOG_DBG("Ad hoc network created successfully!");
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        LOG("Usage: {} <device> <essid> <ip_address>", argv[0]);
        return EXIT_FAILURE;
    }
    c_str device    = argv[1];
    c_str essid     = argv[2];
    c_str ip_addr   = argv[3];

    if (!create_adhoc_network(device, essid, ip_addr)) {
        perror("What");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

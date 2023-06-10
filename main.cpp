#include <vector>
#include <array>
#include <fmt/ranges.h>
#include <fmt/core.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

using uint = unsigned int;
using byte = unsigned char;

constexpr uint PORT = 2113;
constexpr uint MAX_BUFFER_SIZE = 1024;


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

bool create_adhoc_network(const char* device, const char* ip_addr) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERR("Failed to create socket");
        return false;
    }
    auto fail = [&sock]() {close(sock); return false;};

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    strncpy(ifr.ifr_name, device, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFFLAGS, &ifr)) {
        LOG_ERR("Failed to retrieve interface flags");
		return fail();
    }

    // set up the ip address 
    sockaddr_in sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = 0;
    if (inet_pton(AF_INET, ip_addr, &(sock_addr.sin_addr)) <= 0) {
        LOG_ERR("Invalid IP address");
		return fail();
    }
    memcpy(&ifr.ifr_addr, &sock_addr, sizeof(sock_addr));
    if (ioctl(sock, SIOCSIFADDR, &ifr)) {
        LOG_ERR("Failed to set interface IP address");
		return fail();
    }
    if (ioctl(sock, SIOCGIFFLAGS, &ifr)) {
        LOG_ERR("Failed to retrieve interface flags");
		return fail();
    }

    // set the interface UP
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr)) {
        LOG_ERR("Failed to set interface flags");
		return fail();
    }
    LOG_DBG("Ad hoc network created successfully!");
    close(sock); 
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        LOG("Usage: {} <device> <ip_address>", argv[0]);
        return EXIT_FAILURE;
    }
    const char* device  = argv[1];
    const char* ip_addr = argv[2];

    if (!create_adhoc_network(device, ip_addr)) {
        perror("What");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

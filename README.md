# A platformer game for Ad-Hoc networks
A simple ad-hoc platformer prototype for GNU / Linux, 
made with the use of POSIX socket API and SDL2 graphics library.

## Building:
```sh
make
```

## Dependencies:
- GNU / Linux Operating System
- SDL2
- fmt

## Testing:
It is possible to emulate wlan interfaces with `mac80211_hwsim` kernel module.
Network namespaces need to be configured to emulate wlan interfaces properly.

# A platformer game for Ad-Hoc networks
An ad-hoc platformer made with the use of POSIX socket API and SDL2 graphics library.

## Building:
```sh
make
```

## Dependencies:
- SDL2
- fmt

## Testing:
It is possible to emulate wlan interfaces `with mac80211_hwsim` kernel module.
One will need to configure network namespaces to configure emulated interfaces properly.

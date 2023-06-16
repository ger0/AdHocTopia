
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
- C++20

The program should compile with C++17 with small modifications.

# Usage:
`./adhoctopia <device> <essid> <player id> <other player count>`

**Device** - the wireless interface used to create an Ad-Hoc network.
**ESSID** - the ESSID of the Ad-Hoc network, it can be any string of characters
**player id** - the id of the player within 1-254 range, the player with lowest number sends their map to others
**player count** - how many other players are there to wait for, 
for example 2 players in the game means the value of 1
Every player draws the map. 

### Keys:
- **S** sets the Starting point,
- **F** sets the Destination point
- **W** allows the user to draw a wall
- **SPACE** marks that the player is ready to start playing
- **Arrows** in the playing mode, self explanatory

Player with lower player id number will send the map to others.
The game starts once the map is loaded.

## Testing:
It is possible to emulate wireless interfaces with `mac80211_hwsim` kernel module.
Network namespaces need to be configured to emulate wlan properly.

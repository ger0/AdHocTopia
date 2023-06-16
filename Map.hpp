#ifndef ADHTP_MAP_HDR
#define ADHTP_MAP_HDR

#include <SDL2/SDL_events.h>
#include <array>
#include "math.hpp"
#include "types.hpp"
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_render.h>
#include <vector>

enum CellType: byte {
    EMPTY   = 0x00,
    WALL    = 0xff,
    START   = 0xf0,
    FINISH  = 0x0f,
};

struct Map {
    static constexpr int WIDTH  = 800;
    static constexpr int HEIGHT = 600;
    static constexpr int SIZE   = WIDTH * HEIGHT;
    std::array<byte, SIZE> data;
    
    Map();
    byte at_bnd(const int x, const int y) const;
    void handle_event(SDL_Event &event);
    Vector2D refl_vector(Vector2D const &vect, const float x, const float y) const;

    std::tuple<int, int> start_point;
    std::tuple<int, int> finish_point;

    bool start_initialised  = false;
    bool finish_initialised = false;
    
    void update(std::vector<byte> new_map);

    SDL_Texture *_texture   = nullptr;
	SDL_Renderer *_renderer = nullptr;
private:
    bool        _is_drawing = false;
    CellType    _brush_type = CellType::EMPTY;
};
#endif //ADHTP_MAP_HDR

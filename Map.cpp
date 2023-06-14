#include "Map.hpp"

inline byte &at(Map &self, const int x, const int y) {
    return self.data.at(x + y * self.WIDTH);
}

static constexpr int BRUSH_SIZE = 18;

SDL_Colour get_cell_colour(byte cell) {
    switch (cell) {
        case CellType::START:
            return {0,255,0,255};
        case CellType::FINISH:  
            return {255,0,0,255};
        case CellType::WALL:
            return {100,100,100,255};
        case CellType::EMPTY:
            return {0,0,0,255};
        default:
            return {255,0,255,255};
    }
}

Map::Map() {
    data.fill(0);
}

void _write_at(Map &self, const int x, const int y, byte value, const int size) {
    for (int iy = y; iy < y + size; iy++) {
        for (int ix = x; ix < x + size; ix++) {
            if (ix >= self.WIDTH || ix < 0 || iy >= self.HEIGHT || iy < 0) continue;
            at(self, ix, iy) = value;
        }
    }
}
void _draw_at(Map &self, const int x, const int y, const CellType value, const int size) {
    // Set the target texture
    SDL_SetRenderTarget(SDL_GetRenderer(SDL_GetWindowFromID(1)), self._texture);

    // Draw
    SDL_Rect rect = {x, y, size, size};
    SDL_Color col = get_cell_colour(value);
    SDL_SetRenderDrawColor(
        SDL_GetRenderer(SDL_GetWindowFromID(1)), col.r, col.g, col.g, col.a);
    SDL_RenderFillRect(
        SDL_GetRenderer(SDL_GetWindowFromID(1)), &rect);

    // Reset the target to the default rendering target (the window)
    SDL_SetRenderTarget(SDL_GetRenderer(SDL_GetWindowFromID(1)), NULL);
}

// calculates a reflection based on surface's gradient at point mx, my
Vector2D Map::refl_vector(Vector2D const &vect, const float mx, const float my) const {
    const int x = mx + 0.5f;
    const int y = my + 0.5f;
    float x0 = this->at_bnd(x-1, y-1) / 3.f
             + this->at_bnd(x-1, y  ) / 3.f
             + this->at_bnd(x-1, y+1) / 3.f;

    float x1 = this->at_bnd(x+1, y-1) / 3.f
             + this->at_bnd(x+1, y  ) / 3.f
             + this->at_bnd(x+1, y+1) / 3.f;

    float y0 = this->at_bnd(x-1, y+1) / 3.f
             + this->at_bnd(x  , y+1) / 3.f
             + this->at_bnd(x+1, y+1) / 3.f;

    float y1 = this->at_bnd(x-1, y-1) / 3.f
             + this->at_bnd(x  , y-1) / 3.f
             + this->at_bnd(x+1, y-1) / 3.f;

    // gradient
    Vector2D surf_grad = {x1 - x0, y1 - y0};
    surf_grad.normalize();
    Vector2D norm_vect = vect;
    norm_vect.normalize();
    
    return reflect(norm_vect, surf_grad);
}

void Map::update(std::vector<byte> buff) {
    memcpy(&this->data, buff.data(), buff.size());
    for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < WIDTH; ++x) {
        CellType value = (CellType)this->at_bnd(x, y);
        LOG_DBG("VAL: {} {}", this->at_bnd(x, y), CellType::WALL);
        _draw_at(*this, x, y, value, 1);
    }
    }
    LOG_DBG("Refreshed the MAP STATE!");
}
void Map::handle_event(SDL_Event &event) {
    int x, y;

    const auto& ksymbl  = event.key.keysym.sym;
    const auto& embttn  = event.button;

    switch (event.type) {
        case SDL_KEYDOWN:
            if (ksymbl == SDLK_s) {
                _brush_type = CellType::START;
            } else if (ksymbl == SDLK_f) {
                _brush_type = CellType::FINISH;
            } else if (ksymbl == SDLK_w) {
                _brush_type = CellType::WALL;
            } else {
                _brush_type = CellType::EMPTY;
            }
            return;
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (embttn.button != SDL_BUTTON_LEFT) break;
            _is_drawing = true;
            x = embttn.x;
            y = embttn.y;
            break;
        case SDL_MOUSEBUTTONUP:
            if (embttn.button != SDL_BUTTON_LEFT) break;
            _is_drawing = false;
            return;
            break;
        case SDL_MOUSEMOTION: 
            x = event.motion.x;
            y = event.motion.y;
            break;
    } 
    if (_is_drawing && _brush_type != EMPTY) {
        _draw_at(*this, x, y, _brush_type, BRUSH_SIZE);
        _write_at(*this, x, y, _brush_type, BRUSH_SIZE);
    }
}

byte Map::at_bnd(const int x, const int y) const {
    if (x <= 0 || x >= this->WIDTH - 1
        || y <= 0 || y >= this->HEIGHT - 1) {
        return CellType::WALL;
    }
    else return data.at(x + y * this->WIDTH);
}


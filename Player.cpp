#include "Player.hpp"

static constexpr float JUM_CAP  = 5.f;
static constexpr float VEL_CAP  = 2.f; //cap
static constexpr float ACCEL    = 0.3f;
static constexpr float DECCEL   = 0.85f;
static constexpr float GRAVITY  = 0.2f; 

auto is_on_ground(Map &map, int x, int y) -> bool {
    return (map.at_bnd(x, y) != WALL 
        && map.at_bnd(x, y + 1) == WALL);
};

auto is_colliding(Map &map, int x, int y) -> bool {
    return 
        (map.at_bnd(x + 1, y) == WALL &&
        map.at_bnd(x - 1, y) == WALL &&
        map.at_bnd(x, y - 1) == WALL &&
        map.at_bnd(x, y + 1) == WALL);
};

bool unstuck_walls(Player &self, Map &map) {
    for (int off = 0; off < self.SIZE.HEIGHT; ++off) {
        if (is_on_ground(map, self.pos.x, self.pos.y - off)) {
            self.pos.y = self.pos.y - off;
            return true;
        }
    }
    return false;
}

bool move_down(Player &self, Map &map) {
    for (int off = 0; off > -self.SIZE.HEIGHT; --off) {
        if (is_on_ground(map, self.pos.x, self.pos.y - off)) {
            self.pos.y = self.pos.y - off;
            return true;
        }
    }
    return false;
}


// [TODO] Refactor this nightmare
void move_flight(Player &self, Map &map) {
    auto &vel = self.vel;
    auto &pos = self.pos;
    vel.y += GRAVITY;

    int curr_x, curr_y;

    // MESSY BOUNDARY CHECKING
    Vector2D step_vel = vel;
    step_vel.normalize();

    // a partial dist
    float step_x = pos.x;
    float step_y = pos.y;

    // max distance to the last possible point
    Vector2D distance(
        (pos.x + vel.x) - pos.x, (pos.y + vel.y) - pos.y
    );
    float distance_len = distance.length();

    Vector2D distance_taken(0.f, 0.f);
    float distance_remain = distance_len - 0.f;

    do {
        curr_x = (int)(step_x + 0.5f);
        curr_y = (int)(step_y + 0.5f);

        if (is_colliding(map, step_x, step_y)) {
            break;
        }
        step_x += step_vel.x;
        step_y += step_vel.y;

        distance_taken.x = step_x - pos.x;
        distance_taken.y = step_y - pos.y;

        distance_remain = distance_len - distance_taken.length();
    } while (distance_remain > 0.f);
    pos.x = curr_x;
    pos.y = curr_y;
} 

void move_walking(Player &self, Map &map) {
    self.pos.x += self.vel.x + 0.5f; 
    self.vel.y = 0.f;

    while (self.vel.x != 0.f) {
        if (is_on_ground(map, self.pos.x, self.pos.y)) return;
        if (unstuck_walls(self, map)) return;
        if (move_down(self, map)) return;
        self.vel.x *= 0.5;
    }
}

void Player::set_new_data(int x, int y, float vel_x, float vel_y) {
    this->pos.x = x;
    this->pos.y = y;
    this->vel.x = vel_x;
    this->vel.y = vel_y;
}

void Player::update_position(Map &map) {
    // Handle movements:
    switch (direction) {
        case Left:
            vel.x = vel.x <= -VEL_CAP ? -VEL_CAP : vel.x - ACCEL;
            break;
        case Right:
            vel.x = vel.x >= VEL_CAP ? VEL_CAP : vel.x + ACCEL;
            break;
        case None:
            if (vel.x > -0.01f && vel.x < 0.01f) {
                vel.x = 0.f;
            } else {
                vel.x *= DECCEL;
            }
            break;
    };
    int old_x = pos.x;
    int old_y = pos.y;

    if (!needs_jump && is_on_ground(map, pos.x, pos.y)) {
        move_walking(*this, map);
    } 
    else if (!is_colliding(map, pos.x, pos.y)) {
        move_flight(*this, map);
    }
    needs_jump = false;

    // check once more:
    if (is_on_ground(map, pos.x, pos.y)) {
        has_jumped = false;
        return;
    }

    if (map.at_bnd(pos.x, pos.y) == WALL) {
        // revert
        vel.x = 0.f;
        vel.y = 0.f;
        pos.x = old_x;
        pos.y = old_y;
        has_jumped = false;
    }
}

void Player::handle_event(SDL_Event &event) {
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
            case SDLK_LEFT:
                if (direction == Right) direction = None;
                else                    direction = Left;
                break;
            case SDLK_RIGHT:
                if (direction == Left)  direction = None;
                else                    direction = Right;
                break;
            case SDLK_UP:
                if (!has_jumped) {
                    vel.y -= JUM_CAP;
                    has_jumped = true;
                    needs_jump = true;
                }
                break;
        }
    } else if (event.type == SDL_KEYUP && event.key.repeat == 0) {
        switch (event.key.keysym.sym) {
            case SDLK_LEFT:
                if (direction == Left)  direction = None;
                else                    direction = Right;
                break;
            case SDLK_RIGHT:
                if (direction == Right) direction = None;
                else                    direction = Left;
                break;
        }
    }
}

void Player::render(SDL_Renderer *renderer) const {
    auto mid_w = SIZE.WIDTH  / 2;
    auto mid_h = SIZE.HEIGHT / 2;
    SDL_Rect rect = {pos.x - mid_w, pos.y - mid_h, mid_w, mid_h};
    const auto& c = colour;
    SDL_SetRenderDrawColor(renderer, 255, 75, 0, 255);
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

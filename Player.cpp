#include "Player.hpp"

static constexpr float JUM_CAP  = 5.f;
static constexpr float VEL_CAP  = 2.f; //cap
static constexpr float ACCEL    = 0.3f;
static constexpr float DECCEL   = 0.85f;
static constexpr float GRAVITY  = 0.3f; 

bool unstuck_walls(Player &self, Map &map) {
    for (int off = 0; off < self.SIZE.HEIGHT; ++off) {
        if (map.at_bnd(self.pos.x, self.pos.y - off) == CellType::EMPTY) {
            self.pos.y = self.pos.y - off;
            return true;
        }
    }
    return false;
}

void move_down(Player &self, Map &map) {
    for (int off = 0; off > -self.SIZE.HEIGHT; --off) {
        if (map.at_bnd(self.pos.x, self.pos.y - off) == CellType::EMPTY) {
            self.pos.y = self.pos.y - off;
            return;
        }
    }
}

// [TODO] Refactor this nightmare
void move_flight(Player &self, Map &map) {
    auto &vel = self.vel;
    auto &pos = self.pos;

    vel.y += GRAVITY;

    int prev_x = pos.x;
    int prev_y = pos.y;

    int curr_x, curr_y;

    // MESSY BOUNDARY CHECKING
    Vector2D step_vel = vel;
    step_vel.normalize();

    // a partial dist
    float step_x = prev_x;
    float step_y = prev_y;

    // max distance to the last possible point
    Vector2D distance(
        (pos.x + vel.x) - pos.x, (pos.y + vel.y) - pos.y
    );
    float distance_len = distance.length();

    Vector2D distance_taken(0.f, 0.f);
    float distance_remain = distance_len - 0.f;

    // the cells
    byte step_cell;
    byte belw_cell;
    do {
        curr_x = (int)(step_x + 0.5f);
        curr_y = (int)(step_y + 0.5f);

        step_cell = map.at_bnd(curr_x, curr_y);
        belw_cell = map.at_bnd(curr_x + 1, curr_y);
        if ((step_cell != CellType::WALL && belw_cell == CellType::WALL)
            || step_cell == CellType::WALL) {
            //LOG_ERR("WALL!");
            break;
        }
        step_x += step_vel.x;
        step_y += step_vel.y;

        distance_taken.x = step_x - pos.x;
        distance_taken.y = step_y - pos.y;

        distance_remain = distance_len - distance_taken.length();
    } while (distance_remain > 0.f);

    Vector2D refl_vector = map.refl_vector(vel, step_x, step_y);

    pos.x = (int)(step_x + 0.5f);
    pos.y = (int)(step_y + 0.5f);

    // Collision with wall, move the player back
    if (belw_cell == CellType::WALL || step_cell == CellType::WALL) {
        vel.y = -refl_vector.y * DECCEL;
        vel.x =  refl_vector.x;
        pos.x = prev_x + vel.x + 0.5f;
        pos.y = prev_y + vel.y + 0.5f;
        self.has_jumped = false;
    }
} 

void move_walking(Player &self, Map &map) {
    bool success = true;
    self.pos.x += self.vel.x + 0.5f; 
    self.vel.y = 0.f;

    byte curr_cell = map.at_bnd(self.pos.x, self.pos.y);
    if (curr_cell == CellType::WALL) {
        success = unstuck_walls(self, map);
    } else {
        move_down(self, map);
    }
    if (!success) {
        self.vel.x = -self.vel.x * DECCEL;
        self.pos.x += self.vel.x + 0.5f;
    }
}

void Player::place(int x, int y, float vel_x, float vel_y) {
    this->pos.x = x;
    this->pos.y = y;
    this->vel.x = vel_x;
    this->vel.y = vel_y;
}

void Player::move(Map &map) {
    // is on ground?
    byte curr_cell = map.at_bnd(pos.x, pos.y);
    // if standing on a wall 
    if (map.at_bnd(pos.x, pos.y + 1) == CellType::WALL 
            && curr_cell != CellType::WALL
            && !has_jumped) { //&& bounces >= abs(vel.y / 3)) {
        //bounces = 0;
        is_on_ground = true;
    } 
    // if in the air 
    else {
        is_on_ground = false;
    }

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
    if (is_on_ground) {
        has_jumped = false; 
        move_walking(*this, map);
    } else {
        move_flight(*this, map);
        return;
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

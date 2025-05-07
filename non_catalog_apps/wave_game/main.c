#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <math.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define PLAYER_WIDTH 9
#define PLAYER_HEIGHT 8
#define PLAYER_SPEED 1.0f
#define GAP_START 40.0f
#define LEVEL_WIDTH SCREEN_WIDTH
#define HIGHSCORE_PATH APP_DATA_PATH("highscore.txt")
#define TRAIL_LENGTH 64
#define START_DELAY_MS 1000

typedef struct {
    float x, y;
    float speed;
    bool button_pressed;
} Player;

typedef struct {
    float x, y;
    bool active;
} TrailSegment;

typedef struct {
    Player player;
    uint8_t level[LEVEL_WIDTH];
    TrailSegment trail[TRAIL_LENGTH];
    int write_index;
    int8_t dir;
    float gap;
    float score_acc;
    float score;
    int highscore;
    bool game_over;
    bool restart_requested;
    bool exit_requested;
    bool paused;
    bool start_delay_active;
    uint32_t start_time;
    FuriMutex* mutex;
} GameState;

int interpolate(int x0, int y0, int x1, int y1, int y) {
    if(y1 == y0) return x0;
    return x0 + (x1 - x0) * (y - y0) / (y1 - y0);
}

int load_highscore() {
    int hs = 0;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, HIGHSCORE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[16];
        int len = storage_file_read(file, buf, sizeof(buf)-1);
        if(len > 0) buf[len] = '\0', hs = atoi(buf);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return hs;
}

void save_highscore(int hs) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, HIGHSCORE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%d", hs);
        storage_file_write(file, buf, len);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

void game_reset(GameState* g) {
    g->player.x = 40;
    g->player.y = SCREEN_HEIGHT / 2;
    g->player.speed = PLAYER_SPEED;
    g->player.button_pressed = false;
    g->write_index = 0;
    g->dir = 1;
    g->gap = GAP_START;
    uint8_t start = (SCREEN_HEIGHT + 1 - (int)g->gap) / 2;
    for(int i = 0; i < LEVEL_WIDTH; i++) g->level[i] = start;
    for(int i = 0; i < TRAIL_LENGTH; i++) {
        g->trail[i].y = -1;
        g->trail[i].x = -1;
        g->trail[i].active = false;
    }
    g->score_acc = 0.0f;
    g->score = 0.0f;
    g->game_over = false;
    g->restart_requested = false;
    g->exit_requested = false;
    g->paused = false;
    g->start_delay_active = true;
    g->start_time = furi_get_tick();
}

void generate_next_column(GameState* g) {
    int prev = (g->write_index + LEVEL_WIDTH - 1) % LEVEL_WIDTH;
    uint8_t cur = g->level[prev];
    if(cur == 0) g->dir = 1;
    else if(cur + (int)g->gap >= SCREEN_HEIGHT) g->dir = -1;
    else if(rand() % 21 == 0) g->dir = -g->dir;
    cur += g->dir;
    g->level[g->write_index] = cur;
    g->write_index = (g->write_index + 1) % LEVEL_WIDTH;
}

void draw_tunnel(Canvas* c, GameState* g) {
    for(int x = 0; x < LEVEL_WIDTH; x++) {
        int idx = (g->write_index + x) % LEVEL_WIDTH;
        uint8_t gs = g->level[idx];
        int ge = gs + (int)g->gap;
        canvas_draw_line(c, x, 0, x, gs);
        canvas_draw_line(c, x, ge, x, SCREEN_HEIGHT);
    }
}

void check_collision(GameState* g) {
    int px = (int)g->player.x;
    if(px >= 0 && px < LEVEL_WIDTH) {
        int idx = (g->write_index + px) % LEVEL_WIDTH;
        uint8_t gs = g->level[idx];
        int ge = gs + (int)g->gap;
        if(g->player.y < gs || g->player.y + PLAYER_HEIGHT > ge) {
            g->game_over = true;
            if((int)g->score > g->highscore) {
                g->highscore = (int)g->score;
                save_highscore(g->highscore);
            }
        }
    }
}

void draw_player(Canvas* c, GameState* g) {
    (void)c;
    float angle = g->player.button_pressed ? -0.8f : 0.8f;
    float center_x = g->player.x;
    float center_y = g->player.y + PLAYER_HEIGHT / 2.0f;
    float half_w = PLAYER_WIDTH / 2.0f;
    float half_h = PLAYER_HEIGHT / 2.0f;
    float nose_x = center_x + half_w;
    float nose_y = center_y;
    float base_left_x = center_x - half_w;
    float base_left_y = center_y - half_h;
    float base_right_x = center_x - half_w;
    float base_right_y = center_y + half_h;
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);

    float rotate_x(float x, float y) { return cos_a * (x - center_x) - sin_a * (y - center_y) + center_x; }
    float rotate_y(float x, float y) { return sin_a * (x - center_x) + cos_a * (y - center_y) + center_y; }

    int x1 = (int)rotate_x(nose_x, nose_y);
    int y1 = (int)rotate_y(nose_x, nose_y);
    int x2 = (int)rotate_x(base_left_x, base_left_y);
    int y2 = (int)rotate_y(base_left_x, base_left_y);
    int x3 = (int)rotate_x(base_right_x, base_right_y);
    int y3 = (int)rotate_y(base_right_x, base_right_y);

    if(y2 > y3) { int tx = x2, ty = y2; x2 = x3; y2 = y3; x3 = tx; y3 = ty; }
    if(y1 > y2) { int tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    if(y2 > y3) { int tx = x2, ty = y2; x2 = x3; y2 = y3; x3 = tx; y3 = ty; }

    for(int y = y1; y <= y3; ++y) {
        int xa, xb;
        if(y < y2) {
            xa = interpolate(x1, y1, x2, y2, y);
            xb = interpolate(x1, y1, x3, y3, y);
        } else {
            xa = interpolate(x2, y2, x3, y3, y);
            xb = interpolate(x1, y1, x3, y3, y);
        }
        if(xa > xb) { int tmp = xa; xa = xb; xb = tmp; }
        canvas_draw_line(c, xa, y, xb, y);
    }
}

void game_update(GameState* g) {
    if(furi_mutex_acquire(g->mutex, FuriWaitForever) != FuriStatusOk) return;
    if(g->exit_requested) { furi_mutex_release(g->mutex); return; }
    if(g->restart_requested) { game_reset(g); furi_mutex_release(g->mutex); return; }
    if(g->game_over || g->paused) { furi_mutex_release(g->mutex); return; }

    uint32_t now = furi_get_tick();
    if(g->start_delay_active && now - g->start_time < START_DELAY_MS) {
        furi_mutex_release(g->mutex);
        return;
    }
    g->start_delay_active = false;

    if(g->player.button_pressed) g->player.y -= g->player.speed;
    else g->player.y += g->player.speed;
    if(g->player.y < 0) g->player.y = 0;
    if(g->player.y > SCREEN_HEIGHT - PLAYER_HEIGHT) g->player.y = SCREEN_HEIGHT - PLAYER_HEIGHT;

    generate_next_column(g);
    for(int i = 0; i < TRAIL_LENGTH-1; i++) g->trail[i] = g->trail[i+1];
    g->trail[TRAIL_LENGTH-1] = (TrailSegment){.x=g->player.x, .y=g->player.y + PLAYER_HEIGHT/2 -1, .active=true};
    for(int i = 0; i < TRAIL_LENGTH; i++) if(g->trail[i].active) g->trail[i].x -= 1;

    int obs_idx = (g->write_index + (int)g->player.x) % LEVEL_WIDTH;
    float gs = g->level[obs_idx];
    float center = gs + g->gap/2;
    float dist = fabsf(g->player.y - center);
    float closeness = fmaxf(0.0f, 1.0f - (dist / (g->gap/2)));
    float rate = 0.2f + closeness * 0.1f;
    g->score_acc += rate;
    g->score = g->score_acc;

    check_collision(g);
    furi_mutex_release(g->mutex);
}

void game_render(Canvas* c, void* ctx) {
    GameState* g = ctx;
    if(furi_mutex_acquire(g->mutex, FuriWaitForever) != FuriStatusOk) return;

    canvas_clear(c);
    uint32_t now = furi_get_tick();

    if(g->start_delay_active) {
        uint32_t elapsed = now - g->start_time;
        int bar_x = 14;
        int bar_w_max = SCREEN_WIDTH - bar_x * 2;
        int bar_h = 12;
        int bar_y = SCREEN_HEIGHT / 2;

        int bar_w = (bar_w_max * (int)elapsed) / START_DELAY_MS;
        if(bar_w > bar_w_max) bar_w = bar_w_max;

        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, SCREEN_WIDTH / 2, bar_y - 14, AlignCenter, AlignCenter, "WAVE");

        canvas_draw_frame(c, bar_x, bar_y, bar_w_max, bar_h);

        canvas_draw_box(c, bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2);

        canvas_set_font(c, FontSecondary);
        canvas_set_color(c, ColorWhite);
        canvas_draw_str_aligned(c, SCREEN_WIDTH / 2, bar_y + bar_h / 2, AlignCenter, AlignCenter, "get ready");

        canvas_set_color(c, ColorBlack);
    } else if(!g->game_over) {
        draw_tunnel(c, g);
        for(int i = 0; i < TRAIL_LENGTH; i++) {
            if(g->trail[i].active) {
                canvas_draw_box(c, (int)g->trail[i].x, (int)g->trail[i].y, 2, 3);
            }
        }
        draw_player(c, g);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)g->score);
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, AlignCenter, AlignCenter, buf);
    } else {
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 18, AlignCenter, AlignCenter, "GAME OVER");

        char hs_buf[32];
        snprintf(hs_buf, sizeof(hs_buf), "High Score - %d", g->highscore);
        canvas_draw_str_aligned(c, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 6, AlignCenter, AlignCenter, hs_buf);
        canvas_draw_str_aligned(c, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 6, AlignCenter, AlignCenter, "Ok - Restart");
        canvas_draw_str_aligned(c, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 18, AlignCenter, AlignCenter, "Back - Exit");
    }

    furi_mutex_release(g->mutex);
}

void input_callback(InputEvent* ev, void* ctx) {
    GameState* g = ctx;
    if(furi_mutex_acquire(g->mutex, FuriWaitForever) != FuriStatusOk) return;
    if(ev->key == InputKeyBack && ev->type == InputTypePress) {
        if(g->game_over) g->exit_requested = true;
        else g->paused = !g->paused;
    } else if(ev->key == InputKeyOk || ev->key == InputKeyUp) {
        if(ev->type == InputTypePress) {
            if(g->game_over) g->restart_requested = true;
            else g->player.button_pressed = true;
        } else if(ev->type == InputTypeRelease) {
            g->player.button_pressed = false;
        }
    }
    furi_mutex_release(g->mutex);
}

int32_t wave_app(void* p) {
    UNUSED(p);
    srand(furi_get_tick());
    Gui* gui = furi_record_open(RECORD_GUI);
    static GameState g_state;
    GameState* g = &g_state;
    g->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    g->highscore = load_highscore();
    game_reset(g);
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, game_render, g);
    view_port_input_callback_set(vp, input_callback, g);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);
    while(!g->exit_requested) {
        game_update(g);
        view_port_update(vp);
        furi_delay_ms(16);
    }
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_mutex_free(g->mutex);
    furi_record_close(RECORD_GUI);
    return 0;
}

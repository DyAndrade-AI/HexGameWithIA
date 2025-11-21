#include "ui.h"
#include "hex.h"
#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_WIDTH 1080
#define WINDOW_HEIGHT 840
#define BOARD_MARGIN 70.0f
#define STATUS_FONT_SIZE 24
#define LABEL_FONT_SIZE 20
#define PROMPT_FONT_SIZE 24
#define PROMPT_PANEL_HEIGHT 110.0f

static int window_ready = 0;
static int current_size = 0;
static char board_state[MAX_BOARD_SIZE];
static char status_message[256];

static Vector2 cell_centers[MAX_BOARD_SIZE];
static float cell_radius = 26.0f;
static float cell_step = 40.0f;
static Rectangle board_bounds = {0};

static Color color_background = {245, 245, 245, 255};
static Color color_grid = {200, 200, 200, 255};
static Color color_token_x = {230, 60, 90, 255};
static Color color_token_o = {60, 150, 255, 255};
static Color color_empty = {245, 245, 245, 255};

static int prompt_active = 0;
static char prompt_label[160];
static char prompt_buffer[512];
static size_t prompt_limit = 0;
static double cursor_timer = 0.0;
static int cursor_visible = 1;

static void update_layout(void);
static void render_frame(void);
static void draw_board(void);
static void draw_prompt(void);
static int position_to_index(Vector2 point);
static void reset_prompt_state(void);

int ui_init(void) {
    if (window_ready) return 0;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Hex");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    window_ready = 1;
    memset(board_state, '+', sizeof(board_state));
    status_message[0] = '\0';
    return 0;
}

void ui_shutdown(void) {
    if (!window_ready) return;
    CloseWindow();
    window_ready = 0;
}

void ui_draw_board(const char *board, int size, const char *message) {
    if (!window_ready || !board) return;
    if (size < 0) size = 0;
    if (size > MAX_BOARD_SIDE) size = MAX_BOARD_SIDE;
    current_size = size;
    size_t total = (size_t)size * (size_t)size;
    memcpy(board_state, board, total);
    if (total < sizeof(board_state)) {
        memset(board_state + total, '+', sizeof(board_state) - total);
    }
    if (message) {
        strncpy(status_message, message, sizeof(status_message) - 1);
        status_message[sizeof(status_message) - 1] = '\0';
    } else {
        status_message[0] = '\0';
    }
    render_frame();
}

int ui_wait_move(int size, int *pos_out) {
    if (!window_ready || !pos_out) return 0;
    if (size != current_size) current_size = size;
    while (!WindowShouldClose()) {
        if (!prompt_active && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            int cell = position_to_index(mouse);
            if (cell >= 0) {
                *pos_out = cell;
                return 1;
            }
        }
        if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) {
            return 0;
        }
        render_frame();
    }
    return 0;
}

int ui_prompt(const char *prompt, char *buffer, size_t len) {
    if (!window_ready || !buffer || len == 0) return 0;
    reset_prompt_state();
    prompt_active = 1;
    prompt_limit = len;
    if (prompt) {
        strncpy(prompt_label, prompt, sizeof(prompt_label) - 1);
        prompt_label[sizeof(prompt_label) - 1] = '\0';
    } else {
        prompt_label[0] = '\0';
    }
    buffer[0] = '\0';

    while (!WindowShouldClose()) {
        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 125) {
                size_t current_len = strlen(prompt_buffer);
                if (current_len + 1 < sizeof(prompt_buffer) && current_len + 1 < prompt_limit) {
                    prompt_buffer[current_len] = (char)key;
                    prompt_buffer[current_len + 1] = '\0';
                }
            }
            key = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t current_len = strlen(prompt_buffer);
            if (current_len > 0) {
                prompt_buffer[current_len - 1] = '\0';
            }
        }

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            strncpy(buffer, prompt_buffer, len - 1);
            buffer[len - 1] = '\0';
            prompt_active = 0;
            render_frame();
            return 1;
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            buffer[0] = '\0';
            prompt_active = 0;
            render_frame();
            return 0;
        }

        render_frame();
    }

    prompt_active = 0;
    buffer[0] = '\0';
    return 0;
}

static void reset_prompt_state(void) {
    prompt_active = 0;
    prompt_limit = 0;
    prompt_label[0] = '\0';
    memset(prompt_buffer, 0, sizeof(prompt_buffer));
    cursor_timer = 0.0;
    cursor_visible = 1;
}

static void update_layout(void) {
    if (current_size <= 0) {
        board_bounds = (Rectangle){BOARD_MARGIN, BOARD_MARGIN, 100.0f, 100.0f};
        return;
    }

    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();

    float reserved_bottom = STATUS_FONT_SIZE * 2.5f + BOARD_MARGIN;
    if (prompt_active) reserved_bottom += PROMPT_PANEL_HEIGHT;

    float available_w = (float)screen_w - BOARD_MARGIN * 2.0f;
    float available_h = (float)screen_h - reserved_bottom - BOARD_MARGIN;

    if (available_w < 200.0f) available_w = 200.0f;
    if (available_h < 200.0f) available_h = 200.0f;

    float horizontal_slots = (float)current_size;
    if (current_size > 1) {
        horizontal_slots += (float)(current_size - 1) * 0.5f;
    }

    float step_x = available_w / horizontal_slots;
    float step_y = available_h / (float)current_size;
    cell_step = fminf(step_x, step_y);
    if (cell_step < 28.0f) cell_step = 28.0f;
    cell_radius = cell_step * 0.4f;

    float board_w = horizontal_slots * cell_step;
    float board_h = (float)current_size * cell_step;
    float left = ((float)screen_w - board_w) * 0.5f;
    if (left < BOARD_MARGIN * 0.5f) left = BOARD_MARGIN * 0.5f;
    float top = BOARD_MARGIN;

    board_bounds = (Rectangle){left, top, board_w, board_h};

    for (int y = 0; y < current_size; y++) {
        for (int x = 0; x < current_size; x++) {
            int idx = y * current_size + x;
            float offset_x = (float)y * cell_step * 0.5f;
            float px = left + offset_x + (float)x * cell_step;
            float py = top + (float)y * cell_step;
            cell_centers[idx] = (Vector2){px, py};
        }
    }
}

static void draw_board(void) {
    update_layout();
    BeginDrawing();
    ClearBackground(color_background);

    if (current_size > 0) {
        for (int y = 0; y < current_size; y++) {
            for (int x = 0; x < current_size; x++) {
                int idx = y * current_size + x;
                Vector2 center = cell_centers[idx];

                for (int n = 0; n < 6; n++) {
                    int nx = x;
                    int ny = y;
                    switch (n) {
                        case 0: ny = y - 1; break;
                        case 1: nx = x + 1; ny = y - 1; break;
                        case 2: nx = x + 1; break;
                        case 3: ny = y + 1; break;
                        case 4: nx = x - 1; ny = y + 1; break;
                        case 5: nx = x - 1; break;
                    }
                    if (nx < 0 || ny < 0 || nx >= current_size || ny >= current_size) continue;
                    int neighbor_idx = ny * current_size + nx;
                    DrawLineV(center, cell_centers[neighbor_idx], Fade(color_grid, 0.4f));
                }
            }
        }

        for (int y = 0; y < current_size; y++) {
            for (int x = 0; x < current_size; x++) {
                int idx = y * current_size + x;
                Vector2 center = cell_centers[idx];
                char token = board_state[idx];
                Color outline = (Color){100, 100, 100, 255};
                Color fill = color_empty;
                if (token == 'X') fill = color_token_x;
                if (token == 'O') fill = color_token_o;
                DrawPoly(center, 6, cell_radius, 30.0f, Fade(fill, 0.95f));
                DrawPolyLines(center, 6, cell_radius, 30.0f, outline);
            }
        }

        for (int n = 0; n < current_size; n++) {
            char label[4];
            snprintf(label, sizeof(label), "%c", 'A' + n);
            Vector2 top_center = cell_centers[n];
            DrawText(label, (int)(top_center.x - 8), (int)(board_bounds.y - 30), LABEL_FONT_SIZE, DARKGRAY);
        }
        for (int n = 0; n < current_size; n++) {
            char label[4];
            snprintf(label, sizeof(label), "%d", n + 1);
            Vector2 left_center = cell_centers[n * current_size];
            DrawText(label, (int)(board_bounds.x - 40), (int)(left_center.y - 10), LABEL_FONT_SIZE, DARKGRAY);
        }
    }

    int screen_h = GetScreenHeight();
    DrawRectangle(0, screen_h - (STATUS_FONT_SIZE * 2 + 30), GetScreenWidth(), STATUS_FONT_SIZE * 2 + 30, Fade(color_grid, 0.15f));
    DrawText(status_message, (int)BOARD_MARGIN / 2, screen_h - STATUS_FONT_SIZE - 10, STATUS_FONT_SIZE, DARKGRAY);

    draw_prompt();

    EndDrawing();
}

static void draw_prompt(void) {
    if (!prompt_active) return;

    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    float panel_width = (float)screen_w - BOARD_MARGIN;
    if (panel_width < 300.0f) panel_width = 300.0f;
    float panel_left = (screen_w - panel_width) * 0.5f;
    float panel_top = (float)screen_h - PROMPT_PANEL_HEIGHT - 15.0f;

    Rectangle panel = {panel_left, panel_top, panel_width, PROMPT_PANEL_HEIGHT};
    DrawRectangleRounded(panel, 0.1f, 10, Fade(BLACK, 0.1f));
    DrawRectangleRoundedLines(panel, 0.1f, 10, Fade(BLACK, 0.5f));

    DrawText(prompt_label, (int)(panel_left + 16), (int)(panel_top + 12), PROMPT_FONT_SIZE, BLACK);

    cursor_timer += GetFrameTime();
    if (cursor_timer >= 0.5f) {
        cursor_timer = 0.0;
        cursor_visible = !cursor_visible;
    }

    char input_line[sizeof(prompt_buffer) + 2];
    snprintf(input_line, sizeof(input_line), "%s%s", prompt_buffer, (cursor_visible ? "_" : " "));
    DrawText(input_line, (int)(panel_left + 16), (int)(panel_top + 50), PROMPT_FONT_SIZE, DARKGRAY);
}

static int position_to_index(Vector2 point) {
    if (current_size <= 0) return -1;
    for (int i = 0; i < current_size * current_size; i++) {
        Vector2 center = cell_centers[i];
        float dx = point.x - center.x;
        float dy = point.y - center.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist <= cell_radius * 1.2f) {
            return i;
        }
    }
    return -1;
}

static void render_frame(void) {
    if (!window_ready) return;
    draw_board();
}

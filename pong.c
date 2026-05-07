#include "base/base.h"
#include <raylib.h>
#include <raymath.h>

#define COLOR_BG 0x11111bff
#define COLOR_UI 0xffffffff
#define COLOR_FG 0xeeeeeeff

#define FONT_SIZE 100

#define PLAYER_COUNT  2
#define PLAYER_HEIGHT 60
#define PLAYER_WIDTH  10
#define PLAYER_OFFSET 5
#define PLAYER_SPEED  500
#define PLAYER_COLOR  COLOR_FG

#define BALL_SIZE  10
#define BALL_SPEED 350
#define BALL_COLOR COLOR_FG

#define KEY_PLAYER_1_UP   KEY_W
#define KEY_PLAYER_1_DOWN KEY_S
#define KEY_PLAYER_2_UP   KEY_LEFT_BRACKET
#define KEY_PLAYER_2_DOWN KEY_APOSTROPHE
#define KEY_PAUSE         KEY_ESCAPE

#define GAMEPAD_STICK_DEADZONE 0.1f

typedef enum {
    SCREEN_INPUT_SELECT,
    SCREEN_GAME,
    SCREEN_PAUSE,
} Screen;

typedef enum {
    INPUT_NOT_SELECTED,
    INPUT_KEYBOARD_LEFT,
    INPUT_KEYBOARD_RIGHT,
    INPUT_GAMEPAD,
} InputKind;

struct {
    i32 up;
    i32 down;
    i32 pause;
    i32 exit;
} inputs[] = {
    [INPUT_KEYBOARD_LEFT] = {
        .up    = KEY_W,
        .down  = KEY_S,
        .pause = KEY_ESCAPE,
    },
    [INPUT_KEYBOARD_RIGHT] = {
        .up    = KEY_UP,
        .down  = KEY_DOWN,
        .pause = KEY_ESCAPE,
    },
    // gamepad inputs also have left and right stick controls respectively
    [INPUT_GAMEPAD] = {
        // TODO: change to LEFT_FACE_* once virtual gamepad emulator is setup properly
        .up    = GAMEPAD_BUTTON_RIGHT_FACE_UP,
        .down  = GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
        .pause = GAMEPAD_BUTTON_MIDDLE_RIGHT,
        .exit  = GAMEPAD_BUTTON_MIDDLE_LEFT,
    },
};

static const char *InputKindStr(InputKind kind) {
    switch (kind) {
        case INPUT_NOT_SELECTED  : return "press any key";
        case INPUT_KEYBOARD_LEFT : return "keyboard left";
        case INPUT_KEYBOARD_RIGHT: return "keyboard right";
        case INPUT_GAMEPAD  : return "gamepad";
    }
    return "unknown input";
}

typedef struct {
    InputKind chosenInput;
    u8 gamepad;
    Vector2 position;
    f32 velocityY;
    Vector2 size;
    Color color;
    u32 score;
} Player;

typedef struct {
    Vector2 position;
    Vector2 velocity;
    Vector2 size;
    Color color;
    const Player* lastHit;
} Ball;

static bool CheckCollisionPlayerBall(const Player *p, const Ball *b) {
    return (p->position.x < b->position.x + b->size.x) &&
           (p->position.x + p->size.x > b->position.x) &&
           (p->position.y < b->position.y + b->size.y) &&
           (p->position.y + p->size.y > b->position.y);
}

static Ball BallSpawnAtPlayer(const Player *p, bool player2) {
    return (Ball){
        .position = {
            .x = player2 ? p->position.x - BALL_SIZE : p->position.x + p->size.x,
            .y = p->position.y + (p->size.y - BALL_SIZE) / 2.0f,
        },
        .size = {
            .x = BALL_SIZE,
            .y = BALL_SIZE,
        },
        .color = GetColor(BALL_COLOR),
        .lastHit = p,
    };
}

// well actually it's not THAAAT crazy you know
#define INPUT_IS(action, player, key)                                       \
    ((player).chosenInput == INPUT_KEYBOARD_LEFT || (player).chosenInput == INPUT_KEYBOARD_RIGHT \
         ? IsKey##action(inputs[(player).chosenInput].key)                              \
         : IsGamepadButton##action((player).gamepad, inputs[(player).chosenInput].key))

#define INPUT_STICK_Y(inputKind)                                               \
    ((inputKind) == INPUT_GAMEPAD_LEFT    ? leftStickY                         \
     : (inputKind) == INPUT_GAMEPAD_RIGHT ? rightStickY                        \
                                          : 0)
i32 AnyGamepadButtonPressed(void) {
    for (usize gamepad = 0; gamepad < PLAYER_COUNT; ++gamepad) {
        for (usize button = 0; button < MAX_GAMEPAD_BUTTONS; ++button) {
            if (IsGamepadButtonPressed(gamepad, button)) {
                return gamepad;
            }
        }
    }

    return -1;
}

f32 PlayerGetGamepadAxisMovement(const Player *p, GamepadAxis axis) {
    if (p->chosenInput != INPUT_GAMEPAD || !IsGamepadAvailable(p->gamepad)) {
        return 0.0f;
    }
    f32 movement = GetGamepadAxisMovement(p->gamepad, axis);
    if (movement > -GAMEPAD_STICK_DEADZONE && movement < GAMEPAD_STICK_DEADZONE) {
        movement = 0.0f;
    }
    return movement;
}

static inline f32 sign(f32 x) {
    return x >= 0 ? 1 : -1;
}

static inline f32 frac(f32 x) {
    return x - floor(x);
}

// https://graphtoy.com/?f1(x,t)=(cos(x%20*%202%20*%20PI)%20+%201)%20/%202.0&v1=true&f2(x,t)=(sign(sin(x%20*%202%20*%20PI))%20+%201)%20/%202.0&v2=true&f3(x,t)=1%20-%20frac(x)&v3=true&f4(x,t)=((1%20-%204%20*%20abs(frac(x%20+%200.5)%20-%200.5))%20+%201)%20/%202.0&v4=true&f5(x,t)=&v5=false&f6(x,t)=&v6=false&grid=2&coords=0,0,1.3318485523385302
MAYBE_UNUSED static inline f32 shape_cosine(f32 x, f32 freq) {
    return (cosf((x * 2 * PI) * freq) + 1) / 2.0f;
}

MAYBE_UNUSED static inline f32 shape_square(f32 x, f32 freq) {
    return (sign(sinf((x * 2 * PI) * freq)) + 1) / 2.0f;
}

MAYBE_UNUSED static inline f32 shape_sawtooth(f32 x, f32 freq) {
    return 1 - frac(x * freq);
}

MAYBE_UNUSED static inline f32 shape_triangle(f32 x, f32 freq) {
    return ((1 - 4 * fabsf(frac((x + 0.5f) * freq) - 0.5f)) + 1) / 2.0f;
}

i32 main(void) {
    InitWindow(800, 600, "pong");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    i32 w = GetScreenWidth();
    i32 h = GetScreenHeight();
    Font font = GetFontDefault();

    Screen currentScreen = SCREEN_INPUT_SELECT;

    Player p1 = {
        .position = {
            .x = PLAYER_OFFSET,
            .y = (h - PLAYER_HEIGHT) / 2.0f,
        },
        .size = {
            .x = PLAYER_WIDTH,
            .y = PLAYER_HEIGHT,
        },
        .color = GetColor(PLAYER_COLOR),
    };

    Player p2 = {
        .position = {
            .x = w - (PLAYER_WIDTH + PLAYER_OFFSET),
            .y = (h - PLAYER_HEIGHT) / 2.0f,
        },
        .size = {
            .x = PLAYER_WIDTH,
            .y = PLAYER_HEIGHT,
        },
        .color = GetColor(PLAYER_COLOR),
    };

    Ball ball = BallSpawnAtPlayer(&p1, false);

    Color colorUI = ColorAlpha(GetColor(COLOR_UI), 0.53f);
    Color colorBG = GetColor(COLOR_BG);

    f32 t = 0.0f;

    while (!WindowShouldClose()) {
        f32 dt = GetFrameTime();
        t += dt;

        if (INPUT_IS(Pressed, p1, exit) ||
            INPUT_IS(Pressed, p2, exit)) {
            goto close;
        }

        switch (currentScreen) {
            case SCREEN_INPUT_SELECT: {
                i32 gp = AnyGamepadButtonPressed();
                if (p1.chosenInput == INPUT_NOT_SELECTED) {
                    if (gp != -1) {
                        p1.chosenInput = INPUT_GAMEPAD;
                        p1.gamepad = gp;
                    } else if (GetKeyPressed() > 0) {
                        p1.chosenInput = INPUT_KEYBOARD_LEFT;
                    }
                } else if (p2.chosenInput == INPUT_NOT_SELECTED) {
                    if (gp != -1 && ((p1.chosenInput == INPUT_GAMEPAD && p1.gamepad != gp) || p1.chosenInput != INPUT_GAMEPAD)) {
                        p2.chosenInput = INPUT_GAMEPAD;
                        p2.gamepad = gp;
                    } else if (GetKeyPressed() > 0) {
                        if (p1.chosenInput == INPUT_KEYBOARD_LEFT) {
                            p2.chosenInput = INPUT_KEYBOARD_RIGHT;
                        } else {
                            p2.chosenInput = INPUT_KEYBOARD_LEFT;
                        }
                    }
                } else {
                    if (GetKeyPressed() > 0 || AnyGamepadButtonPressed() != -1) {
                        currentScreen = SCREEN_GAME;
                        DisableCursor();
                    }
                }
            } break;
            case SCREEN_GAME: {
                if (INPUT_IS(Pressed, p1, pause) ||
                    INPUT_IS(Pressed, p2, pause)) {
                    currentScreen = SCREEN_PAUSE;
                    EnableCursor();
                    break;
                }

                if (INPUT_IS(Down, p1, up) || PlayerGetGamepadAxisMovement(&p1, GAMEPAD_AXIS_LEFT_Y) < 0.0f) {
                    p1.velocityY = -1;
                } else if (INPUT_IS(Down, p1, down) || PlayerGetGamepadAxisMovement(&p1, GAMEPAD_AXIS_LEFT_Y) > 0.0f) {
                    p1.velocityY = 1;
                } else {
                    p1.velocityY = 0;
                }

                if (INPUT_IS(Down, p2, up) || PlayerGetGamepadAxisMovement(&p2, GAMEPAD_AXIS_LEFT_Y) < 0.0f) {
                    p2.velocityY = -1;
                } else if (INPUT_IS(Down, p2, down) || PlayerGetGamepadAxisMovement(&p2, GAMEPAD_AXIS_LEFT_Y) > 0.0f) {
                    p2.velocityY = 1;
                } else {
                    p2.velocityY = 0;
                }

                p1.position.y += p1.velocityY * PLAYER_SPEED * dt;
                p2.position.y += p2.velocityY * PLAYER_SPEED * dt;

                if (p1.position.y <= PLAYER_OFFSET) {
                    p1.position.y = PLAYER_OFFSET;
                } else if (p1.position.y + p1.size.y >= h - PLAYER_OFFSET) {
                    p1.position.y = h - PLAYER_OFFSET - p1.size.y;
                }
                if (p2.position.y <= PLAYER_OFFSET) {
                    p2.position.y = PLAYER_OFFSET;
                } else if (p2.position.y + p2.size.y >= h - PLAYER_OFFSET) {
                    p2.position.y = h - PLAYER_OFFSET - p2.size.y;
                }

                if (ball.velocity.x == 0 && ball.velocity.y == 0 && ball.lastHit->velocityY != 0) {
                    if (ball.lastHit == &p1) {
                        ball.velocity.x = 1;
                    } else {
                        ball.velocity.x = -1;
                    }
                    ball.velocity.y = ball.lastHit->velocityY;
                }

                if (CheckCollisionPlayerBall(&p1, &ball)) {
                    ball.position.x = p1.position.x + p1.size.x;
                    ball.velocity.x = 1;
                } else if (CheckCollisionPlayerBall(&p2, &ball)) {
                    ball.position.x = p2.position.x - ball.size.x;
                    ball.velocity.x = -1;
                }

                if (ball.position.x > w) {
                    p1.score += 1;
                    ball = BallSpawnAtPlayer(&p1, false);
                } else if (ball.position.x + ball.size.x < 0) {
                    p2.score += 1;
                    ball = BallSpawnAtPlayer(&p2, true);
                }

                if (ball.position.y <= 0 || ball.position.y + ball.size.y >= h) {
                    ball.velocity.y *= -1;
                }

                ball.position.x += ball.velocity.x * BALL_SPEED * dt;
                ball.position.y += ball.velocity.y * BALL_SPEED * dt;
            } break;
            case SCREEN_PAUSE: {
                if (INPUT_IS(Pressed, p1, pause) || INPUT_IS(Pressed, p2, pause)) {
                    currentScreen = SCREEN_GAME;
                    DisableCursor();
                    break;
                }
            } break;
        }

        BeginDrawing();
        switch (currentScreen) {
            case SCREEN_INPUT_SELECT: {
                ClearBackground(colorBG);
                Color color = colorUI;
                // 2 + 2 is 4, - 1 that's 3 quick mafs
                f32 alpha = shape_triangle(t, 1) / 2.0f + 0.2f;

                if (p1.chosenInput == INPUT_NOT_SELECTED) {
                    color = ColorAlpha(colorUI, alpha);
                } else {
                    color = colorUI;
                }
                DrawText(TextFormat("player 1:\n%s",
                                    p1.chosenInput == INPUT_GAMEPAD
                                        ? GetGamepadName(p1.gamepad)
                                        : InputKindStr(p1.chosenInput)),
                         36, h / 2.f - 69, 36, color);
                if (p1.chosenInput != INPUT_NOT_SELECTED) {
                    if (p2.chosenInput == INPUT_NOT_SELECTED) {
                        color = ColorAlpha(colorUI, alpha);
                    } else {
                        color = colorUI;
                    }
                } else {
                    color = ColorAlpha(colorUI, 0.2);
                }
                DrawText(TextFormat("player 2:\n%s",
                                    p2.chosenInput == INPUT_GAMEPAD
                                        ? GetGamepadName(p2.gamepad)
                                        : InputKindStr(p2.chosenInput)),
                         36, h / 2.f + 69, 36, color);
                if (p1.chosenInput != INPUT_NOT_SELECTED && p2.chosenInput != INPUT_NOT_SELECTED) {
                    const char *txt = TextFormat("press any key to begin playing");
                    const u8 fontSize = 32;
                    Vector2 txtV = MeasureTextEx(font, txt, fontSize, fontSize / 10.0f);
                    DrawText(txt, (w - txtV.x) / 2.0f, h - txtV.y * 2, fontSize,
                             ColorAlpha(colorUI, alpha));
                }
            } break;
            case SCREEN_GAME: FALLTHROUGH;
            case SCREEN_PAUSE: {
                ClearBackground(colorBG);

                f32 centerX = w / 2.0f;
                f32 centerY = h / 2.0f;

                DrawLineDashed((Vector2){centerX, 0}, (Vector2){centerX, h}, 10, 5, colorUI);

                const char *score1 = TextFormat("%ld", p1.score);
                Vector2 score1V = MeasureTextEx(font, score1, FONT_SIZE, 0);
                DrawText(score1, (centerX - score1V.x) / 2.0f, centerY - score1V.y / 2.0f + 5, FONT_SIZE, colorUI);

                const char *score2 = TextFormat("%ld", p2.score);
                Vector2 score2V = MeasureTextEx(font, score2, FONT_SIZE, 0);
                DrawText(score2, centerX + (centerX - score2V.x) / 2.0f, centerY - score2V.y / 2.0f + 5, FONT_SIZE, colorUI);

                DrawRectangleV(p1.position, p1.size, p1.color);
                DrawRectangleV(p2.position, p2.size, p2.color);

                DrawRectangleV(ball.position, ball.size, ball.color);

                if (currentScreen == SCREEN_PAUSE) {
                    DrawRectangle(0, 0, w, h, ColorAlpha(colorUI, 0.1));

                    f32 pauseW = 20;
                    f32 pauseH = 70;
                    DrawRectangle(centerX - pauseW - 8, centerY - pauseH / 2.0f, pauseW, pauseH, GetColor(COLOR_FG));
                    DrawRectangle(centerX + 8, centerY - pauseH / 2.0f, pauseW, pauseH, GetColor(COLOR_FG));
                }
            } break;
        }
        EndDrawing();
    }

close:
    CloseWindow();
}

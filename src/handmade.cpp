#include "raylib.h"

static bool running;

int main() {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(800, 600, "Handmade Hero");
  SetExitKey(KEY_NULL);
  while (!WindowShouldClose()) {
    if (WindowShouldClose()) {
      running = false;
    } else {
      static Color my_color = WHITE;
      BeginDrawing();
      ClearBackground(my_color);
      DrawText("Congrats! You created your first window!", 190, 200, 20,
               LIGHTGRAY);
      EndDrawing();
      if (my_color.r == WHITE.r) {
        my_color = BLACK;
      } else {
        my_color = WHITE;
      }
    }
  }

  return 0;
}

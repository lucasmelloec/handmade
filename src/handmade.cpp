#include "raylib.h"

int main() {
  InitWindow(800, 600, "Handmade Hero");
  SetExitKey(KEY_NULL);
  while (!WindowShouldClose()) {
    BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Congrats! You created your first window!", 190, 200, 20,
                LIGHTGRAY);
    EndDrawing();
  }

  return 0;
}

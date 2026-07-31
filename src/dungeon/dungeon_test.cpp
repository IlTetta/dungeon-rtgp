// Small standalone test program for the dungeon generator and its 3D layout.
// It builds a dungeon, prints it as ASCII, and then prints some statistics about the 3D layout
// (how many floor/wall boxes and lights it produces, and the overall size of the level).
// No OpenGL here: we only test the pure logic (grid + box/light placement) before rendering.
//
// Built as a SEPARATE executable (target "bsp_test"), so we can run it without the 3D window.

#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"

int main() {
    int width = 60;
    int height = 30;
    unsigned int seed = 12345;

    DungeonGenerator generator(width, height, seed);
    Dungeon dungeon = generator.generate();

    std::cout << "Dungeon " << width << "x" << height << " (seed " << seed << ")\n\n";
    printDungeon(dungeon);

    // --- 3D layout statistics ---
    DungeonLayout layout = buildDungeonLayout(dungeon);

    int floorCount = 0;
    int wallCount = 0;
    for (const BoxPlacement& box : layout.boxes) {
        if (box.material == MAT_FLOOR) floorCount++;
        else wallCount++;
    }

    std::cout << "\n--- 3D layout ---\n";
    std::cout << "rooms        : " << dungeon.rooms.size() << "\n";
    std::cout << "floor boxes  : " << floorCount << "\n";
    std::cout << "wall boxes   : " << wallCount << "\n";
    std::cout << "total boxes  : " << layout.boxes.size() << "  (each = 1 RenderObject = 1 draw call)\n";
    std::cout << "lights       : " << layout.lights.size() << "\n";
    std::cout << "triangles    : ~" << layout.boxes.size() * 12 << "  (12 per cube)\n";

    return 0;
}

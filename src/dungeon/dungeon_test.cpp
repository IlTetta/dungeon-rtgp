// bsp_test: a small console program to check the dungeon generator without the 3D window.
// It prints the dungeon as ASCII and a few numbers about its 3D layout.

#include <cstdlib>

#include "dungeon/dungeon_generator.h"
#include "world/dungeon_geometry.h"

int main(int argc, char** argv) {
    int width = 60;
    int height = 30;
    // reference seed by default; "bsp_test <seed>" previews any other dungeon without recompiling
    unsigned int seed = 12345;
    if (argc > 1)
        seed = (unsigned int)std::strtoul(argv[1], nullptr, 10);

    DungeonGenerator generator(width, height, seed);
    Dungeon dungeon = generator.generate();

    std::cout << "Dungeon " << width << "x" << height << " (seed " << seed << ")\n\n";
    printDungeon(dungeon);

    // 3D layout statistics
    DungeonLayout layout = buildDungeonLayout(dungeon);

    // NB: everything that is not a floor ends up in wallCount, so it also counts the ceilings
    int floorCount = 0;
    int wallCount = 0;
    for (const BoxPlacement& box : layout.boxes) {
        if (box.material == MAT_FLOOR)
            floorCount++;
        else
            wallCount++;
    }

    std::cout << "\n--- 3D layout ---\n";
    std::cout << "rooms : " << dungeon.rooms.size() << "\n";
    std::cout << "floor boxes : " << floorCount << "\n";
    std::cout << "wall boxes : " << wallCount << "\n";
    std::cout << "total boxes : " << layout.boxes.size() << "  (each = 1 RenderObject = 1 draw call)\n";
    std::cout << "lights : " << layout.lights.size() << "\n";
    std::cout << "triangles : ~" << layout.boxes.size() * 12 << "  (12 per cube)\n";

    return 0;
}

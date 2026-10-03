#pragma once

// Procedural dungeon generator using a BSP (Binary Space Partitioning) approach.
//
// Idea:
//   1. start from one big rectangle (the whole map);
//   2. recursively split it in two smaller rectangles (a random horizontal or vertical cut),
//      until the pieces are small enough;
//   3. in each smallest piece ("leaf") carve a room (a smaller rectangle of FLOOR);
//   4. while coming back up from the recursion, connect a room of the first half with a
//      room of the second half using a corridor. Every split gets joined this way, so the
//      whole dungeon is always connected.
//
// The result is a 2D grid of tiles (WALL / FLOOR).

#include <vector>
#include <random>
#include <iostream>
#include <algorithm>

// A tile of the map. WALL is the "solid" default, FLOOR is where the player can walk.
enum Tile {
    WALL,
    FLOOR
};

// A rectangular area of the map. (x, y) is the top-left corner, w/h are width and height.
struct Rect {
    int x;
    int y;
    int w;
    int h;
};

// The finished dungeon: a grid of tiles, stored as a single flat array of size width*height.
// The tile at column x, row y is at index (y * width + x).
struct Dungeon {
    int width = 0;
    int height = 0;
    std::vector<Tile> tiles;
    std::vector<Rect> rooms;   // the carved rooms, in grid coordinates (used for props, lights, spawn)

    Tile get(int x, int y) const {
        return tiles[y * width + x];
    }

    void set(int x, int y, Tile t) {
        tiles[y * width + x] = t;
    }
};

class DungeonGenerator {
public:
    // width/height = size of the map in tiles. seed = number that makes the random generation
    // repeatable: with the same seed you always get the same dungeon (very useful for
    // debugging and for the performance tests, where we want the same scene every run).
    DungeonGenerator(int width, int height, unsigned int seed)
        : width(width), height(height), rng(seed)
    {
    }

    // Build and return the dungeon.
    Dungeon generate() {
        // start with a map full of WALL
        dungeon.width = width;
        dungeon.height = height;
        dungeon.tiles.assign(width * height, WALL);
        dungeon.rooms.clear();

        // run the recursive partitioning on the whole map
        Rect whole = { 0, 0, width, height };
        splitAndBuild(whole, 0);

        return dungeon;
    }

private:
    // The recursive core. Given an area and the current depth in the tree, it either:
    //  - stops and carves a room (if the area is small enough or we reached the max depth), or
    //  - splits the area in two, recurses on both halves, and connects the two rooms.
    // It RETURNS one room of this sub-tree, so that the caller (the level above) can connect
    // the two halves together.
    Rect splitAndBuild(Rect area, int depth) {
        // can we still split this area? we can only split along a side that is at least twice
        // the minimum leaf size (so that BOTH halves are still big enough).
        bool canSplitVertically = area.w >= 2 * minLeafSize;    // cut the width: left | right
        bool canSplitHorizontally = area.h >= 2 * minLeafSize;  // cut the height: top / bottom

        // stop condition: too deep, or the area is too small to be split anymore
        if (depth >= maxDepth || (!canSplitVertically && !canSplitHorizontally)) {
            return carveRoom(area);
        }

        // choose how to cut: if only one direction is possible, use that one; otherwise random.
        bool cutVertically;
        if (canSplitVertically && !canSplitHorizontally)
            cutVertically = true;
        else if (canSplitHorizontally && !canSplitVertically)
            cutVertically = false;
        else
            cutVertically = (randRange(0, 1) == 0);

        Rect first, second;
        if (cutVertically) {
            // pick a vertical cut position, keeping at least minLeafSize on both sides
            int cut = randRange(minLeafSize, area.w - minLeafSize);
            first  = { area.x, area.y, cut, area.h };
            second = { area.x + cut, area.y, area.w - cut, area.h };
        }
        else {
            // horizontal cut
            int cut = randRange(minLeafSize, area.h - minLeafSize);
            first  = { area.x, area.y, area.w, cut };
            second = { area.x, area.y + cut, area.w, area.h - cut };
        }

        // recurse on both halves; each returns one of its rooms
        Rect roomA = splitAndBuild(first, depth + 1);
        Rect roomB = splitAndBuild(second, depth + 1);

        // dig a corridor between the two rooms so the two halves are connected
        connect(roomA, roomB);

        // pass one room up, so the level above can connect this whole block to its sibling.
        return roomA;
    }

    // Carve a room (a rectangle of FLOOR) somewhere inside "area", leaving a 1-tile margin so
    // rooms of neighboring areas do not touch. Returns the room rectangle.
    Rect carveRoom(Rect area) {
        // random room size: at least 4, at most the area size minus a 1-tile margin on each side
        int roomW = randRange(4, area.w - 2);
        int roomH = randRange(4, area.h - 2);
        // random room position inside the area (respecting the margin)
        int roomX = area.x + randRange(1, area.w - roomW - 1);
        int roomY = area.y + randRange(1, area.h - roomH - 1);

        // set all the tiles of the room to FLOOR
        for (int y = roomY; y < roomY + roomH; y++)
            for (int x = roomX; x < roomX + roomW; x++)
                dungeon.set(x, y, FLOOR);

        // remember the room: props, lights and the spawn point are placed per room later
        Rect room = { roomX, roomY, roomW, roomH };
        dungeon.rooms.push_back(room);
        return room;
    }

    // Connect two rooms with an L-shaped corridor between their centers.
    void connect(Rect a, Rect b) {
        int ax = a.x + a.w / 2;
        int ay = a.y + a.h / 2;
        int bx = b.x + b.w / 2;
        int by = b.y + b.h / 2;

        // randomly go horizontal-then-vertical or vertical-then-horizontal, so corridors are
        // not all bent the same way
        if (randRange(0, 1) == 0) {
            carveHorizontalTunnel(ax, bx, ay);
            carveVerticalTunnel(ay, by, bx);
        }
        else {
            carveVerticalTunnel(ay, by, ax);
            carveHorizontalTunnel(ax, bx, by);
        }
    }

    // Dig a horizontal 1-tile-wide corridor on row y, from column x1 to column x2.
    void carveHorizontalTunnel(int x1, int x2, int y) {
        int from = std::min(x1, x2);
        int to   = std::max(x1, x2);
        for (int x = from; x <= to; x++)
            dungeon.set(x, y, FLOOR);
    }

    // Dig a vertical 1-tile-wide corridor on column x, from row y1 to row y2.
    void carveVerticalTunnel(int y1, int y2, int x) {
        int from = std::min(y1, y2);
        int to   = std::max(y1, y2);
        for (int y = from; y <= to; y++)
            dungeon.set(x, y, FLOOR);
    }

    // Return a random integer between lo and hi, both included.
    int randRange(int lo, int hi) {
        if (lo >= hi)
            return lo;   // safety, in case the range is empty
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(rng);
    }

    int width;
    int height;
    std::mt19937 rng;   // Mersenne Twister, seeded once so the whole generation is repeatable
    Dungeon dungeon;    // the map we are building

    // tuning parameters of the generation
    int minLeafSize = 8;   // both halves of a split must be at least this big
    int maxDepth = 4;   // max recursion depth, so at most 2^4 = 16 rooms
};

// Print the dungeon to the console as ASCII art: '#' for walls, '.' for floor.
// Handy to check the generation logic without any graphics.
inline void printDungeon(const Dungeon& d) {
    for (int y = 0; y < d.height; y++) {
        for (int x = 0; x < d.width; x++)
            std::cout << (d.get(x, y) == WALL ? '#' : '.');
        std::cout << '\n';
    }
}

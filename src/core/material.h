#pragma once

// The KIND of surface of an object, used by the game logic: the collisions read it to know what
// blocks the player, and the renderer to know what to skip.
enum MaterialId {
    MAT_FLOOR,
    MAT_WALL,
    MAT_CEILING,
    MAT_PROP,   // solid props that block the player: columns, statues, barrels, ...
    MAT_CHAIN,  // hanging chains: dynamic, the player walks through them and swings them
    MAT_DECOR   // torches and braziers: drawn, but they do not block and do not cast shadows
};

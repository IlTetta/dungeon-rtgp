#pragma once

// SHARED FILE: da cambiare insieme all'inizio di ogni milestone

// The "kind" of surface an object has.
// IO sets this value when I builds the scene,
// YOU decides how to actually render each kind (which shader / uniforms).
// We use a plain enum and put a "MAT_" prefix in front of every name, because a plain
// enum puts its names directly in the surrounding scope, so without a prefix a name
// like FLOOR could easily clash with something else.
enum MaterialId {
    MAT_FLOOR,
    MAT_WALL,
    MAT_CEILING,
    MAT_PROP,   // solid props (blocks the player): columns, statues, barrels, ...
    MAT_CHAIN,  // dynamic, non-blocking decor (chains): the player walks through and swings them
    MAT_DECOR   // static, non-blocking decor (wall torches, ...): drawn, but never blocks the player
};

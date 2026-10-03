# Procedural Dungeon Walkthrough - RTGP

Real-Time Graphics Programming project (a.a. 2025/2026).
Interactive first-person walkthrough of a procedurally generated dungeon, lit by torches and
braziers, with GGX shading, shadow mapping, SSAO and volumetric fog. The focus of the project is
to **measure the impact of CPU/GPU optimizations** with a reproducible benchmark: frustum culling,
instancing, number of shadow-casting lights and fog quality.

**Team:** Andrea Tettamanti · Lorenzo Vernavà
**Stack:** C++17 / OpenGL 4.1 (core) / GLSL · Windows (MSVC x64)
**Libraries:** GLFW, GLAD, GLM, Dear ImGui, stb_image. Models are loaded with our own OBJ loader
(no Assimp) and the physics is ours too (no Bullet).

## Features

**World and interaction**
- Procedural **BSP dungeon** (rooms + corridors, always connected) from a seed, turned into 3D
  geometry: floor, wall and ceiling slabs sharing one unit cube. Any seed can be regenerated live.
- **First-person camera** with ad-hoc **collisions** (player cylinder against the AABBs).
- **Props** (columns, statues, altar, barrels, crates, urns, rubble, torches, braziers) loaded
  with our **OBJ loader** + **stb_image**, placed per room without overlaps, with a per-object
  **material system**.
- **Verlet chains** that swing when the player walks through them.
- **Instanced fire particles** (one draw call for thousands of quads).

**Rendering**
- **GGX (Cook-Torrance) forward shading**; every light is a fire: wall torches (SPOT) and
  braziers (POINT), the nearest 32 shaded each frame.
- **Shadow mapping**: 2D maps with PCF for the torches, distance cubemaps (one pass with a
  geometry shader) for the braziers; the nearest 8 SPOT + 2 POINT lights cast shadows, with a
  fade instead of popping.
- **SSAO** (G-buffer, hemisphere kernel, blur) and **volumetric fog** (per-pixel ray marching).

**Optimizations and measurement**
- **Frustum culling** (Gribb-Hartmann planes, AABB positive-vertex test), with a spectator camera
  that shows the culled volume.
- **Structural instancing** (floor/wall/ceiling slabs in at most 3 instanced draw calls) and
  **particle instancing**, both with an A/B switch.
- **Benchmark harness**: record a camera path, replay it (time-based, so every configuration
  covers the same journey) and log every frame to CSV. Automated sweeps: culling (x SSAO),
  particle instancing, structural instancing, shadow-light budget, fog steps.
- **Dear ImGui HUD** with metrics, a frame time graph and all the settings.

## Controls
WASD move · mouse look · Shift sprint · **C** toggle culling · **V** spectator camera ·
**F1** free the cursor (use the HUD) · ESC quit.

## Build
See [SETUP.md](SETUP.md). Real measurements need a **Release** build with **VSync off**
(checkbox in the Benchmark window). The reference camera path is in
`benchmarks/bench_path_seed12345.txt`; the CSVs are written to `benchmarks/` next to the
executable and analysed with `analysis/analyze_benchmarks.m` (MATLAB, no toolboxes).

## Attribution
The statue is a museum photoscan (*Mourner from the Tomb of Philip the Bold*, Scan the World),
licensed **CC-BY**: credited here and in the project report. Surface textures from ambientCG
(CC0).

## Branches
- `main`: the complete project, always buildable (tags `M2`, `M3` at the milestones).
- `feat/andrea`: world, geometry, tooling, benchmark (BSP dungeon, culling, HUD, instancing).
- `feat/lorenzo`: rendering, lighting, effects (GGX, shadow mapping, SSAO, volumetric fog).

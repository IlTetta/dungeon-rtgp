# Procedural Dungeon Walkthrough - RTGP

Real-Time Graphics Programming project (a.a. 2025/2026).
Interactive first-person walkthrough of a procedurally generated dungeon, with dynamic
point lights, GGX shading, shadow mapping and volumetric fog. The focus of the project is
to **measure the impact of CPU/GPU optimizations** (frustum culling, fog quality, light count).

**Team:** Andrea Tettamanti · Lorenzo Vernavà
**Stack:** C++ / OpenGL 4.1 (core) / GLSL · Windows
**Libraries:** GLFW, GLAD, GLM, Dear ImGui, stb_image (models are loaded with our own OBJ loader,
so Assimp is not used)

## Implemented so far
- Procedural **BSP dungeon** generation (rooms + corridors), turned into 3D geometry.
- **GGX (Cook-Torrance) forward rendering** with several dynamic point lights (torches/braziers).
- **Textured surfaces** (albedo) for floor / wall / ceiling and a per-object **material system**.
- A set of **3D props** (columns, statues, altar, barrels, crates, urns, rubble, chains, braziers)
  loaded from `assets/props/` with our own **OBJ loader** + **stb_image** texture loading.
- **Frustum culling** (the measurable optimization) + a **Dear ImGui performance HUD**.
- Ad-hoc player **collisions** and **Verlet chain physics** (swinging chains, no physics engine).

## Controls
WASD move · mouse look · Shift sprint · **C** toggle culling · **F1** free the cursor (use/move
the HUD) · ESC quit.

## Attribution
The statue is a museum photoscan (*Mourner from the Tomb of Philip the Bold*, Scan the World),
licensed **CC-BY** — credited here and in the project report.

## Branch workflow
- `main`: always buildable; merge here only at the end of each milestone (tags `M1`…`M4`).
- `feat/andrea`: scene, geometry, tooling (BSP dungeon, culling, HUD, benchmark).
- `feat/lorenzo`: rendering, lighting, effects (GGX, shadow mapping, volumetric fog).

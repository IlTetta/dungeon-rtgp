# Procedural Dungeon Walkthrough - RTGP

Real-Time Graphics Programming project (a.a. 2025/2026).
Interactive first-person walkthrough of a procedurally generated dungeon, with dynamic
point lights, GGX shading, shadow mapping and volumetric fog. The focus of the project is
to **measure the impact of CPU/GPU optimizations** (frustum culling, fog quality, light count).

**Team:** Andrea Tettamanti · Lorenzo Vernavà
**Stack:** C++ / OpenGL 4.1 (core) / GLSL · Windows
**Libraries:** GLFW, GLAD, GLM, Assimp, Dear ImGui

## Branch workflow
- `main`: always buildable; merge here only at the end of each milestone (tags `M1`…`M4`).
- `feat/andrea`: scene, geometry, tooling (BSP dungeon, culling, HUD, benchmark).
- `feat/lorenzo`: rendering, lighting, effects (GGX, shadow mapping, volumetric fog).


# Dungeon Prop Set — asset list & placement guide

10 props for the OpenGL 4.1 engine. Each lives in `assets/props/<name>/` as:
`<name>.obj` + `<name>_albedo.png` (+ `_normal.png` / `_roughness.png` on the same UVs).

**Global rules:** 1 Blender unit = 1 world unit · Y up · −Z forward · triangulated · one texture per prop · no `.mtl`.

## Prop list

| Prop | Material | Tris | Size W×D×H (u) | Tex | Pivot | Where / how to place |
|------|----------|-----:|----------------|-----|-------|----------------------|
| column  | stone         | 364   | 0.78×0.78×3.00 | 1024 | floor   | Along walls / in rows; 3u tall = floor-to-ceiling. Random 90° yaw. |
| crate   | wood          | 572   | 0.90×0.90×0.85 | 1024 | floor   | Storage rooms, corners, against walls. Stackable. Random yaw. |
| barrel  | wood + iron   | 304   | 0.72×0.70×1.00 | 1024 | floor   | Clusters of 2–3 with crates. Random yaw. |
| urn     | terracotta    | 380   | 0.70×0.70×0.90 | 1024 | floor   | Alcoves, along walls, temple rooms. Small clusters. |
| brazier | iron + embers | 372   | 0.73×0.73×0.75 | 1024 | floor   | Room centers / flanking the altar. **Light source** (add point light + flame). |
| torch   | iron          | 844   | 0.23×0.50×0.79 | 1024 | **wall** | Corridor & room walls at 4–6u intervals, height ~1.9u. **Light source**. |
| chain   | iron          | 1536  | 0.11×0.11×0.97 | 1024 | **hanging** | From ceilings / manacles. Scale & repeat for length. Pivot at lower end. |
| rubble  | stone         | 200   | 1.18×1.14×0.50 | 1024 | floor   | Near broken walls & corners. Scatter with random yaw + scale (0.7–1.4). |
| altar   | stone         | 440   | 1.30×0.92×1.09 | 1024 | floor   | Special / boss room centerpiece. Center or against the far wall. |
| statue  | marble (scan) | ~85k  | 0.62×0.51×1.75 | 2048 | floor   | **Unique showpiece — one per level.** Flank the altar / in a niche, facing the entrance. |

## Pivots (where the origin sits)

- **floor** — origin at base center. Place origin on the floor point; any yaw works.
- **wall** (torch) — origin at the wall-contact (back-plate base). Put origin on the wall, rotate so the back faces into the wall (align to the wall normal), mount at ~1.9u.
- **hanging** (chain) — vertical segment, origin at the *lower* end. To hang it, offset so the top meets the ceiling anchor (or rotate 180°).

## Engine notes

- Load OBJ, bind `_albedo` as base color now; add `_normal` (OpenGL +Y convention) and `_roughness` on the same UVs when your PBR path is ready.
- If textures look upside-down → enable vertical flip in the loader (`stbi_set_flip_vertically_on_load(true)`).
- **Lights:** brazier & torch are light sources — spawn a warm point light (~1800K, orange) at the fire + an animated flame billboard/particle (torch: at the crown basket; brazier: above the coals). The baked amber embers do the rest.
- **Colliders:** use a simple box / cylinder / capsule sized to the bounding box — never the raw mesh, especially the 85k statue.
- **Attribution:** the statue is a museum photoscan (*Mourner from the Tomb of Philip the Bold*, Scan the World) — almost certainly **CC-BY**, so credit the source in your report/credits.

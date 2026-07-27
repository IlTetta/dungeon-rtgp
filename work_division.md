# Divisione del lavoro — RTGP Project

Progetto: *Procedural Dungeon Walkthrough* (RTGP, a.a. 2025/2026).
Team: **Andrea Tettamanti** e **Lorenzo Vernavà**.

L'obiettivo di questo documento è dividere il lavoro in **due tracce parallele di difficoltà equivalente**, sviluppate su due branch separati e integrate su `main` a ogni milestone.

---

## 1. Criterio della divisione

Divisione **verticale per dominio**, non per milestone: ognuno è "proprietario" di alcune cartelle/file, così i due branch toccano codice diverso e i merge restano puliti.

- **Andrea → Scena, geometria e strumenti (lato CPU).**
- **Lorenzo → Rendering, illuminazione ed effetti (lato GPU/shader).**

I due pezzi "difficili e vetrina" sono distribuiti uno per parte per bilanciare:
- **Frustum culling** (l'ottimizzazione misurabile, cuore del progetto) → Andrea.
- **Fog volumetrica via ray marching** (extra avanzato) → Lorenzo.

---

## 2. Strategia dei branch

- `main` → sempre in stato funzionante. Ci si merga **solo alla fine di ogni milestone**.
- `feat/andrea` → branch di lavoro di Andrea.
- `feat/lorenzo` → branch di lavoro di Lorenzo.

Flusso a ogni milestone:
1. All'inizio della milestone entrambi partono aggiornati da `main` (`git switch feat/... ; git merge main`).
2. Si lavora ognuno sul proprio branch, commit frequenti.
3. A fine milestone: prima merga **Andrea** su `main` (di solito porta la scena/le strutture), poi Lorenzo aggiorna il suo branch da `main`, risolve eventuali conflitti e merga a sua volta.
4. Si tagga il commit di milestone: `git tag M1`, `git tag M2`, ...

> Regola pratica: chi tocca per primo un file "di confine" (vedi §3) avvisa l'altro. Se possibile si usa una PR anche tra di noi, così il merge è tracciato.

---

## 3. Contratti condivisi (da concordare a inizio M1, poi "congelati")

Questi sono i punti in cui i due mondi si incontrano: vanno definiti insieme **prima** di iniziare, per non pestarsi i piedi.

- **`Scene`**: struttura dati con la lista dei renderable. Ogni oggetto espone: `mesh handle`, `model matrix`, `AABB`, `materialId`. Andrea la popola (dungeon), Lorenzo la consuma (rendering).
- **Elenco luci**: array di point light (posizione, colore, intensità, raggio). Andrea fornisce le posizioni delle torce dal dungeon; Lorenzo le anima e le usa nello shading.
- **`FrameMetrics`**: struct con i contatori (fps, frame time, draw call, triangoli, oggetti cullati, #luci, #step fog). Andrea la mostra nell'HUD; Lorenzo scrive i campi lato rendering (draw call, triangoli, step fog).
- **Main loop / renderer entry point**: unico file "caldo". Definiamo subito l'ordine delle fasi (update scena → culling → shadow pass → main pass → fog pass → HUD) così ognuno riempie la propria fase senza riscrivere il loop.

File di proprietà (indicativo):
- **Andrea**: `dungeon/`, `culling/`, `hud/`, `collision/`, `benchmark/`.
- **Lorenzo**: `shaders/` (GLSL), `lighting/`, `shadows/`, `fog/`, `particles/`, `materials/`.
- **Condivisi (modifiche concordate)**: `main.cpp` / render loop, `Scene`, `FrameMetrics`.

---

## 4. Responsabilità per persona

### Andrea — Scena, geometria, strumenti
1. **Generazione procedurale del dungeon (BSP)**: partizione, stanze + corridoi, mesh/griglia risultante, posizioni delle torce.
2. **Rappresentazione della scena**: lista oggetti con transform + AABB + material id.
3. **Camera FPS + collisioni ad-hoc** (sfera/AABB vs muri, niente Bullet).
4. **Frustum culling**: estrazione dei piani dalla view-projection, test AABB vs frustum. *(ottimizzazione misurabile)*
5. **HUD ImGui + raccolta metriche** + **benchmark harness**: percorso camera fisso e ripetibile, logging su CSV per gli esperimenti.
6. **Particelle** (instancing) per le scintille delle torce *(nice-to-have, M3)*.

### Lorenzo — Rendering, illuminazione, effetti
1. **Shading GGX PBR** delle superfici del dungeon.
2. **Texturing / normal mapping** su muri e pavimenti.
3. **Shadow mapping (PCF)** per le point light dinamiche (depth map / cubemap per luce).
4. **Point light dinamiche multiple**: animazione torce, attenuazione, gestione del set di luci.
5. **Fog volumetrica via ray marching** (pass fragment full-screen). *(extra avanzato + carico GPU controllabile)*
6. Supporto ai campi rendering di `FrameMetrics` (draw call, triangoli, #step fog).

---

## 5. Milestone (con carico bilanciato a ogni tappa)

| Milestone | Andrea | Lorenzo | Merge → `main` (Definition of Done) |
|---|---|---|---|
| **M1 — base render + dungeon** | BSP dungeon + `Scene` + camera FPS + collisioni ad-hoc | Renderer forward di base, caricamento mesh, shader semplice + prima GGX, scaffolding FBO | Si cammina in un dungeon illuminato in modo base |
| **M2 — lighting + shadows + culling + HUD** | Frustum culling + HUD ImGui + metriche + percorso benchmark | Shadow mapping (PCF) + point light dinamiche (torce) | Dungeon con torce, ombre, culling attivo e HUD con i numeri |
| **M3 — extra avanzato** | Benchmark harness completo (CSV, automazione esperimenti) + particelle instancing | **Fog volumetrica ray marching** | Effetto volumetrico attivo + esperimenti automatizzabili |
| **M4 — analisi + doc + video** | Esperimenti *culling ON/OFF* e *fog steps vs fps* + grafici + relative sezioni doc | Esperimento *scaling #luci* + sezioni doc su shading/ombre/fog | Documento + video completi |

---

## 6. Bilanciamento (perché le parti sono equivalenti)

- **Andrea** ha più ampiezza CPU + algoritmica (BSP, culling matematico, collisioni) e tutta la parte di **strumenti/misura** (HUD + benchmark harness), che è consistente e centrale per la tesi del progetto.
- **Lorenzo** ha più profondità GPU/shader (GGX, shadow mapping su più luci — che con le cubemap è impegnativo — e la **fog ray-marched**, il pezzo più pesante).
- I due "big ticket" (culling ↔ fog) e le due parti "impegnative ma note dal corso" (HUD/benchmark ↔ shadow mapping) sono incrociati, così nessuno dei due branch è nettamente più difficile.

---

## 7. Checklist di merge (a fine milestone)

- [ ] Il proprio branch compila e gira da solo.
- [ ] `git merge main` fatto sul proprio branch e conflitti risolti **prima** di mergiare su `main`.
- [ ] Nessun contratto condiviso (§3) modificato senza averlo detto all'altro.
- [ ] `main` compila e gira dopo il merge di entrambi.
- [ ] Tag della milestone creato (`git tag M#`).

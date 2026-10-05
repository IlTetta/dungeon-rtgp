# SETUP: come compilare ed eseguire il progetto

Guida per compilare `dungeon-rtgp` su Windows.

Le librerie (GLFW, GLAD, GLM, Dear ImGui, stb_image) e gli asset sono **già nel repo**
(`external/`, `libs/win/`, `assets/`): basta clonare e compilare, senza scaricare nulla.

---

## 1. Prerequisiti

- **Visual Studio 2022** con il workload **"Sviluppo di applicazioni desktop con C++"**.
  Per installarlo: **Visual Studio Installer** → "Visual Studio Community 2022" → **Modifica** →
  spunta **"Sviluppo di applicazioni desktop con C++"** → Installa.
  (Dà il toolset **v143**, lo stesso con cui è compilata la `glfw3.lib` del laboratorio.)
- **Git**.
- CMake è incluso in Visual Studio.
- Una GPU con **OpenGL 4.1**.

---

## 2. Clona il repo

```bash
git clone <URL-del-repo> dungeon-rtgp
cd dungeon-rtgp
```

Il branch `main` contiene il progetto completo.

---

## 3. Compila ed esegui

### Opzione A: dentro Visual Studio 2022 (consigliata)

1. **File → Apri → Cartella…** → scegli la cartella `dungeon-rtgp`.
2. VS legge il `CMakeLists.txt` e configura da solo (esito nel pannello "CMake").
3. Nella barra in alto scegli la configurazione e come elemento di avvio **`dungeon.exe`**:
   - **x64-Debug** per sviluppare;
   - **x64-Release** (RelWithDebInfo) per **misurare**: il Debug di MSVC è molto più lento e i
     numeri non sono rappresentativi.

   Le due configurazioni sono definite in `CMakeSettings.json`.
4. **Ctrl+F5** (esegui senza debug).

> Aprendo una cartella CMake, l'Esplora soluzioni mostra la "CMake Targets View". Per vedere i
> file: **Visualizza** → **Esplora soluzioni**.

### Opzione B: da terminale

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

```bash
cmake --build build --config Release
```

L'eseguibile finisce in `build/Release/dungeon.exe` (o `build/Debug/` con `--config Debug`).

### Risultato atteso

Si apre la finestra "Dungeon RTGP" in prima persona dentro il dungeon del seed 12345, con le
finestre dell'HUD. **F1** libera il cursore per usare l'HUD, **ESC** chiude.

### Il programma di test `bsp_test`

Un secondo eseguibile da console che genera il dungeon e lo stampa in ASCII, senza finestra:
`bsp_test.exe` (opzionale: `bsp_test.exe <seed>`). In Visual Studio sceglilo come elemento di avvio.

---

## 4. Dipendenze e asset

- **GLFW, GLAD, GLM** → `external/` + `libs/win/glfw3.lib`.
- **Dear ImGui** (HUD) → sorgenti in `external/imgui/`, compilati insieme al progetto.
- **stb_image** (caricamento texture) → `external/stb/`.
- **Prop** (`.obj` + texture) in `assets/props/`, texture delle superfici in `assets/textures/`.
  Dopo ogni build `shaders/` e `assets/` vengono **copiate accanto all'eseguibile**, perché il
  programma li carica con percorsi relativi.
- **Assimp**: non usato, i modelli li carica il nostro loader OBJ (`src/engine/obj_loader.h`).
- **Bullet**: non usato, collisioni e catene (Verlet) sono scritte da noi.

---

## 5. Struttura del codice

| Cartella | Contenuto | Chi |
|---|---|---|
| `src/core/` | strutture condivise: `Scene`, `Material`, `FrameMetrics` | entrambi |
| `src/engine/` | `Shader`, `Mesh`, `Camera`, loader OBJ, texture | entrambi |
| `src/dungeon/` | generatore BSP + `bsp_test` | Andrea |
| `src/world/` | geometria 3D, prop, catene, collisioni, culling, particelle, debug draw | Andrea |
| `src/bench/` | percorso camera, benchmark, esperimenti | Andrea |
| `src/hud/`, `src/input/` | HUD ImGui, input | Andrea |
| `src/render/` | renderer, ombre, SSAO, fog, instancing strutturale | Lorenzo (instancer: Andrea) |
| `shaders/` | GLSL | Lorenzo (particelle: Andrea) |
| `src/main.cpp` | finestra e loop | entrambi |

La scena la costruisce il codice di `world/` e il renderer la legge soltanto (`const Scene&`).

---

## 6. Misurare (benchmark)

1. Build **x64-Release**, e nella finestra **Benchmark** togli la spunta a **VSync**.
2. **Load** il percorso di riferimento (`benchmarks/bench_path_seed12345.txt`, copialo accanto
   all'eseguibile, nella cartella `benchmarks/` della build) oppure registrane uno con **Record**.
3. Lancia uno degli sweep ("Run experiments", "Run fog sweep", ...). Ogni configurazione scrive un
   CSV in `benchmarks/` accanto all'eseguibile; rilanciando lo stesso sweep i file diventano
   `_run2`, `_run3`, ... invece di sovrascriversi.
4. Analisi con `analysis/analyze_benchmarks.m` (MATLAB, senza toolbox).

---

## 7. Se qualcosa non va

- **CMake: "could not find any instance of Visual Studio"** → manca il workload C++.
- **Errori di link su GLFW / `LNK2019`** → stai compilando a 32 bit o con MinGW: serve
  **MSVC x64** (la libreria è vc143 x64).
- **`LNK4098` runtime mismatch** → già gestito nel `CMakeLists.txt` (runtime statico `/MT`, come
  la `glfw3.lib` del lab). Non cambiarlo.
- **Hai modificato solo un `.h` e il programma non cambia** → con il percorso della cartella che
  contiene caratteri non ASCII (`à`, `°`) Ninja non legge le dipendenze degli header, quindi non
  ricompila i `.cpp` che li includono. Usa **Compila → Ricompila tutto** (o tocca un `.cpp`).
- **Schermo nero o "shader file not read"** → l'eseguibile non trova `shaders/`: lancialo dalla
  sua cartella (la build copia lì shader e asset).

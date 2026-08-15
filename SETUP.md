# SETUP — come far partire il progetto

Guida per mettere in piedi e compilare `dungeon-rtgp` su Windows.

Le librerie (GLFW, GLAD, GLM) sono **già dentro il repo** (`external/` + `libs/win/`), quindi
NON devi scaricare né configurare niente a mano: basta clonare e compilare.

---

## 1. Prerequisiti

- **Visual Studio 2022** con il workload **"Sviluppo di applicazioni desktop con C++"**.
  Per installarlo: apri **Visual Studio Installer** → su "Visual Studio Community 2022" →
  **Modifica** → spunta **"Sviluppo di applicazioni desktop con C++"** → Installa.
  (Questo dà il toolset **v143**, lo stesso con cui sono compilate le librerie del lab.)
- **Git**.
- CMake è incluso in Visual Studio, non serve installarlo a parte.

---

## 2. Clona il repo e mettiti sul tuo branch

```bash
git clone <URL-del-repo> dungeon-rtgp
cd dungeon-rtgp
git switch feat/lorenzo
```

(Se il branch `feat/lorenzo` non compare, fai prima `git fetch` e poi lo `switch`.)

---

## 3. Compila ed esegui

### Opzione A — dentro Visual Studio 2022 (consigliata)

1. Apri **Visual Studio 2022** → **File → Apri → Cartella…** → scegli la cartella `dungeon-rtgp`.
2. VS legge il `CMakeLists.txt` e **configura da solo** (guarda il pannello "CMake" per l'esito).
3. Nella barra in alto, verifica che la configurazione sia **x64-Debug** e che l'elemento di
   avvio sia **`dungeon.exe`**.
4. Premi **Ctrl+F5** (esegui senza debug).

> Nota: aprendo una cartella CMake, l'Esplora soluzioni mostra la "CMake Targets View" (vedi
> solo il target `dungeon`, non i file). Per vedere i file **Visualizza** -> **Esplora soluzioni**.

### Opzione B — da terminale

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

L'eseguibile finisce in `build/Debug/dungeon.exe`.

### Risultato atteso

Una **finestra grigio-scuro** dal titolo "Dungeon RTGP". Si chiude con **ESC**.
Se la vedi, la toolchain e le librerie funzionano.

---

## 4. Dipendenze e asset (già nel repo)

È tutto già committato: cloni e compili, non devi procurarti nulla.
- **Dear ImGui** (HUD prestazioni) → sorgenti in `external/imgui/`, compilati col progetto.
- **stb_image** (caricamento texture) → header in `external/stb/`.
- **Modelli e texture** dei prop in `assets/props/`, texture delle superfici in `assets/textures/`
  (vengono copiate accanto all'eseguibile a ogni build, come `shaders/`).
- **Assimp**: NON usato — i modelli `.obj` li carica un nostro loader (niente libreria da 500 MB).
- **Bullet**: NON usato — collisioni ad-hoc e catene con fisica **Verlet** scritta da noi.

---

## 5. La tua parte (Lorenzo)

Tu lavori in `src/render/` (la classe `Renderer`) e in `shaders/` (il GLSL). Non toccare le
cartelle `src/world/`, `src/hud/`, `src/dungeon/`.

I file **condivisi** che usi ma NON modifichi (se non insieme, a inizio milestone) sono in
`src/core/`:
- `scene.h` → la scena da disegnare (la riempio io, tu la LEGGI, sempre per `const&`).
- `metrics.h` → i contatori dell'HUD; tu scrivi solo `drawCalls` e `trianglesDrawn`.
- `material.h` → l'id del materiale (FLOOR/WALL/PROP), che ti dice come renderizzare ogni oggetto.

Le utility comuni sono in `src/engine/` (`shader.h`, `mesh.h`, `camera.h`): sono già pronte,
riscritte da me (non copiate dal lab). Per M1 ti servono soprattutto `shader.h` e `mesh.h`.
Per gli shader puoi partire dal GGX del lab (`lecture05`).

---

## 6. Flusso git (promemoria)

- Lavora sempre su **`feat/lorenzo`**, con commit frequenti.
- A **inizio milestone**: `git switch feat/lorenzo` poi `git merge main` (per allinearti).
- A **fine milestone**: si merga su `main` insieme e si tagga (`M1`, `M2`, ...).
- I file condivisi (`src/core/`, `src/engine/`, `main.cpp`, `CMakeLists.txt`) si toccano solo
  insieme, a inizio milestone → così i merge restano puliti.

---

## 7. Se qualcosa non va

- **CMake: "could not find any instance of Visual Studio"** → manca il workload C++.
- **Errori di link su GLFW / `LNK2019`** → stai compilando a 32 bit o con MinGW; deve essere
  **MSVC x64** (le librerie sono vc143 x64).
- **`LNK4098` runtime mismatch** → è già gestito nel `CMakeLists.txt` (usiamo il runtime
  statico `/MT` per combaciare con la `glfw3.lib` del lab). Non cambiarlo.

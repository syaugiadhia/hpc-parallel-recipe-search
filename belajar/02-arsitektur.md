# 02 — Arsitektur Codebase

> **Prasyarat baca:** [01-pengenalan-dan-domain.md](01-pengenalan-dan-domain.md).
> **Setelah ini kamu paham:** bagaimana folder & file diatur, prinsip "satu engine,
> banyak mode", urutan langkah yang dijalankan tiap program, dan bagaimana proyek
> di-build.

---

## 1. Intuisi: satu mesin, empat setir

Bayangkan sebuah **mesin pencari resep** yang sangat pintar. Mesin ini ditulis **sekali**.
Lalu kita pasang mesin yang sama ke **empat kendaraan berbeda**:

- 🚶 **Serial** — jalan kaki sendirian (1 pekerja).
- 🚗 **OpenMP** — mobil dengan beberapa penumpang yang bantu mendorong (banyak thread, 1 mesin).
- 🚚 **MPI** — konvoi truk di banyak lokasi (banyak proses, bisa banyak komputer).
- 🚛 **Hybrid** — konvoi truk yang tiap truknya juga penuh penumpang pendorong.

Karena mesinnya **sama persis**, hasil pencariannya selalu identik. Yang beda hanya
**bagaimana kerjanya dibagi**. Ini keputusan arsitektur paling penting di proyek ini:
**logika pencarian tidak diduplikasi** — semua mode memanggil pustaka (library) yang sama.

---

## 2. Struktur direktori

```
tubes-2/
├── CMakeLists.txt          # build system (C++17; OpenMP/MPI opsional)
├── README.md               # dokumentasi pemakaian
├── tutor_multi_pc.md       # panduan MPI multi-komputer
├── data/
│   ├── recipes.json        # data resep (720 elemen)
│   └── tiers.json          # katalog tier (starter, special, tier1..15)
│
├── src/
│   ├── common/             # ★ ENGINE INTI — dipakai SEMUA mode
│   │   ├── RecipeGraph.*    # graf resep + interning id
│   │   ├── RecipeTree.*     # pohon resep + signature + json/ascii
│   │   ├── JsonLoader.*     # parser data resep (3 format)
│   │   ├── TierCatalog.*    # katalog tier + penanda terminal
│   │   ├── Search.*         # ★ SearchEngine: BFS lazy, DFS, memo, OpenMP
│   │   ├── Statistics.*     # metrik (SearchStats) + serialisasi JSON
│   │   ├── Visualizer.*     # output JSON / DOT / PNG-SVG
│   │   └── Cli.*            # parse argumen → AppOptions
│   │
│   ├── serial/main_serial.cpp     # executable alchemy_serial
│   ├── openmp/main_openmp.cpp     # executable alchemy_openmp
│   └── mpi/
│       ├── main_mpi.cpp           # executable alchemy_mpi (MPI_Init, routing rank)
│       ├── MpiMaster.*            # rank 0: buat task, bagi, gabung hasil
│       └── MpiWorker.*            # rank >0: minta task, hitung, kirim hasil
│
├── gui/alchemy_gui.py      # GUI Python (orkestrator, master/slave LAN)
├── tests/test_search.cpp   # unit test (ctest)
├── benchmarks/targets.txt  # daftar target untuk benchmark batch
├── scripts/                # script .sh/.ps1/.py (run, benchmark, scan host)
├── commands/               # batch .bat Windows untuk demo multi-PC
├── results/                # output hasil run (JSON/DOT/PNG/CSV)
└── paper/                  # laporan IEEE + PENJELASAN_CODEBASE.md
```

> **Istilah — executable:** file program jadi yang bisa langsung dijalankan (mis.
> `alchemy_serial.exe`). Tiga di antaranya (serial/openmp/mpi) adalah pintu masuk
> berbeda ke engine yang sama.
>
> **Istilah — library / pustaka:** kumpulan kode yang tidak berjalan sendiri tapi
> "ditempel" (di-*link*) ke program lain. Di sini `src/common/` dikompilasi jadi library
> `alchemy_common`, lalu tiap executable menempelkannya.

---

## 3. Prinsip arsitektur: "1 engine, banyak mode"

Diagram layer (lapisan):

```
┌──────────────────────────────────────────────────────────────┐
│  GUI Python (gui/alchemy_gui.py)  ← opsional, hanya pembungkus │
│  Memanggil executable di bawah lewat command line.            │
└──────────────────────────────────────────────────────────────┘
        │ menjalankan
        ▼
┌────────────┐ ┌────────────┐ ┌──────────────────────────────┐
│  serial    │ │  openmp    │ │  mpi (master/worker, hybrid)  │  ← LAPISAN MODE
│ main_*.cpp │ │ main_*.cpp │ │ main_mpi + MpiMaster/MpiWorker│
└────────────┘ └────────────┘ └──────────────────────────────┘
        │            │                     │
        └────────────┴─────────────────────┘
                     │ semua memanggil
                     ▼
        ┌─────────────────────────────────┐
        │   alchemy_common (ENGINE INTI)   │   ← LAPISAN LOGIKA
        │   Search, RecipeGraph, RecipeTree │
        │   JsonLoader, TierCatalog, ...    │
        └─────────────────────────────────┘
```

**Konsekuensi penting:** karena keempat mode memakai engine yang sama,
**kebenaran satu = kebenaran semua**. Unit test (`tests/test_search.cpp`) cukup menguji
engine `common`; tidak perlu menguji ulang tiap mode. Mode hanya bertanggung jawab atas
*pembagian kerja*, bukan *cara mencari*.

---

## 4. Pipeline eksekusi umum (6 langkah)

Setiap executable — apa pun modenya — mengikuti alur yang sama. Mari telusuri pakai
contoh `main_serial.cpp`:

```
1. Parse argumen CLI
   alchemy::parseArgs(argc, argv, mpiMode)  →  AppOptions
   (AppOptions = semua pilihan: target, algoritma, mode, limit, threads, dll.)

2. Muat data resep
   JsonLoader::loadFromFile(options.dataPath)  →  RecipeGraph

3. Muat & validasi katalog tier
   TierCatalog::loadFromFile(options.tiersPath)
   tiers.validateAgainstGraph(graph)   ← strict utk --list-elements, warning utk search
   tiers.applyTerminals(graph)          ← tandai starter + special (Time) sbg terminal

4. JALANKAN — salah satu dari:
   - list elements   (bantu cari nama target)
   - benchmark batch (banyak target → CSV)
   - search tunggal  (SearchEngine.search(target))   ◄── INTI, beda tiap mode

5. Tulis output
   Visualizer::writeOutputs(...)  →  JSON (selalu), DOT+PNG (kalau --render full)

6. Cetak ringkasan ke layar
   target, statistik, dan pohon ASCII tiap resep
```

Bandingkan ketiga `main`:

| Langkah | `main_serial.cpp` | `main_openmp.cpp` | `main_mpi.cpp` |
|---|---|---|---|
| Parse arg | `parseArgs(.., false)` | `parseArgs(.., false)` | `parseArgs(.., true)` ← flag MPI aktif |
| Setup paralel | — | `configureOpenmp` (set thread) | `MPI_Init`, `configureRankThreads` |
| Langkah 4 | `engine.search()` | `engine.search()` (engine ber-OpenMP) | `runMpiMaster` / `runMpiWorker` |

Perhatikan: serial & openmp `main`-nya hampir **kembar** — bedanya cuma openmp memanggil
`configureOpenmp()` untuk menyalakan thread. MPI berbeda karena harus mengurus banyak
proses (lihat [09-mpi.md](09-mpi.md)).

> **Catatan — `parseArgs(.., true)`:** argumen ketiga `mpiMode` membuat beberapa flag
> (`--split-depth`, `--thread-profile`, `--baseline-ms`) **hanya** valid di MPI. Kalau
> kamu kasih flag itu ke serial/openmp, akan ditolak. Lihat [Cli.hpp](src/common/Cli.hpp).

---

## 5. Build system (CMakeLists.txt)

> **Istilah — CMake:** alat yang membaca `CMakeLists.txt` lalu menghasilkan perintah
> kompilasi yang sesuai untuk komputermu (Windows/Linux, compiler apa pun). Kamu tidak
> menulis perintah compiler manual; CMake mengaturnya.

Poin penting dari [CMakeLists.txt](CMakeLists.txt):

- **C++17** wajib.
- **nlohmann/json** wajib (parser JSON). Kalau tidak ada → `FATAL_ERROR`, build berhenti.
- **OpenMP opsional** — `find_package(OpenMP QUIET)`. Kalau ada:
  - definisikan makro `ALCHEMY_HAS_OPENMP=1` (kode di-`#ifdef` pakai ini),
  - build executable `alchemy_openmp`.
  - Kalau tidak ada: lewati `alchemy_openmp`, dan thread hybrid MPI dimatikan.
- **MPI opsional** — `find_package(MPI QUIET)`. Kalau ada → build `alchemy_mpi`. Kalau
  tidak → hanya warning; serial & openmp tetap bisa di-build.
- Semua executable **link** ke library `alchemy_common`.
- `enable_testing()` + target `alchemy_tests` (dijalankan via `ctest`).

> **Istilah — `#ifdef ALCHEMY_HAS_OPENMP`:** "if defined". Blok kode di dalamnya hanya
> ikut dikompilasi kalau makro `ALCHEMY_HAS_OPENMP` ada. Ini cara satu file sumber
> (`Search.cpp`) bisa punya versi ber-OpenMP **dan** versi tanpa OpenMP, tergantung
> apakah OpenMP tersedia saat build.

Perintah build standar (detail lengkap di [14-cli-build-run.md](14-cli-build-run.md)):

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

---

**Lanjut ke:** [03-struktur-data.md](03-struktur-data.md) — struktur data yang dipakai
engine: graf resep, pohon resep, dan trik "interning" yang bikin paralelisme cepat. →

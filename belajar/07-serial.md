# 07 — Mode Serial (Baseline)

> **Prasyarat baca:** [04-searching-overview.md](04-searching-overview.md),
> [06-konsep-paralel-hpc.md](06-konsep-paralel-hpc.md).
> **Setelah ini kamu paham:** mode paling sederhana, kenapa ia jadi titik acuan
> (baseline) untuk semua perbandingan, dan betapa tipisnya `main_serial.cpp`.

Kode: [src/serial/main_serial.cpp](src/serial/main_serial.cpp).

---

## 1. Intuisi

Serial = **satu pekerja, satu thread, satu proses**. Tidak ada paralelisme sama sekali.
Ini "jalan kaki sendirian": paling lambat untuk beban berat, tapi paling sederhana dan
**paling bisa diandalkan**. Karena itu ia dijadikan **baseline** — patokan yang dipakai
menghitung speedup mode lain.

> **Istilah — baseline:** garis dasar / titik acuan. "Berapa cepat tanpa paralel sama
> sekali?" Semua speedup dihitung relatif ke angka ini (`S = T_serial / T_paralel`).

---

## 2. Detail teknis: alur `main`

`main_serial.cpp` mengikuti pipeline umum dari [02 §4](02-arsitektur.md#4-pipeline-eksekusi-umum-6-langkah)
**tanpa** setup paralel apa pun:

```cpp
auto options = parseArgs(argc, argv, false);   // mpiMode=false → flag MPI ditolak
// ... muat graph & tiers, applyTerminals ...
if (listElements)   return runListElements(...);
if (benchmark)      return runBenchmark(...);
return runSingle(options, graph);              // jalur utama
```

Catatan kecil tapi penting
([main_serial.cpp:156-158](src/serial/main_serial.cpp#L156-L158)):

```cpp
if (options.threads != 1) {
    throw std::runtime_error("--threads is only supported by alchemy_openmp or alchemy_mpi");
}
```

Serial **menolak** `--threads`. Kalau mau banyak thread, pakai `alchemy_openmp`. Ini
menegaskan: serial benar-benar 1 pekerja.

### `runSingle` — inti

[main_serial.cpp:95-105](src/serial/main_serial.cpp#L95-L105):

```cpp
SearchEngine engine(graph, options);
auto result = engine.search(options.target);                 // ← seluruh pencarian
auto outputs = Visualizer::writeOutputs(result.target, result.recipes, result.stats, options);
printSummary(options, result, outputs);
```

Hanya itu. `engine.search()` (lihat [04 §2](04-searching-overview.md#2-titik-masuk-searchenginesearch))
menjalankan BFS/DFS lurus tanpa thread. Statistik yang dicatat:

```cpp
stats.processes        = 1;
stats.threadsPerProcess = 1;   // normalizedThreadCount: useOpenmp=false → 1
stats.totalWorkers     = 1;
```

(Lihat [Search.cpp:414-417](src/common/Search.cpp#L414-L417).)

---

## 3. Cara menjalankan

```bash
build/alchemy_serial --data data/recipes.json --tiers data/tiers.json \
    --target Brick --algorithm bfs --mode multiple --limit 50 \
    --output results/h_serial
```

Output: `results/h_serial.json` + ringkasan di layar (target, waktu, nodes visited,
dan pohon ASCII tiap resep). Lihat contoh nyata di `results/h_serial.json`.

Angka `Execution time` dari sini → inilah yang kamu masukkan sebagai `--baseline-ms` saat
menjalankan MPI/hybrid untuk menghitung speedup.

---

## 4. Peran dalam perbandingan

Pada eksperimen resmi ([15](15-benchmark-dan-interpretasi.md)), serial untuk Brick limit
50 = **92.69 ms**, speedup 1.000, efisiensi 1.000 (per definisi). Semua mode lain dibanding
ke angka ini. Menariknya, beberapa mode paralel **tidak mengalahkannya** untuk workload
ringan ini — pelajaran utama proyek.

---

**Lanjut ke:** [08-openmp.md](08-openmp.md) — paralel shared-memory pertama kita. →

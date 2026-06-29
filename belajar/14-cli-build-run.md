# 14 — CLI, Build & Run (Cheat-Sheet Praktis)

> **Prasyarat baca:** [02-arsitektur.md](02-arsitektur.md) untuk konteks build.
> **Setelah ini kamu paham:** cara build proyek, semua flag CLI, dan contoh perintah siap
> pakai untuk tiap mode.

---

## 1. Build

> **Istilah — build:** proses mengompilasi kode sumber menjadi executable.

```bash
# 1. Konfigurasi (CMake mendeteksi compiler, OpenMP, MPI, nlohmann_json)
cmake -S . -B build

# 2. Kompilasi semua target
cmake --build build

# 3. Jalankan unit test
ctest --test-dir build --output-on-failure
```

Hasil executable ada di `build/`: `alchemy_serial`, `alchemy_openmp`, `alchemy_mpi`
(dan `alchemy_tests`). Catatan ketersediaan:
- `alchemy_openmp` hanya ter-build kalau OpenMP terdeteksi.
- `alchemy_mpi` hanya ter-build kalau MPI terdeteksi.
- `nlohmann_json` **wajib**, kalau tak ada build berhenti (lihat
  [02 §5](02-arsitektur.md#5-build-system-cmakeliststxt)).

---

## 2. Daftar flag CLI

Dari [Cli.hpp](src/common/Cli.hpp) (`AppOptions`) dan `usageText`:

| Flag | Arti | Default |
|---|---|---|
| `--data PATH` | file data resep JSON | `data/recipes.json` |
| `--tiers PATH` | katalog tier | `data/tiers.json` |
| `--target NAME` | elemen yang dicari | — |
| `--algorithm bfs\|dfs` | algoritma pencarian | `bfs` |
| `--mode single\|multiple\|all` | mode pencarian | `multiple` |
| `--limit N` | jumlah resep (mode multiple) | `5` |
| `--trace-mode full\|memo` | memoization on/off | `memo` |
| `--visual-mode full\|shared` | gaya gambar | `full` |
| `--render json\|full` | json saja, atau + DOT/gambar | `json` |
| `--format png\|svg` | format gambar | `png` |
| `--max-visual-depth N` | potong gambar besar | `-1` (tak dipotong) |
| `--threads N` | thread OpenMP (openmp / hybrid mpi) | `1` |
| `--output PREFIX` | prefix file output | `results/alchemy` |
| `--benchmark FILE` | jalankan daftar target → CSV | — |
| `--list-elements --tier ... [--filter ...]` | bantu cari nama target | — |
| **khusus MPI:** | | |
| `--split-depth N` | kedalaman ekspansi task | `1` |
| `--thread-profile SPEC` | profil hybrid per host, mis. `1x2,2x4,2x4` | — |
| `--baseline-ms X` | waktu serial untuk hitung speedup/efisiensi | `0` |

> Flag `--split-depth`, `--thread-profile`, `--baseline-ms` **hanya** valid di
> `alchemy_mpi` (karena `parseArgs(.., mpiMode=true)`). Lihat
> [02 §4](02-arsitektur.md#4-pipeline-eksekusi-umum-6-langkah).

---

## 3. Contoh perintah tiap mode

### Serial (baseline)
```bash
build/alchemy_serial --target Brick --algorithm bfs --mode multiple --limit 50 \
    --output results/h_serial
```

### OpenMP (4 thread, 1 PC)
```bash
build/alchemy_openmp --target Brick --mode multiple --limit 50 \
    --threads 4 --output results/demo_omp4
```

### MPI lokal (4 proses, 1 PC)
```bash
mpiexec -n 4 build/alchemy_mpi --target Brick --mode multiple --limit 50 \
    --split-depth 2 --baseline-ms 92.69 --output results/demo_mpi4
```

### Hybrid lokal (2 rank × 2 thread)
```bash
mpiexec -n 2 build/alchemy_mpi --target Brick --mode multiple --limit 50 \
    --threads 2 --baseline-ms 92.69 --output results/demo_hybrid
```

### Multi-PC (master 4 slot + slave 2 slot) — MS-MPI
```powershell
mpiexec -hosts 2 localhost 4 desktop-slave 2 -wdir C:\tubes-2 ^
    C:\tubes-2\build\alchemy_mpi.exe --data C:\tubes-2\data\recipes.json ^
    --tiers C:\tubes-2\data\tiers.json --target Brick --mode multiple --limit 50 ^
    --split-depth 2 --baseline-ms 92.69 --output C:\tubes-2\results\multi
```
(Detail jaringan di [11-multinode-lan.md](11-multinode-lan.md).)

### Render gambar (butuh Graphviz `dot` di PATH)
Tambahkan `--render full --format png` ke perintah mana pun.

### Cari nama target valid
```bash
build/alchemy_serial --list-elements --tier tier3 --filter glass
```

---

## 4. Membaca output

Setiap run menghasilkan:
- **`<prefix>.json`** — selalu. Berisi `target`, parameter, `recipes_found`, blok
  `statistics`, dan array `recipes` (pohon).
- **`<prefix>.dot` + `<prefix>.png/svg`** — hanya jika `--render full`.
- Ringkasan di layar: target, statistik (waktu, nodes visited, cache, per-rank untuk MPI),
  lalu **pohon ASCII** tiap resep.

Untuk `--benchmark`, output utama adalah **`<prefix>.csv`** (satu baris per target). Kolom
CSV (versi MPI): `target,algorithm,mode,trace_mode,visual_mode,processes,recipes_found,
nodes_visited,cache_hits,cache_entries,time_ms,tasks_processed,communication_ms,output_dot,
output_image`. Versi OpenMP punya kolom `threads_per_process,total_workers` tambahan.

Contoh-contoh hasil nyata ada di folder `results/` (mis. `results/demo_serial.json`,
`results/gui_run_compare.csv`).

---

**Lanjut ke:** [15-benchmark-dan-interpretasi.md](15-benchmark-dan-interpretasi.md) —
angka hasil sebenarnya dan apa artinya. →

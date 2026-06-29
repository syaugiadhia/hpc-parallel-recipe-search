# 10 — Mode Hybrid (MPI + OpenMP)

> **Prasyarat baca:** [08-openmp.md](08-openmp.md) dan [09-mpi.md](09-mpi.md).
> **Setelah ini kamu paham:** bagaimana dua model paralel digabung dalam satu run, beda
> `--threads` vs `--thread-profile`, dan bagaimana tiap rank diatur jumlah thread-nya.

Kode: `configureRankThreads` & `expandThreadProfile` di
[main_mpi.cpp:56-103](src/mpi/main_mpi.cpp#L56-L103).

---

## 1. Intuisi: konvoi truk berisi tim pendorong

Hybrid = **MPI di luar, OpenMP di dalam**:

- **MPI** membagi kerja **antar proses/komputer** (truk-truk di lokasi berbeda).
- **OpenMP** memparalelkan kerja **di dalam tiap proses** (tim pendorong di tiap truk).

```
Komputer A                          Komputer B
┌────────────────────┐              ┌────────────────────┐
│ Rank 0 (master)    │              │ Rank 2 (worker)    │
│   2 thread OpenMP  │   pesan MPI  │   4 thread OpenMP   │
│ Rank 1 (worker)    │◄────────────►│ Rank 3 (worker)    │
│   4 thread OpenMP  │   (jaringan) │   4 thread OpenMP   │
└────────────────────┘              └────────────────────┘
```

Tiap **rank** (proses MPI) yang menerima task akan menyelesaikannya memakai
`completePartial`, yang **di dalamnya** memakai BFS ber-OpenMP (lihat
[08-openmp.md](08-openmp.md)) kalau thread-nya > 1.

> **Kenapa hybrid masuk akal?** Antar-komputer kamu *wajib* pakai MPI (OpenMP tak bisa
> lintas mesin — lihat [12](12-kenapa-openmp-tidak-multinode.md)). Tapi di dalam satu
> komputer multi-core, OpenMP lebih murah daripada menjalankan banyak proses MPI yang
> saling berkirim pesan. Jadi: MPI untuk menyeberang mesin, OpenMP untuk memanfaatkan
> core di tiap mesin.

---

## 2. Dua cara mengatur thread

### Cara A — `--threads N` (seragam)
Semua rank memakai N thread yang sama.

```bash
mpiexec -n 4 build/alchemy_mpi ... --threads 2
```
→ 4 rank × 2 thread = 8 worker total.

### Cara B — `--thread-profile SPEC` (per host)
Format `proses×thread` dipisah koma, **berurutan per host**:

```bash
mpiexec ... --thread-profile 1x2,2x4,2x4
```

Artinya:
- host-1: **1** proses × **2** thread,
- host-2: **2** proses × **4** thread,
- host-3: **2** proses × **4** thread.

Total: 1+2+2 = **5 rank**, dan thread per rank = `[2, 4, 4, 4, 4]`. Total worker =
2+4+4+4+4 = **18**.

> ⚠️ Jumlah rank yang dihasilkan profil **harus cocok** dengan jumlah rank yang
> diluncurkan `mpiexec`. Kalau tidak, program melempar error (lihat di bawah).

---

## 3. Detail teknis: `expandThreadProfile`

[main_mpi.cpp:56-83](src/mpi/main_mpi.cpp#L56-L83) mengurai SPEC jadi **vektor thread
per-rank**:

```cpp
// "1x2,2x4" → untuk tiap token "PxT": tambahkan T sebanyak P kali
threadsByRank.insert(end, processes, threads);   // mis. 2x4 → tambah {4,4}
...
if (threadsByRank.size() != worldSize)
    throw runtime_error("--thread-profile expands to X ranks, but mpiexec launched Y");
```

Jadi `1x2,2x4,2x4` → `[2,4,4,4,4]`. Validasi memastikan ukurannya = `worldSize` (jumlah
rank dari `mpiexec`).

---

## 4. Detail teknis: `configureRankThreads`

[main_mpi.cpp:85-103](src/mpi/main_mpi.cpp#L85-L103). Tiap rank menjalankan ini saat start:

```cpp
if (!threadProfile.empty()) {
    threadsByRank = expandThreadProfile(threadProfile, worldSize);
    options.threads = threadsByRank[rank];      // ambil jatah thread RANK INI
} else {
    options.threads = max(1, options.threads);  // mode --threads seragam
    threadsByRank.assign(worldSize, options.threads);
}
if (options.threads > 1 && !openmpAvailable())
    throw runtime_error("This alchemy_mpi build does not include OpenMP support ...");
options.useOpenmp = options.threads > 1;
omp_set_num_threads(options.threads);           // aktifkan thread untuk rank ini
```

Poin penting:
- Tiap rank mengambil **jatah thread-nya sendiri** dari `threadsByRank[rank]`.
- Kalau build `alchemy_mpi` **tidak** punya OpenMP tapi kamu minta `>1` thread → error
  jelas. (OpenMP harus tersedia saat build; lihat [02 §5](02-arsitektur.md#5-build-system-cmakeliststxt).)
- `useOpenmp` hanya nyala kalau thread > 1, sehingga rank ber-1-thread berjalan seperti
  MPI murni.

`threadVectorForRun` & `uniformThreadCount` ([MpiMaster.cpp:134-151](src/mpi/MpiMaster.cpp#L134-L151))
lalu memakai vektor ini untuk mengisi statistik (`threadsByRank`, `threadsPerProcess`,
`totalWorkers`).

---

## 5. Total worker & efisiensi

`totalWorkers` di hybrid = **jumlah semua thread di semua rank**
([MpiMaster.cpp:292](src/mpi/MpiMaster.cpp#L292)):

```cpp
stats.totalWorkers = std::accumulate(threadsByRank.begin(), threadsByRank.end(), 0);
```

Efisiensi `E = speedup / totalWorkers` memakai angka ini — jadi makin banyak thread
diklaim, makin sulit efisiensi tinggi (tiap worker harus benar-benar berkontribusi).

---

## 6. Cara menjalankan (hybrid lokal)

```bash
# 2 rank, masing-masing 2 thread, semua di 1 PC:
mpiexec -n 2 build/alchemy_mpi --data data/recipes.json --tiers data/tiers.json \
    --target Brick --mode multiple --limit 50 --threads 2 \
    --baseline-ms 92.69 --output results/demo_hybrid

# hybrid multi-host via thread-profile (lihat 11-multinode-lan.md untuk -hosts):
mpiexec -hosts 2 localhost 2 SLAVE 2 build/alchemy_mpi ... --thread-profile 1x2,1x2
```

Di ringkasan akan muncul `Threads by rank: [2, 2]` dan `Total workers: 4`.

---

**Lanjut ke:** [11-multinode-lan.md](11-multinode-lan.md) — menjalankan MPI/hybrid lintas
komputer sungguhan via LAN. →

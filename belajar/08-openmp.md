# 08 — Mode OpenMP (Paralel Shared-Memory)

> **Prasyarat baca:** [05-bfs-lazy-detail.md](05-bfs-lazy-detail.md) (frontier, estimate,
> ekspansi state) dan [06-konsep-paralel-hpc.md](06-konsep-paralel-hpc.md) (shared memory,
> thread, Amdahl).
> **Setelah ini kamu paham:** bagaimana OpenMP memparalelkan BFS dengan banyak thread di
> satu komputer, kenapa itu aman, kapan paralel diaktifkan, dan kenapa speedup-nya terbatas.

Kode: `expandPartialBfsLazyOpenmp` di
[Search.cpp:677-755](src/common/Search.cpp#L677-L755); setup di `configureOpenmp`
([main_openmp.cpp:39-48](src/openmp/main_openmp.cpp#L39-L48)).

---

## 1. Intuisi: tim pendorong di satu mobil

OpenMP = beberapa **thread** di **satu proses, satu komputer**, berbagi RAM yang sama
(termasuk `RecipeGraph` dan frontier). Analogi: satu mobil dengan beberapa orang mendorong
bareng.

Di mana mereka mendorong bersama? Di **ekspansi state BFS**. Ingat dari
[05 §6](05-bfs-lazy-detail.md#6-kenapa-desain-ini-bagus-untuk-diparalelkan): mengekspansi
satu bibit (pohon parsial) tidak mengubah data bersama — itu kerja independen. Jadi:
kumpulkan banyak bibit yang "selevel", lalu ekspansi mereka **serentak** oleh banyak thread.

```
   Frontier (priority_queue)
        │  ambil sekumpulan bibit ber-estimate SAMA → "batch"
        ▼
   batch = [ bibit, bibit, bibit, bibit, ... ]   (≤ threads*8)
        │
        ▼   #pragma omp parallel for  (banyak thread sekaligus)
   ┌─────────┬─────────┬─────────┬─────────┐
   │Thread 0 │Thread 1 │Thread 2 │Thread 3 │   ← tiap thread ekspansi 1 bibit
   │expand() │expand() │expand() │expand() │     (fungsi MURNI, tak ganggu yg lain)
   └────┬────┴────┬────┴────┬────┴────┬────┘
        └─────────┴────┬────┴─────────┘
                       ▼  digabung SERIAL oleh thread utama
              dorong bibit-bibit baru kembali ke frontier
```

---

## 2. Setup: `configureOpenmp`

[main_openmp.cpp:39-48](src/openmp/main_openmp.cpp#L39-L48):

```cpp
if (!openmpAvailable()) throw runtime_error("This build does not include OpenMP support");
options.useOpenmp = true;
options.threads   = std::max(1, options.threads);
omp_set_num_threads(options.threads);   // berapa thread yang dipakai OpenMP
```

> **Istilah — `omp_set_num_threads(n)`:** memberi tahu runtime OpenMP untuk memakai `n`
> thread di region paralel berikutnya. Ini dari pustaka `<omp.h>`.

`alchemy_openmp` hanya di-build kalau compiler punya OpenMP (lihat
[02 §5](02-arsitektur.md#5-build-system-cmakeliststxt)).

---

## 3. Kapan paralel benar-benar aktif?

Bukan selalu. `expandPartialBfsLazy` mengalihkan ke versi OpenMP hanya jika **semua**
syarat ini terpenuhi ([Search.cpp:589-592](src/common/Search.cpp#L589-L592)):

```cpp
if (options.useOpenmp && options.threads > 1 && openmpAvailable() &&
    options.mode != SearchMode::All && limit > 1) {
    return expandPartialBfsLazyOpenmp(partial, limit);
}
```

Jadi paralel mati kalau: 1 thread, atau mode `all`, atau `limit == 1` (single). Masuk akal
— untuk kasus-kasus itu tak ada cukup kerja untuk dibagi.

---

## 4. Detail teknis: bagaimana batch diparalelkan

Lihat [Search.cpp:699-751](src/common/Search.cpp#L699-L751). Per putaran loop:

**(a) Kumpulkan batch** — ambil bibit teratas, lalu terus ambil bibit berikutnya **selama
estimate-nya sama** dan batch belum penuh:

```cpp
const std::size_t batchLimit = std::max(8, threadCount * 8);
...
while (batch.size() < batchLimit && !frontier.empty()) {
    const auto& next = frontier.top();
    if (next.estimate != batchEstimate || next.openLeaves.empty()) break;
    batch.push_back(next);
    frontier.pop();
}
```

Kenapa hanya yang **estimate sama**? Supaya urutan prioritas (terpendek dulu) tetap
terjaga — kita tak boleh "menyalip" bibit yang lebih menjanjikan.

**(b) Ekspansi paralel** — tapi hanya kalau batch cukup besar:

```cpp
if (batch.size() >= 2 * threadCount) {
#pragma omp parallel for schedule(dynamic) num_threads(threadCount)
    for (int i = 0; i < batch.size(); ++i)
        expansions[i] = expandFrontierState(graph_, depthMap, std::move(batch[i]));
} else {
    for (...) expansions[i] = expandFrontierState(...);   // serial saja
}
```

Dua hal kunci:

- **`expandFrontierState`** ([Search.cpp:238-295](src/common/Search.cpp#L238-L295)) adalah
  **fungsi murni** (bebas dari `SearchEngine`-state): ia hanya membaca `graph_` &
  `depthMap` (read-only) dan menghasilkan bibit-bibit baru. Tak ada penulisan ke data
  bersama → **aman tanpa kunci (lock)**. Inilah alasan paralel ini benar.
  > **Istilah — race condition:** bug saat dua thread menulis data yang sama tanpa
  > koordinasi, hasilnya acak/rusak. Dihindari di sini dengan membuat ekspansi *tak menulis
  > apa pun yang dibagi*.

- **`schedule(dynamic)`** — biaya tiap ekspansi tidak seragam (ada daun dengan banyak
  resep, ada yang sedikit). `dynamic` membagi tugas ke thread **saat berjalan**: thread
  yang selesai duluan langsung ambil tugas berikutnya. Ini **load balancing**.
  > **Istilah — load balancing:** menyeimbangkan beban antar pekerja supaya tak ada yang
  > nganggur sementara yang lain kewalahan.

**(c) Gabung serial** — setelah batch selesai, thread utama menyatukan hasil:

```cpp
for (auto& expansion : expansions) {
    stats_.nodesVisited += expansion.nodesVisited;
    for (auto& state : expansion.states) pushState(std::move(state));  // ke frontier
}
```

Penggabungan ke `priority_queue` dilakukan **serial** (bukan paralel) karena
`priority_queue` bukan struktur yang aman ditulis banyak thread. Ini bagian serial yang
membatasi speedup (Amdahl).

---

## 5. Kenapa syarat `batch.size() >= 2*threadCount`?

Ini optimasi penting. Komentar asli
([Search.cpp:726-729](src/common/Search.cpp#L726-L729)) menjelaskan:

> *"Hanya fork/join tim thread kalau batch cukup besar untuk menutup overheadnya. Di
> workload besar, banyak level frontier punya batch kecil; fork/join per level (dulu tanpa
> syarat) bikin openmp lebih lambat dari serial. Output sama persis (ekspansi identik,
> cuma dihitung serial saat batch kecil)."*

> **Istilah — fork/join:** "fork" = membuat/membangunkan tim thread; "join" = menunggu
> semua selesai lalu menyatukannya. Tiap `#pragma omp parallel for` melakukan satu siklus
> fork/join, dan itu **ada biayanya**.

Intinya: kalau cuma ada 3 bibit dan 4 thread, membuat tim thread lebih mahal daripada
sekadar mengerjakannya sendiri. Jadi untuk batch kecil → kerjakan serial. **Hasil tetap
identik**, hanya cara menghitungnya beda. Ini bukti penting bahwa paralelisme di sini
**tidak mengubah kebenaran**, hanya kecepatan.

---

## 6. Batas speedup (Hukum Amdahl di praktik)

Yang **paralel** hanya ekspansi batch. Yang tetap **serial**:
- mengambil/mengelola `priority_queue`,
- mengumpulkan batch,
- `pushState` (merge ke frontier),
- cek duplikat `seenFinalSignatures`.

Untuk Brick limit 50 (workload ringan), bagian serial + overhead fork/join mendominasi.
Hasil eksperimen: OpenMP 4 thread = 86.50 ms vs serial 92.69 ms → **speedup cuma 1.072**,
efisiensi turun ke 0.268. Masih lebih cepat, tapi tipis. Penjelasan lengkap di
[15-benchmark-dan-interpretasi.md](15-benchmark-dan-interpretasi.md).

> 💡 **Penting untuk dipahami:** OpenMP unggul justru karena overhead-nya **rendah** (semua
> di satu RAM, tak ada kirim pesan). Bandingkan dengan MPI yang overhead komunikasinya
> besar — itu sebabnya MPI lokal malah lebih lambat (lihat [09](09-mpi.md) & [15](15-benchmark-dan-interpretasi.md)).

---

## 7. Cara menjalankan

```bash
build/alchemy_openmp --data data/recipes.json --tiers data/tiers.json \
    --target Brick --algorithm bfs --mode multiple --limit 50 \
    --threads 4 --output results/demo_omp4
```

`Threads per process: 4`, `Total workers: 4` akan muncul di ringkasan. Lihat contoh hasil
di `results/demo_omp4.json`.

---

**Lanjut ke:** [09-mpi.md](09-mpi.md) — paralel distributed-memory yang bisa lintas
komputer. →

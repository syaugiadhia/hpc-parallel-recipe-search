# 06 — Konsep Paralel & HPC (Fondasi)

> **Prasyarat baca:** [01](01-pengenalan-dan-domain.md). Tidak perlu paham kode dulu.
> **Setelah ini kamu paham:** beda proses vs thread, shared vs distributed memory, apa
> itu speedup/efisiensi, dan kenapa menambah pekerja tidak selalu mempercepat (Hukum
> Amdahl). Ini fondasi wajib sebelum file 07–12.

---

## 1. Kenapa paralel? Intuisi tukang batu

Satu tukang menata 1000 batu bata butuh 1000 menit. Sepuluh tukang bekerja **paralel** —
idealnya — selesai 100 menit. Itulah inti **High Performance Computing**: bagi satu
pekerjaan besar ke banyak pekerja agar selesai lebih cepat.

Tapi kenyataan tak seindah teori:
- Sepuluh tukang harus **berkoordinasi** (siapa menata bagian mana) — itu makan waktu.
- Ada bagian yang **tak bisa dibagi** (mis. menyiapkan semen awal) — tetap serial.
- Kalau pekerjaannya cuma 10 batu, memanggil 10 tukang malah lebih ribet daripada
  dikerjakan sendiri.

Ketiga kenyataan ini muncul persis di proyek ini (lihat hasilnya di
[15-benchmark-dan-interpretasi.md](15-benchmark-dan-interpretasi.md)).

---

## 2. Proses vs Thread

Dua cara dasar "menggandakan pekerja":

### Thread (utas)
> **Istilah — thread:** "pekerja" di dalam **satu** program/proses. Semua thread dalam
> satu proses **berbagi memori yang sama** — mereka bisa membaca & menulis variabel yang
> sama secara langsung.

Analogi: beberapa orang menulis di **satu papan tulis yang sama** di satu ruangan. Cepat
bertukar info (tinggal lihat papan), tapi bisa saling tabrak kalau menulis di tempat sama
bersamaan.

### Proses
> **Istilah — proses:** program yang berjalan sebagai unit terpisah, dengan **memori
> sendiri-sendiri** yang tidak bisa diakses proses lain secara langsung. Untuk bertukar
> data, proses harus **berkirim pesan**.

Analogi: beberapa orang di **gedung berbeda**, masing-masing punya papan tulis sendiri.
Untuk berbagi info, harus **kirim surat**. Lebih lambat bertukar, tapi tak ada rebutan
papan, dan bisa di **gedung (komputer) mana pun**.

---

## 3. Shared-memory vs Distributed-memory (KONSEP PALING PENTING)

Ini membedakan OpenMP dari MPI, dan menjelaskan kenapa OpenMP tak bisa multinode
(dibahas tuntas di [12](12-kenapa-openmp-tidak-multinode.md)).

```
┌─────────────── SHARED MEMORY (OpenMP) ───────────────┐
│  Satu komputer, satu proses                          │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐         │
│  │Thread 0│ │Thread 1│ │Thread 2│ │Thread 3│         │
│  └───┬────┘ └───┬────┘ └───┬────┘ └───┬────┘         │
│      └──────────┴────┬─────┴──────────┘              │
│                ┌─────▼─────┐                          │
│                │  RAM yang  │  ← semua thread baca/   │
│                │   SAMA     │    tulis di sini langsung│
│                └───────────┘                          │
└──────────────────────────────────────────────────────┘

┌──────────── DISTRIBUTED MEMORY (MPI) ────────────────┐
│  Bisa banyak komputer, banyak proses                 │
│  Komputer A            │   Komputer B                 │
│  ┌────────┐ ┌────────┐ │   ┌────────┐ ┌────────┐      │
│  │Proses 0│ │Proses 1│ │   │Proses 2│ │Proses 3│      │
│  │ RAM A0 │ │ RAM A1 │ │   │ RAM B2 │ │ RAM B3 │      │
│  └───┬────┘ └───┬────┘ │   └───┬────┘ └───┬────┘      │
│      └───pesan──┴──────┼───────┴──pesan───┘           │
│             (lewat jaringan / LAN)                    │
└──────────────────────────────────────────────────────┘
```

- **Shared-memory:** banyak thread, satu kolam RAM bersama. Komunikasi = baca/tulis
  variabel (instan). **Batas: satu komputer.** Ini wilayah **OpenMP**.
- **Distributed-memory:** banyak proses, tiap proses RAM sendiri. Komunikasi = **kirim
  pesan eksplisit**. Bisa lintas komputer lewat jaringan. Ini wilayah **MPI**.

Di proyek ini:
- OpenMP berbagi `RecipeGraph` & frontier dalam satu proses.
- MPI: tiap rank baca `recipes.json` **sendiri** (tak ada graf bersama) dan bertukar task
  & hasil sebagai **pesan JSON**.

---

## 4. Istilah MPI: rank, worker, slot

- **Rank** — nomor identitas tiap proses MPI (0, 1, 2, ...). Rank 0 biasanya "bos"
  (master). Lihat [09-mpi.md](09-mpi.md).
- **Worker** — pekerja efektif. Di MPI murni = jumlah rank pekerja. Di hybrid = total
  thread semua rank (`totalWorkers`). Dipakai untuk menghitung efisiensi.
- **Slot** — berapa proses MPI boleh jalan di satu komputer (mis. master 4 slot, slave 2
  slot → total 6 proses). Lihat [11-multinode-lan.md](11-multinode-lan.md).

---

## 5. Mengukur keberhasilan paralel: Speedup & Efisiensi

> **Istilah — speedup (S):** berapa kali lebih cepat dibanding versi serial.
> ```
> S = T_serial / T_paralel
> ```
> S = 1 → sama saja. S = 4 → 4× lebih cepat. **S < 1 → malah lebih lambat** (terjadi di
> MPI lokal proyek ini!).

> **Istilah — efisiensi (E):** speedup per pekerja. Seberapa "terpakai" tiap pekerja.
> ```
> E = S / p          (p = jumlah pekerja/worker)
> ```
> E = 1 (atau 100%) → ideal, tiap pekerja berkontribusi penuh. E = 0.5 → tiap pekerja
> efektif hanya separuh (separuh lagi hilang ke overhead).

Di kode, dihitung di MPI master ([MpiMaster.cpp:359-362](src/mpi/MpiMaster.cpp#L359-L362)):

```cpp
stats.speedup    = baselineMs / timeMs;                  // butuh --baseline-ms
stats.efficiency = speedup / std::max(1, totalWorkers);
```

`--baseline-ms` = waktu serial yang kamu ukur lebih dulu, dipakai sebagai `T_serial`.

---

## 6. Kenapa menambah pekerja tak selalu mempercepat: Hukum Amdahl

> **Istilah — Hukum Amdahl:** kalau sebagian pekerjaan **tak bisa diparalelkan** (bagian
> serial), maka bagian itu menjadi batas atas kecepatan, berapa pun pekerja ditambah.

Rumus sederhananya: kalau fraksi `f` dari pekerjaan tetap serial, maka dengan `p` pekerja:

```
S_max = 1 / ( f + (1 - f)/p )
```

Kalau `f = 0` (semua paralel), `S = p` (ideal). Kalau `f = 0.5`, sebanyak apa pun pekerja,
S tak pernah lebih dari 2×.

Di proyek ini, bagian **serial** pencarian meliputi:
- mengelola `priority_queue` frontier (BFS),
- deduplikasi (cek signature),
- menggabungkan (merge) hasil.

Hanya **ekspansi state** yang diparalelkan. Untuk workload ringan (mis. Brick limit 50),
bagian serial + overhead thread mendominasi → speedup mentok tipis (lihat [15](15-benchmark-dan-interpretasi.md)).

---

## 7. Overhead: harga yang harus dibayar paralel

> **Istilah — overhead:** kerja tambahan yang tidak ada di versi serial, muncul *karena*
> paralel. Bukan kerja "berguna", tapi tak terhindarkan.

Dua jenis overhead utama di proyek:

- **Overhead sinkronisasi (OpenMP):** biaya membuat & menyatukan tim thread tiap putaran
  (fork/join). Kalau pekerjaan per putaran kecil, biaya ini bisa lebih besar dari kerjanya.
- **Overhead komunikasi (MPI):** biaya kirim/terima pesan + ubah data ke/dari JSON
  (serialisasi). Diukur sebagai `communicationMs`. Di proyek ini, untuk workload ringan,
  `communicationMs` bahkan **melebihi** waktu serial → MPI lokal jadi lebih lambat.

Memahami overhead inilah inti kesimpulan akademik proyek (file [15](15-benchmark-dan-interpretasi.md)).

---

**Lanjut ke:** [07-serial.md](07-serial.md) — mode paling sederhana sebagai baseline. →

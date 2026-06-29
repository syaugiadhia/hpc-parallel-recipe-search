# 📚 Belajar Codebase: Little Alchemy HPC Recipe Search

Selamat datang. Folder `belajar/` ini adalah **materi belajar bertahap** untuk memahami
seluruh proyek ini — dari "apa sih ini?" sampai detail terdalam algoritma dan paralelisme.

Berbeda dengan `paper/PENJELASAN_CODEBASE.md` (satu file padat untuk bikin slide), di sini
materi **dipecah per topik**, ditulis naratif, dengan **analogi**, **diagram ASCII**,
**contoh langkah-demi-langkah**, dan yang terpenting: **menjelaskan "kenapa"-nya**, bukan
cuma "apa"-nya.

> **Gaya penulisan:** tiap bab dibagi dua lapis — bagian **"Intuisi"** (santai, pakai
> analogi, buat yang baru kenal HPC) lalu **"Detail teknis"** (akurat sampai level kode).
> Semua istilah teknis dijelaskan saat pertama muncul, dan dikumpulkan lagi di
> [16-glossary-dan-faq.md](16-glossary-dan-faq.md). Jadi kamu tidak perlu sudah jago
> dulu untuk mulai.

---

## Ringkasan 1 paragraf

Proyek ini mencari **resep Little Alchemy 2**: diberi sebuah elemen target (mis. *Brick*),
program membangun **pohon resep** yang menjelaskan cara membuatnya dari 4 elemen dasar
(Air, Earth, Fire, Water). Masalahnya dimodelkan sebagai **pencarian pada graf**, memakai
algoritma **BFS** (best-first) atau DFS. Inti pencarian ditulis **sekali** di
`src/common/`, lalu dipakai 4 **mode eksekusi HPC**: Serial (baseline), OpenMP (banyak
thread di 1 komputer), MPI (banyak proses, bisa lintas komputer), dan Hybrid (MPI+OpenMP).
Fokus akademiknya: **membandingkan performa** keempat mode (waktu, speedup, efisiensi,
overhead komunikasi).

---

## 🗺️ Peta dokumen & urutan baca

Baca berurutan dari atas ke bawah untuk pemahaman paling mulus. Tiap angka adalah satu file.

### Bagian A — Dasar (wajib dulu)
| # | File | Isi |
|---|------|-----|
| 01 | [Pengenalan & Domain](01-pengenalan-dan-domain.md) | Apa proyek ini, dari game jadi graf, kenapa butuh HPC |
| 02 | [Arsitektur](02-arsitektur.md) | Struktur folder, "1 engine banyak mode", pipeline umum, build |
| 03 | [Struktur Data](03-struktur-data.md) | RecipeGraph, RecipeTree, CompactNode, interning, signature |

### Bagian B — Algoritma pencarian
| # | File | Isi |
|---|------|-----|
| 04 | [Searching Overview](04-searching-overview.md) | Peta semua algoritma: heuristik, BFS vs DFS, mode, memo |
| 05 | [BFS Lazy — Deep Dive](05-bfs-lazy-detail.md) | Jantung proyek: frontier, estimate, contoh jejak langkah-demi-langkah |

### Bagian C — Paralelisme (inti HPC)
| # | File | Isi |
|---|------|-----|
| 06 | [Konsep Paralel & HPC](06-konsep-paralel-hpc.md) | Proses vs thread, shared vs distributed memory, speedup, Amdahl |
| 07 | [Mode Serial](07-serial.md) | Baseline, pembanding speedup |
| 08 | [Mode OpenMP](08-openmp.md) | Paralel shared-memory, batch frontier, pragma |
| 09 | [Mode MPI](09-mpi.md) | Paralel distributed-memory, master-worker, protokol pesan |
| 10 | [Mode Hybrid MPI+OpenMP](10-hybrid-mpi-openmp.md) | Gabungan, thread-profile per host |
| 11 | [Multinode / Multi-PC LAN](11-multinode-lan.md) | Jalan lintas komputer, smpd, networking, troubleshooting |
| 12 | [Kenapa OpenMP TIDAK bisa multinode](12-kenapa-openmp-tidak-multinode.md) | Penjelasan mendalam shared vs message-passing |

### Bagian D — Sekeliling & praktik
| # | File | Isi |
|---|------|-----|
| 13 | [GUI Python](13-gui.md) | Wrapper orkestrasi, Run Compare, Master/Slave LAN |
| 14 | [CLI, Build & Run](14-cli-build-run.md) | Cara build & menjalankan tiap mode, daftar flag |
| 15 | [Benchmark & Interpretasi](15-benchmark-dan-interpretasi.md) | Hasil nyata + kenapa MPI lokal malah lebih lambat |
| 16 | [Glossary & FAQ](16-glossary-dan-faq.md) | Kamus istilah + pertanyaan umum |

---

## ⏱️ Kalau cuma punya 5 menit

Baca **[01-pengenalan-dan-domain.md](01-pengenalan-dan-domain.md)** (paham masalahnya),
lalu langsung **[15-benchmark-dan-interpretasi.md](15-benchmark-dan-interpretasi.md)**
(paham hasil & kesimpulannya). Itu memberi gambaran besar tanpa masuk kode.

## 🎯 Kalau kamu cuma penasaran soal pertanyaan tertentu

- *"Kok OpenMP gak bisa lintas komputer?"* → [12](12-kenapa-openmp-tidak-multinode.md)
- *"Gimana BFS-nya bekerja persisnya?"* → [05](05-bfs-lazy-detail.md)
- *"Apa beda thread, proses, rank, slot, worker?"* → [06](06-konsep-paralel-hpc.md) + [16](16-glossary-dan-faq.md)
- *"Gimana 3 laptop bisa kerja bareng?"* → [09](09-mpi.md) lalu [11](11-multinode-lan.md)
- *"Kenapa MPI malah lebih lambat dari serial?"* → [15](15-benchmark-dan-interpretasi.md)

---

## Komponen kode utama yang dirujuk (peta cepat)

```
src/common/   ← ENGINE INTI (dipakai semua mode)
  Search.*       BFS lazy, DFS, memo, OpenMP   ← jantung proyek
  RecipeGraph.*  graf resep + interning id
  RecipeTree.*   pohon resep + signature + json/ascii
  JsonLoader.*   parser data (3 format)
  TierCatalog.*  katalog tier + penanda terminal
  Statistics.*   metrik (per-rank juga)
  Visualizer.*   tulis JSON/DOT/PNG
  Cli.*          parse argumen
src/serial/   ← alchemy_serial   (1 proses, 1 thread)
src/openmp/   ← alchemy_openmp   (1 proses, banyak thread)
src/mpi/      ← alchemy_mpi      (banyak proses, bisa lintas PC; + hybrid)
gui/          ← GUI Python (orkestrator)
```

Selamat belajar! Mulai dari [01-pengenalan-dan-domain.md](01-pengenalan-dan-domain.md). →

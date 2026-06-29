# 15 — Benchmark & Interpretasi Hasil

> **Prasyarat baca:** [06-konsep-paralel-hpc.md](06-konsep-paralel-hpc.md) (speedup,
> efisiensi, Amdahl, overhead), dan [08](08-openmp.md)/[09](09-mpi.md).
> **Setelah ini kamu paham:** hasil eksperimen nyata proyek, kenapa OpenMP menang tipis,
> dan **kenapa MPI lokal justru lebih lambat dari serial** — pelajaran inti proyek.

Sumber angka: `paper/laporan.tex` (makalah IEEE resmi proyek).

---

## 1. Konfigurasi eksperimen

- **Target:** Brick
- **Algoritma:** BFS, mode multiple, **limit 50**
- **Trace:** memo, **Visual:** shared
- **Baseline:** waktu serial
- Semua varian menemukan **50 resep yang sama** → perbandingan adil.

> Catatan: ini sengaja **workload ringan**. Hasilnya akan kontra-intuitif, dan justru di
> situ pelajarannya.

---

## 2. Tabel hasil

| Varian | Proses | Thread/rank | Worker | Waktu (ms) | Komunikasi (ms) | Speedup | Efisiensi |
|--------|:------:|:-----------:|:------:|:----------:|:---------------:|:-------:|:---------:|
| Serial      | 1 | 1       | 1 | **92.69**  | 0.00   | 1.000 | 1.000 |
| OpenMP 2    | 1 | 2       | 2 | 88.71      | 0.00   | 1.045 | 0.522 |
| OpenMP 4    | 1 | 4       | 4 | **86.50**  | 0.00   | **1.072** | 0.268 |
| MPI lokal 2 | 2 | 1,1     | 2 | 395.60     | 332.77 | 0.234 | 0.117 |
| MPI lokal 4 | 4 | 1,1,1,1 | 4 | 542.23     | 613.00 | 0.171 | 0.043 |

---

## 3. Interpretasi

### 3.1 OpenMP menang — tapi tipis
- 4 thread tercepat (86.50 ms, **speedup 1.072**), 2 thread juga sedikit lebih cepat (1.045).
- Tapi **efisiensi turun** (0.522 → 0.268): menambah thread masih mempercepat, tapi
  kontribusi tiap thread mengecil.
- **Kenapa?** Workload kecil → bagian serial (kelola frontier, dedup, merge) + overhead
  fork/join thread mendominasi. Ini **Hukum Amdahl** di praktik (lihat
  [06 §6](06-konsep-paralel-hpc.md#6-kenapa-menambah-pekerja-tak-selalu-mempercepat-hukum-amdahl)).
  OpenMP tetap menang karena overhead-nya **rendah** (semua di satu RAM, tak ada kirim pesan).

### 3.2 MPI lokal LEBIH LAMBAT — dan makin parah saat proses ditambah
- 2 proses: 395 ms (speedup 0.234). 4 proses: 542 ms (speedup 0.171). **Lebih lambat dari
  serial**, dan makin buruk saat ditambah proses!
- **Akar masalah: communication time mendominasi** (332 ms → 613 ms), bahkan **melebihi**
  waktu serial. Lihat [09 §7](09-mpi.md#7-mengukur-waktu-komunikasi-kenapa-bisa--wall-clock)
  soal kenapa angka ini bisa sebesar itu.

Rincian sumber overhead MPI:
1. round-trip REQUEST→TASK tiap worker (lihat [09 §4](09-mpi.md#4-protokol-komunikasi-4-tag-pesan)),
2. serialisasi + parsing payload JSON tiap task & hasil,
3. merge & dedup terpusat di master (bagian serial),
4. memoization lokal per rank (cache tak dibagi antar rank),
5. beban Brick limit 50 terlalu ringan untuk menutup biaya itu.

> 💡 **Analogi:** menyuruh 4 orang di gedung berbeda mengerjakan tugas 5 menit, tapi mereka
> habis 30 menit kirim-kiriman surat untuk koordinasi. Lebih cepat dikerjakan sendiri.

---

## 4. Kesimpulan (yang penting untuk laporan/presentasi)

| Kondisi | Mode terbaik | Kenapa |
|---|---|---|
| Workload **ringan**, 1 PC | **OpenMP** | overhead rendah, tak ada komunikasi |
| Workload **berat**, 1 PC multi-core | OpenMP / Hybrid | kerja cukup besar untuk menutup overhead |
| Butuh **banyak komputer** | **MPI / Hybrid** | hanya MPI yang bisa multinode ([12](12-kenapa-openmp-tidak-multinode.md)) |
| Workload **berat + multi-PC** | **Hybrid** | MPI menyeberang mesin + OpenMP di tiap mesin |

**Inti pelajaran:** paralelisme **bukan sihir**. Ia menambah **overhead** (sinkronisasi
untuk OpenMP, komunikasi untuk MPI). Paralel baru menguntungkan kalau **kerja bergunanya
cukup besar** untuk menutup overhead itu. Untuk Brick limit 50, MPI belum menang — butuh
workload lebih berat dan/atau multi-node nyata agar overhead komunikasi teramortisasi.

> **Istilah — teramortisasi (amortized):** biaya tetap (overhead) "terbayar" karena
> tersebar di kerja berguna yang jauh lebih besar, sehingga rata-ratanya jadi kecil.

---

## 5. Keterbatasan jujur (bagus untuk slide "Saran")

- `single`/`multiple` dibatasi `--limit`; minta banyak resep pada elemen ber-percabangan
  besar jadi mahal.
- `--mode all` hanya ambil resep langsung + subtree representatif terpendek, bukan semua
  kombinasi.
- Paralelisme MPI bergantung jumlah task dari `--split-depth`; target dengan sedikit cabang
  → paralelisme terbatas.
- Cache MPI lokal per rank (belum distributed/shared cache).
- Render gambar butuh `dot` (Graphviz) di PATH.
- Pada eksperimen ini MPI belum menang — butuh workload lebih berat / multi-node nyata.

---

**Lanjut ke:** [16-glossary-dan-faq.md](16-glossary-dan-faq.md) — kamus istilah lengkap &
pertanyaan yang sering muncul. →

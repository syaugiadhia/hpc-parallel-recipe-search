# 16 — Glossary & FAQ

> **Prasyarat baca:** idealnya sudah baca file-file sebelumnya, tapi ini juga bisa dipakai
> sebagai rujukan lepas.
> **Setelah ini kamu paham:** semua istilah teknis di satu tempat, plus jawaban
> pertanyaan-pertanyaan yang sering bikin bingung.

---

## A. Kamus istilah (alfabetis)

**A\* / best-first search** — strategi pencarian yang selalu mengeksplorasi kandidat dengan
"tebakan biaya" (estimate) terkecil dulu. Di proyek: BFS lazy memakai `estimate` kedalaman.
→ [05](05-bfs-lazy-detail.md)

**Address space (ruang alamat)** — peta alamat memori milik satu proses. Alamat hanya
bermakna di dalam proses & mesin itu — inti kenapa OpenMP tak bisa multinode. → [12](12-kenapa-openmp-tidak-multinode.md)

**Amdahl, Hukum** — bagian pekerjaan yang tak bisa diparalelkan membatasi speedup maksimum,
berapa pun pekerja ditambah. → [06](06-konsep-paralel-hpc.md)

**Baseline** — patokan serial untuk menghitung speedup (`--baseline-ms`). → [07](07-serial.md)

**BFS (Breadth-First Search)** — menjelajah melebar: lapisan demi lapisan dari yang dekat
tujuan. Cenderung temukan resep terpendek dulu. Pakai antrian. **Default proyek.**

**Canonical (kanonik)** — bentuk "resmi" nama elemen (ejaan pertama yang terlihat); banyak
ejaan input → satu nama output. → [03](03-struktur-data.md)

**CompactNode** — representasi padat satu node pohon (pakai `int nameId` & indeks, bukan
string & pointer) agar pohon murah disalin saat BFS. → [03 §3](03-struktur-data.md#3--compactnode--pohon-padat-untuk-pencarian-cepat)

**Communication time (`communicationMs`)** — total waktu kirim/terima pesan MPI (akumulasi
lintas rank); bisa > wall-clock. → [09 §7](09-mpi.md#7-mengukur-waktu-komunikasi-kenapa-bisa--wall-clock)

**DFS (Depth-First Search)** — menjelajah mendalam: susuri satu jalan sampai mentok baru
mundur. Cenderung ikut urutan data. Pakai rekursi.

**Deadlock** — dua pihak saling menunggu selamanya. Di multi-PC MPI sering karena adapter
jaringan salah. → [11](11-multinode-lan.md)

**Deduplikasi (dedup)** — membuang resep duplikat berdasarkan `signature` struktur.
→ [03 §4](03-struktur-data.md#4--signature--kunci-deduplikasi)

**Distributed memory** — tiap proses RAM sendiri, berbagi lewat pesan. Model MPI.
→ [06 §3](06-konsep-paralel-hpc.md#3-shared-memory-vs-distributed-memory-konsep-paling-penting)

**Efisiensi (E)** — `E = speedup / jumlah_worker`. Seberapa terpakai tiap pekerja. → [06](06-konsep-paralel-hpc.md)

**Estimate** — perkiraan kedalaman akhir sebuah pohon parsial = `1 + max(estimate anak)`;
memandu prioritas BFS. → [05](05-bfs-lazy-detail.md)

**Fork/join** — membuat tim thread (fork) lalu menyatukannya (join). Tiap `#pragma omp
parallel for` = satu siklus, ada biayanya. → [08 §5](08-openmp.md#5-kenapa-syarat-batchsize--2threadcount)

**Frontier** — kumpulan pohon parsial yang sedang dipertimbangkan (di `priority_queue`).
→ [05](05-bfs-lazy-detail.md)

**Heuristik** — tebakan cerdas & murah yang memandu pencarian (di sini: `shortestDepths`).
→ [04 §3](04-searching-overview.md#3-heuristik-kedalaman-terpendek-shortestdepths)

**Hybrid** — MPI antar-proses/mesin + OpenMP antar-thread dalam tiap proses. → [10](10-hybrid-mpi-openmp.md)

**Interning** — mengganti string mahal dengan id integer murah (`idOf`/`nameOfId`); penting
untuk paralel. → [03 §1.3](03-struktur-data.md#13--interning-idof-dan-nameofid-kunci-performa-paralel)

**LAN** — jaringan lokal (mis. Wi-Fi/hotspot bersama). → [11](11-multinode-lan.md)

**Load balancing** — menyeimbangkan beban antar pekerja (OpenMP pakai `schedule(dynamic)`).
→ [08](08-openmp.md)

**Master–worker** — pola MPI: rank 0 (master) membagi task & menggabung; rank lain (worker)
mengerjakan. → [09](09-mpi.md)

**Memoization / cache** — menyimpan hasil sub-perhitungan agar tak dihitung ulang
(`--trace-mode memo`). → [04 §6](04-searching-overview.md#6-memoization---trace-mode-memo-vs-full)

**MPI (Message Passing Interface)** — standar paralel berbasis kirim pesan antar proses;
bisa multinode. → [09](09-mpi.md)

**`MPI_Gather`** — kumpulkan data dari semua rank ke rank 0 (dipakai untuk hostname).
→ [11 §4](11-multinode-lan.md#4-bukti-rank-benar-benar-di-mesin-lain-collectrankhostnames)

**`MPI_Probe`** — "intip" pesan masuk tanpa langsung menerimanya; master pakai
`ANY_SOURCE, ANY_TAG`. → [09 §4](09-mpi.md#4-protokol-komunikasi-4-tag-pesan)

**`MPI_Wtime`** — stopwatch presisi MPI (detik). → [09 §7](09-mpi.md#7-mengukur-waktu-komunikasi-kenapa-bisa--wall-clock)

**Multi-homed** — satu komputer punya banyak alamat/adapter jaringan; sering bikin MPI hang.
→ [11 §5](11-multinode-lan.md#5-tantangan-jaringan-dan-kenapa-terjadi)

**Multinode** — jalan di banyak komputer sekaligus. → [11](11-multinode-lan.md)

**Node (graf)** — titik di graf/pohon (di sini = elemen). Jangan bingung dengan "node" =
komputer di konteks multinode.

**OpenMP** — paralelisme thread shared-memory dalam satu proses (`#pragma omp ...`). Tak
bisa multinode. → [08](08-openmp.md), [12](12-kenapa-openmp-tidak-multinode.md)

**Overhead** — kerja tambahan akibat paralel (sinkronisasi / komunikasi). → [06 §7](06-konsep-paralel-hpc.md#7-overhead-harga-yang-harus-dibayar-paralel)

**POD / trivially-copyable** — tipe data "polos" (cuma angka) yang bisa disalin secepat
menyalin byte. `CompactNode` dibuat begini. → [03 §3](03-struktur-data.md#3--compactnode--pohon-padat-untuk-pencarian-cepat)

**Priority_queue** — antrian yang selalu keluarkan elemen "terbaik" dulu (estimate terkecil).
→ [05 §3](05-bfs-lazy-detail.md#3-antrian-prioritas-priority_queue--frontiernodecompare)

**Proses** — program berjalan dengan RAM sendiri. → [06 §2](06-konsep-paralel-hpc.md#2-proses-vs-thread)

**Pure function (fungsi murni)** — hasilnya hanya bergantung input, tanpa efek samping;
aman diparalelkan. → [05 §6](05-bfs-lazy-detail.md#6-kenapa-desain-ini-bagus-untuk-diparalelkan)

**Race condition** — bug saat thread menulis data sama tanpa koordinasi. Dihindari dengan
ekspansi yang tak menulis data bersama. → [08 §4](08-openmp.md#4-detail-teknis-bagaimana-batch-diparalelkan)

**Rank** — nomor identitas proses MPI (0 = master). → [06 §4](06-konsep-paralel-hpc.md#4-istilah-mpi-rank-worker-slot)

**RecipeGraph** — graf resep: elemen → daftar pasangan bahan. → [03 §1](03-struktur-data.md#1-recipegraph--graf-resep)

**RecipeTree** — pohon resep ramah-manusia (output JSON/ASCII/gambar). → [03 §2](03-struktur-data.md#2-recipetree--pohon-resep-hasil-yang-ditampilkan)

**Relaksasi titik-tetap** — ulangi memperbaiki nilai sampai stabil (dipakai
`shortestDepths`). → [04 §3](04-searching-overview.md#3-heuristik-kedalaman-terpendek-shortestdepths)

**Serialisasi** — mengubah struktur data jadi byte/teks (JSON) untuk dikirim; balikannya
deserialisasi. → [09 §5](09-mpi.md#5-serialisasi-kenapa-pakai-json)

**Shared memory** — RAM bersama untuk banyak thread. Model OpenMP. → [06 §3](06-konsep-paralel-hpc.md#3-shared-memory-vs-distributed-memory-konsep-paling-penting)

**Signature** — string struktur pohon (anak disortir) untuk dedup. → [03 §4](03-struktur-data.md#4--signature--kunci-deduplikasi)

**Siklus** — jalur melingkar di graf; di pencarian resep harus dicegah (`pathContainsId`).
→ [05 §4](05-bfs-lazy-detail.md#4-loop-utama-baris-demi-baris-konsep)

**Slot** — jumlah proses MPI yang boleh jalan di satu host. → [11](11-multinode-lan.md)

**smpd** — daemon MS-MPI yang meluncurkan proses MPI di tiap PC. → [11 §5](11-multinode-lan.md#5-tantangan-jaringan-dan-kenapa-terjadi)

**Speedup (S)** — `S = T_serial / T_paralel`. → [06 §5](06-konsep-paralel-hpc.md#5-mengukur-keberhasilan-paralel-speedup--efisiensi)

**Split-depth** — seberapa dalam master memecah target jadi task MPI. → [09 §3](09-mpi.md#3-membuat-task---split-depth)

**Terminal** — elemen titik berhenti pencarian (4 dasar + Time). → [01](01-pengenalan-dan-domain.md)

**Thread** — pekerja dalam satu proses, berbagi RAM. → [06 §2](06-konsep-paralel-hpc.md#2-proses-vs-thread)

**Thread-profile** — spesifikasi `proses×thread` per host untuk hybrid (mis. `1x2,2x4`).
→ [10 §2](10-hybrid-mpi-openmp.md#2-dua-cara-mengatur-thread)

**Tier** — tingkat elemen Little Alchemy (untuk pengelompokan & validasi). → [03 §6](03-struktur-data.md#6-tiercatalog--katalog-tier-resmi)

**Total workers** — total unit kerja (MPI: jumlah rank; hybrid: jumlah semua thread).
→ [10 §5](10-hybrid-mpi-openmp.md#5-total-worker--efisiensi)

**Wall-clock** — waktu nyata dari mulai sampai selesai. → [09 §7](09-mpi.md#7-mengukur-waktu-komunikasi-kenapa-bisa--wall-clock)

---

## B. FAQ

### "Kenapa cache hits sering 0?"
Pada Brick limit 50, jalur ekspansi BFS yang diambil kebetulan tak menghasilkan reuse, jadi
`cacheHits = 0`. Selain itu, jalur **BFS lazy** memakai dedup `seenFinalSignatures` (bukan
`memo_` per elemen), jadi statistik cache lebih relevan di jalur DFS/bounded. Mekanismenya
tetap ada dan berguna untuk target lebih kompleks. → [04 §6](04-searching-overview.md#6-memoization---trace-mode-memo-vs-full)

### "Kenapa pohon resep selalu biner (2 anak)?"
Karena aturan Little Alchemy: tiap resep menggabungkan **tepat dua** bahan. `JsonLoader`
bahkan menolak resep yang bukan 2 bahan. → [01](01-pengenalan-dan-domain.md)

### "Kenapa semua rank MPI baca `recipes.json` sendiri, bukan master broadcast?"
Graf besar (720 elemen); mengirimnya lewat jaringan mahal. Lebih murah tiap rank baca file
lokal. Konsekuensi: file harus ada di path sama di semua PC. → [09 §2](09-mpi.md#2-setiap-rank-baca-datanya-sendiri-keputusan-desain-penting)

### "Kenapa communication time bisa LEBIH BESAR dari total time?"
`communicationMs` dijumlahkan **lintas semua rank** dan banyak operasi, sedangkan total time
adalah wall-clock satu proses (master). Karena banyak proses paralel menjumlahkan waktunya,
totalnya bisa melebihi wall-clock. Disengaja, untuk menunjukkan total biaya koordinasi.
→ [09 §7](09-mpi.md#7-mengukur-waktu-komunikasi-kenapa-bisa--wall-clock)

### "Kenapa MPI lokal lebih lambat dari serial?"
Overhead komunikasi (round-trip task, serialisasi JSON, merge terpusat) mendominasi untuk
workload ringan, melebihi waktu kerja sebenarnya. → [15](15-benchmark-dan-interpretasi.md)

### "Kenapa OpenMP tak bisa pakai banyak komputer?"
Model shared-memory: thread harus akses RAM bersama, dan RAM komputer lain terpisah secara
fisik. Multinode butuh message passing (MPI). → [12](12-kenapa-openmp-tidak-multinode.md)

### "Apa beda thread, proses, rank, slot, worker?"
- **Thread**: pekerja dalam 1 proses (berbagi RAM).
- **Proses**: program dengan RAM sendiri.
- **Rank**: nomor identitas proses MPI.
- **Slot**: jatah proses per komputer.
- **Worker**: unit kerja efektif (rank, atau total thread di hybrid).
→ [06](06-konsep-paralel-hpc.md)

### "Output keempat mode apakah sama?"
Ya — semua memakai engine `common` yang sama. Untuk konfigurasi setara (mis. Brick limit
50), keempatnya menemukan 50 resep yang sama. Yang beda hanya kecepatan & cara membaginya.
→ [02 §3](02-arsitektur.md#3-prinsip-arsitektur-1-engine-banyak-mode)

### "Apa beda `--mode all` dengan `--mode multiple --limit 999`?"
`all` = semua resep **langsung** unik target dari JSON, tiap bahan diekspansi dengan **satu
subtree terpendek** — bukan semua kombinasi. `multiple` = jelajah BFS sampai N resep
(termasuk variasi subtree). → [04 §4](04-searching-overview.md#4-mode-pencarian-single--multiple--all)

### "Saya di Windows, build/run gimana?"
Lihat [14-cli-build-run.md](14-cli-build-run.md) untuk build, dan
[11-multinode-lan.md](11-multinode-lan.md) + [tutor_multi_pc.md](tutor_multi_pc.md) untuk
MS-MPI multi-PC.

---

Selesai! Kembali ke [README.md](README.md) untuk peta dokumen. Selamat — kamu sudah
menelusuri seluruh proyek dari domain sampai detail paralelisme. 🎉

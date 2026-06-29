# 09 — Mode MPI (Paralel Distributed-Memory, Master–Worker)

> **Prasyarat baca:** [06-konsep-paralel-hpc.md](06-konsep-paralel-hpc.md) (proses,
> distributed memory, rank), [04 §8](04-searching-overview.md#8-tabel-ringkas-fungsi-mana-dipanggil-kapan)
> (`completePartial`).
> **Setelah ini kamu paham:** pola master-worker, bagaimana pekerjaan dipecah jadi task,
> protokol pesan 4-tag, bagaimana hasil digabung, dan kenapa waktu komunikasi bisa besar.

Kode: [src/mpi/main_mpi.cpp](src/mpi/main_mpi.cpp),
[MpiMaster.cpp](src/mpi/MpiMaster.cpp), [MpiWorker.cpp](src/mpi/MpiWorker.cpp).

---

## 1. Intuisi: bos & para pekerja

> **Istilah — MPI (Message Passing Interface):** standar untuk membuat banyak **proses**
> bekerja sama dengan **saling berkirim pesan**. Tiap proses bisa di komputer yang sama
> atau berbeda. Library populer: OpenMPI, MPICH, MS-MPI (Windows).

Proyek ini memakai pola **master–worker**:

- **Master** = **rank 0** (si bos). Tidak ikut mencari; tugasnya: membuat daftar
  pekerjaan (task), membagikannya, dan menggabungkan hasil.
- **Worker** = **rank 1, 2, 3, ...** (para pekerja). Tiap worker minta task, mengerjakannya
  sendiri, kirim hasil, minta lagi.

```
            ┌─────────────┐
            │  Master     │  rank 0
            │  (bagi task,│
            │   gabung)   │
            └──┬───┬───┬──┘
       task ↙  │   │   │  ↖ result
        ┌──────▼┐ ┌▼────┐ ┌▼─────┐
        │Worker1│ │Wkr2 │ │Wkr3  │   rank 1,2,3
        └───────┘ └─────┘ └──────┘
```

> **Istilah — task:** satu satuan pekerjaan yang bisa dikerjakan independen. Di sini, satu
> task = **satu pohon parsial** yang perlu diselesaikan.

---

## 2. Setiap rank baca datanya sendiri (keputusan desain penting)

Lihat [main_mpi.cpp:320](src/mpi/main_mpi.cpp#L320): **semua rank** (master & worker)
memanggil `JsonLoader::loadFromFile(options.dataPath)` masing-masing.

Kenapa tidak master mengirim graf ke worker? Karena graf besar (720 elemen). Mengirimnya
lewat jaringan akan mahal. Lebih murah tiap rank membaca file `recipes.json` sendiri.

**Konsekuensi untuk multi-PC:** file `recipes.json` & `tiers.json` **harus ada di path
yang sama di setiap komputer**. Ini ditegaskan di [11-multinode-lan.md](11-multinode-lan.md).
Ini juga contoh nyata "distributed memory": tak ada graf bersama — tiap proses punya
salinannya sendiri di RAM-nya sendiri.

---

## 3. Membuat task: `--split-depth`

Master memecah target jadi banyak task dengan **mengekspansi sampai kedalaman tertentu**.
Lihat `buildMpiTasks` → `expandForSplit`
([MpiMaster.cpp:47-109](src/mpi/MpiMaster.cpp#L47-L109)).

### Intuisi
`--split-depth N` = "buka N lapis dari target, jadikan tiap pohon parsial sebagai task".

```
Target: Brick

--split-depth 1  →  task = pohon parsial sedalam 1 lapis:
   Task A: Brick(Mud?, Fire)     Task B: Brick(Clay?, Fire)   ...
   (tiap resep langsung Brick jadi satu task; "?" = daun yang worker harus selesaikan)

--split-depth 2  →  master buka satu lapis LAGI dari tiap bahan → lebih banyak task
```

### Kenapa split-depth penting?
Makin banyak task → makin banyak worker kebagian kerja. Kalau task lebih sedikit dari
worker, sebagian worker nganggur. Tapi terlalu banyak task → overhead. Ada batas `maxTasks`
([MpiMaster.cpp:279](src/mpi/MpiMaster.cpp#L279)) agar tak meledak:

```cpp
maxTasks = std::max(64, limit * worldSize * std::max(2, splitDepth + 1));
```

Mode `all` memakai jalur khusus `buildDirectRecipeTasks` (satu task per resep langsung
unik), dan split-depth diabaikan.

---

## 4. Protokol komunikasi: 4 tag pesan

> **Istilah — tag (MPI):** label angka pada tiap pesan, supaya penerima tahu *jenis* pesan
> apa yang datang. Di kode: `MPI_TAG_REQUEST`, `MPI_TAG_TASK`, `MPI_TAG_RESULT`,
> `MPI_TAG_STOP` (didefinisikan di `MpiMaster.hpp`).

Alur percakapan master↔worker:

```
worker → master : REQUEST   "Bos, beri saya kerjaan"
master → worker : TASK      "Ini satu task (pohon parsial, dalam JSON)"
        atau
master → worker : STOP      "Sudah habis / cukup. Berhenti."
worker → master : RESULT    "Ini hasilnya (resep + statistik, dalam JSON)"
```

### Diagram urutan (sequence)

```
 Worker 1                 Master (rank 0)                Worker 2
    │── REQUEST ──────────────►│                            │
    │◄──────── TASK A ─────────│◄──────────── REQUEST ──────│
    │ (kerjakan A...)          │── TASK B ─────────────────►│
    │── RESULT A ─────────────►│ (kerjakan B...)            │
    │◄──────── TASK C ─────────│◄──────────── RESULT B ─────│
    │ ...                      │── TASK D ─────────────────►│
    │── REQUEST ──────────────►│                            │
    │◄──────── STOP ───────────│ (task habis)               │
    │ (berhenti)               │── STOP ───────────────────►│
                               │ (semua worker stop → gabung & selesai)
```

### Loop master
Lihat [MpiMaster.cpp:307-343](src/mpi/MpiMaster.cpp#L307-L343):

```cpp
while (activeWorkers > 0) {
    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);  // tunggu pesan apa pun
    if (status.MPI_TAG == MPI_TAG_REQUEST) {
        if (masih ada task && belum capai limit)
            sendString(source, MPI_TAG_TASK, taskToPayload(...));      // kirim task
        else { sendString(source, MPI_TAG_STOP, ""); --activeWorkers; }// suruh stop
    } else if (status.MPI_TAG == MPI_TAG_RESULT) {
        mergeWorkerResult(...)  // parse JSON, dedup, akumulasi statistik
    }
}
```

> **Istilah — `MPI_Probe(ANY_SOURCE, ANY_TAG)`:** "intip" pesan yang masuk dari **siapa
> pun** dengan tag **apa pun**, tanpa langsung menerimanya. Master pakai ini agar bisa
> melayani worker mana pun yang lebih dulu siap (tidak terpaku urutan).

### Loop worker
Lihat [MpiWorker.cpp:60-87](src/mpi/MpiWorker.cpp#L60-L87):

```cpp
while (true) {
    sendString(0, MPI_TAG_REQUEST, "");          // minta kerjaan
    MPI_Probe(0, MPI_ANY_TAG, &status);
    if (status.MPI_TAG == MPI_TAG_STOP) break;   // disuruh berhenti
    // terima TASK:
    RecipeTree partial = treeFromJson(task.at("partial"));
    auto result = engine.completePartial(partial, taskLimit, false);  // SELESAIKAN pohon parsial
    sendString(0, MPI_TAG_RESULT, resultToPayload(rank, taskId, result, ...));
}
```

Inti kerja worker: `engine.completePartial(partial, ...)` — melanjutkan pohon setengah
jadi sampai semua daunnya elemen dasar (lihat
[04 §8](04-searching-overview.md#8-tabel-ringkas-fungsi-mana-dipanggil-kapan)).

---

## 5. Serialisasi: kenapa pakai JSON?

Task & hasil dikirim sebagai **teks JSON** (`treeToJson`/`treeFromJson`,
`statsToJson`/`statsFromJson`).

> **Istilah — serialisasi:** mengubah struktur data di memori (pohon, statistik) menjadi
> rangkaian byte/teks yang bisa dikirim lewat jaringan, lalu di sisi penerima
> diubah balik (deserialisasi). MPI hanya bisa mengirim byte, jadi struktur kompleks harus
> diserialisasi dulu.

Ini sumber overhead: tiap task & hasil harus diubah ke/dari JSON. Itu bagian dari
`communicationMs`.

### Anti over-produksi (detail cerdas)
Payload task dibangun **saat dikirim**, bukan dipra-bangun, supaya bisa menyertakan
**sisa-limit terkini** ([MpiMaster.cpp:316-321](src/mpi/MpiMaster.cpp#L316-L321)):

```cpp
const int taskLimit = std::max(1, limit - (int)recipes.size());  // hanya minta sebanyak yg masih kurang
```

Jadi kalau master sudah punya 48 dari 50 resep, worker berikutnya hanya diminta cari 2 —
mencegah worker memproduksi berlebihan (hemat compute & komunikasi).

---

## 6. Menggabungkan hasil: `mergeWorkerResult`

Lihat [MpiMaster.cpp:161-194](src/mpi/MpiMaster.cpp#L161-L194). Saat RESULT masuk:

1. **Dedup** pakai `treeSignature` — resep yang sudah ada (dari worker lain) tak dihitung lagi.
2. **Akumulasi statistik per-rank** — `nodesVisitedByRank[rank] += ...`, dst. Ini yang
   bikin laporan bisa menunjukkan "rank 2 mengerjakan berapa node".
3. **Tambah `communicationMs`** dari worker.

Setelah semua worker STOP:
- `sortFinalRecipes` — urutkan (BFS: terpendek dulu),
- potong ke `limit`,
- jumlahkan statistik total,
- hitung speedup/efisiensi kalau `--baseline-ms` diberikan.

---

## 7. Mengukur waktu komunikasi: kenapa bisa > wall-clock?

Tiap `MPI_Send/Recv/Probe` dibungkus `MPI_Wtime()` dan diakumulasi
([MpiMaster.cpp:17-37](src/mpi/MpiMaster.cpp#L17-L37)).

> **Istilah — `MPI_Wtime()`:** stopwatch presisi tinggi dari MPI (detik). Selisih dua
> pemanggilan = durasi.
>
> **Istilah — wall-clock:** waktu nyata "di jam dinding" dari mulai sampai selesai.

`communicationMs` adalah **jumlah** waktu komunikasi **lintas semua rank** dan **banyak
operasi**. Karena dijumlahkan dari banyak proses paralel, totalnya bisa **lebih besar dari
wall-clock satu proses**. Ini **disengaja**: tujuannya menunjukkan *total biaya koordinasi*
yang dibayar sistem, bukan durasi satu rank. (Lihat FAQ [16](16-glossary-dan-faq.md).)

---

## 8. Kalau cuma 1 proses?

Kalau `worldSize <= 1`, `runMpiMaster` fallback ke engine serial biasa
([MpiMaster.cpp:266-276](src/mpi/MpiMaster.cpp#L266-L276)) — tak ada worker untuk diajak
kerja sama, jadi kerjakan sendiri.

---

## 9. Cara menjalankan (lokal, banyak proses di 1 PC)

```bash
mpiexec -n 4 build/alchemy_mpi --data data/recipes.json --tiers data/tiers.json \
    --target Brick --algorithm bfs --mode multiple --limit 50 \
    --split-depth 2 --baseline-ms 92.69 --output results/demo_mpi4
```

`-n 4` = 4 proses (1 master + 3 worker). Untuk **multi-komputer**, lihat
[11-multinode-lan.md](11-multinode-lan.md).

> **Istilah — `mpiexec` / `mpirun`:** program peluncur MPI. Ia menyalakan banyak salinan
> executable-mu sebagai rank 0..n-1 dan menghubungkan mereka.

---

**Lanjut ke:** [10-hybrid-mpi-openmp.md](10-hybrid-mpi-openmp.md) — menggabungkan MPI
(antar-proses) dengan OpenMP (antar-thread) sekaligus. →

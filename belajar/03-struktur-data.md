# 03 — Struktur Data Inti

> **Prasyarat baca:** [02-arsitektur.md](02-arsitektur.md).
> **Setelah ini kamu paham:** semua "wadah data" penting di engine — graf resep, pohon
> resep, representasi padat `CompactNode`, trik **interning**, dan **signature** untuk
> deduplikasi. Ini fondasi untuk memahami algoritma di file 04–05.

---

## 1. `RecipeGraph` — graf resep

Lihat [RecipeGraph.hpp](src/common/RecipeGraph.hpp) dan `RecipeGraph.cpp`.

### 1.1 Intuisi

Ini "buku resep" raksasa. Tanya: *"Brick bisa dibuat dari apa saja?"* → dia kembalikan
daftar pasangan bahan. Tanya elemen dasar seperti *"Water dibuat dari apa?"* → dia
kembalikan **kosong** (karena Water terminal, titik berhenti).

### 1.2 Detail teknis

Isi utama: `recipesByResult_` = map `nama elemen → vector<pasangan bahan>`.

```cpp
recipesByResult_["Brick"] = { (Mud, Fire), (Clay, Fire), ... }
```

Fitur penting:

- **Case-insensitive (tak peduli huruf besar/kecil).** `normalize()` me-*lowercase* nama
  untuk dijadikan kunci. Tapi *nama kanonik* (ejaan resmi yang ditampilkan) adalah ejaan
  pertama yang terlihat. Jadi kamu boleh ketik `brick`, `BRICK`, `Brick` — semua menuju
  elemen yang sama, dan output tetap pakai ejaan asli.
  > **Istilah — kanonik:** bentuk "resmi/baku" yang dipilih sebagai perwakilan. Banyak
  > ejaan input → satu nama kanonik output.

- **Pasangan tak berurutan.** `A+B` dianggap sama dengan `B+A`. `unorderedPairKey`
  mengurutkan kedua bahan sebelum dijadikan kunci, sehingga resep duplikat (mis. menulis
  `(Mud,Fire)` dan `(Fire,Mud)`) ditolak saat `addRecipe`.

- **4 dasar = basic + terminal** otomatis dari konstruktor. `markTerminal()` dipakai
  `TierCatalog` untuk menandai elemen lain (mis. Time).

- `recipesFor(name)` mengembalikan **kosong** kalau elemen terminal → ini yang
  menghentikan ekspansi pencarian.

### 1.3 ★ Interning: `idOf` dan `nameOfId` (kunci performa paralel)

Ini trik penting yang gampang terlewat. Tiap elemen diberi sebuah **id integer** stabil
(posisinya di `insertionOrder_`).

> **Istilah — interning:** mengganti objek "mahal" (di sini: `std::string` nama elemen,
> yang panjang & butuh alokasi memori) dengan "tiket" murah berupa **angka integer**.
> Membandingkan dua angka jauh lebih cepat daripada membandingkan dua string, dan angka
> bisa disalin tanpa alokasi memori.

Kenapa ini penting untuk paralel? Lihat `CompactNode` di bawah. Karena id dibangun
**sekali saat load** dan **read-only saat search**, fungsi `idOf`/`nameOfId` aman dipanggil
dari banyak thread sekaligus tanpa penguncian (lihat komentar di
[RecipeGraph.hpp:29-32](src/common/RecipeGraph.hpp#L29-L32)).

---

## 2. `RecipeTree` — pohon resep (hasil yang ditampilkan)

Lihat [RecipeTree.hpp](src/common/RecipeTree.hpp).

```cpp
struct RecipeTree {
    std::string name;            // nama elemen di node ini
    bool basic = false;          // true kalau ini elemen dasar/terminal (daun)
    bool sharedRef = false;      // utk visual: "sudah digambar di tempat lain"
    bool truncated = false;      // utk visual: dipotong karena terlalu dalam
    std::string refId;           // id rujukan saat sharedRef
    std::vector<RecipeTree> children;  // 0 anak (daun) atau 2 anak
};
```

Ini adalah bentuk **yang ramah manusia**: bersarang, pakai nama string. Dipakai untuk
output JSON, gambar, dan ASCII. Fungsi-fungsi pentingnya:

- `treeDepth(tree)` — kedalaman pohon. Dipakai untuk mengurutkan resep "terpendek dulu".
- `treeSignature(tree)` — **tanda tangan struktur** (lihat §4). Untuk deduplikasi.
- `treeToJson` / `treeFromJson` — ubah ke/dari JSON. **Dipakai MPI** untuk mengirim task
  & hasil antar proses (lihat [09-mpi.md](09-mpi.md)).
- `treeToAscii` — render pohon ke teks (yang kamu lihat di ringkasan terminal).
- `deduplicateTrees(trees, limit)` — buang pohon dengan signature sama, hormati batas.

---

## 3. ★ `CompactNode` — pohon "padat" untuk pencarian cepat

Lihat [Search.cpp:25-39](src/common/Search.cpp#L25-L39). Ini **bukan** struktur yang
ditampilkan; ini bentuk internal yang dipakai BFS supaya kencang.

```cpp
struct CompactNode {
    int nameId = -1;   // id elemen (BUKAN string!) ← hasil interning
    int left = -1;     // indeks anak kiri di dalam array (bukan pointer)
    int right = -1;    // indeks anak kanan
    int parent = -1;   // indeks induk
    bool basic = false;
    int estimate = ...; // perkiraan kedalaman akhir (untuk best-first)
};
```

### Kenapa dibuat seperti ini? (intuisi)

Algoritma BFS menyimpan **ribuan "pohon parsial"** (pohon setengah jadi) dan terus
**menyalin** mereka. Kalau tiap node berisi `std::string` nama, menyalin satu pohon =
menyalin & mengalokasi banyak string = lambat dan boros RAM.

Dengan `CompactNode`:

- nama diganti `int nameId` (interning) → tak ada string,
- anak/induk diganti **indeks integer** ke dalam satu array `std::vector<CompactNode>` →
  tak ada pointer.

Akibatnya `CompactNode` jadi **trivially-copyable** (POD — Plain Old Data). Menyalin
seluruh pohon parsial = menyalin satu blok memori (`memcpy`), bukan ratusan alokasi.

> **Istilah — trivially-copyable / POD:** tipe data "polos" yang isinya cuma angka-angka,
> tanpa pointer atau objek yang butuh penanganan khusus saat disalin. Menyalinnya
> secepat menyalin byte mentah.

> **Komentar asli di kode** ([Search.cpp:21-24](src/common/Search.cpp#L21-L24)):
> *"Ini inti optimasi paralel: copy jauh lebih murah, RAM turun, dan ekspansi tidak lagi
> memory-bandwidth-bound."* Artinya: setelah optimasi ini, yang membatasi kecepatan bukan
> lagi seberapa cepat RAM bisa dibaca/ditulis.

Konversi dua arah:
- `compactToRecipeTree(...)` — `CompactNode` array → `RecipeTree` ramah-manusia (saat hasil siap di-emit).
- `appendCompactTree(...)` / `makeCompactLeaf(...)` — `RecipeTree` → `CompactNode`.

---

## 4. ★ Signature — kunci deduplikasi

Masalah: dua pohon resep bisa **identik secara struktur** tapi ditemukan lewat jalur
berbeda, atau dengan urutan anak terbalik (Mud+Fire vs Fire+Mud). Kita ingin
menganggapnya **satu resep**, bukan dua.

Solusi: hitung **signature** — string yang merepresentasikan struktur pohon, dengan
**anak diurutkan** sehingga urutan tidak penting.

- `treeSignature(tree)` ([RecipeTree](src/common/RecipeTree.hpp)) bekerja pada `RecipeTree`.
- `compactSignature(nodes, index)` ([Search.cpp:218-236](src/common/Search.cpp#L218-L236))
  versi cepat di atas `CompactNode` (pakai `nameId`, lalu mengurutkan signature anak).

Contoh bentuk signature: `12(3(),7(...))` — angka `nameId`, kurung berisi signature anak
yang sudah disortir. Dua pohon dengan signature sama = duplikat → salah satu dibuang.

> **Istilah — deduplikasi (dedup):** membuang duplikat. Di sini: dari banyak pohon yang
> ditemukan, hanya simpan yang strukturnya unik.

---

## 5. `JsonLoader` — membaca data resep

Lihat `JsonLoader.cpp`. Mendukung **3 format** input JSON:

- **Format A** — objek: `{"Brick": [["Mud","Fire"], ...]}`
- **Format B** — array: `[{"result":"Brick","ingredients":["Mud","Fire"]}, ...]`
- **Format C** (dipakai proyek ini) — array:
  `[{"name":"Brick","recipes":[{"elements":["Mud","Fire"]}]}]`

Aturan keras: tiap resep **wajib tepat dua** bahan string. Kalau tidak → error. Ini
menjamin invariannya (pohon biner) sejak data dimuat.

---

## 6. `TierCatalog` — katalog tier resmi

Lihat [TierCatalog.hpp](src/common/TierCatalog.hpp). Memuat `data/tiers.json`:

- `starter` (4 dasar), `special` (Time), `tier1..tier15` (daftar elemen dari Fandom wiki).
- `validateAgainstGraph(graph)` — untuk data bawaan mengharuskan **720 elemen unik** dan
  semua namanya ada di data resep. (Strict saat `--list-elements`; cuma warning saat search.)
- `applyTerminals(graph)` — menandai starter + special sebagai terminal di graf.
- `filterElements(tier, filter)` — dipakai fitur `--list-elements` untuk membantu kamu
  menemukan nama target yang valid.

> **Istilah — tier:** "tingkat" elemen di Little Alchemy (tier1 = dekat dasar, tier15 =
> jauh/kompleks). Di sini dipakai untuk pengelompokan & validasi, bukan untuk pencarian.

---

## 7. `Statistics` (`SearchStats`) — metrik

Lihat [Statistics.hpp](src/common/Statistics.hpp). Struct ini menampung **semua angka
yang diukur** saat pencarian:

| Field | Arti |
|---|---|
| `timeMs` | waktu eksekusi (wall-clock) |
| `communicationMs` | waktu komunikasi MPI (akumulasi kirim/terima) |
| `nodesVisited` | berapa node graf dijelajahi (ukuran beban kerja) |
| `cacheHits`, `cacheEntries` | statistik memoization |
| `tasksProcessed` | berapa task MPI dikerjakan |
| `processes`, `threadsPerProcess`, `totalWorkers` | konfigurasi paralel |
| `speedup`, `efficiency` | metrik performa (lihat [06](06-konsep-paralel-hpc.md)) |
| `...ByRank` (vektor) | versi **per-rank** untuk MPI (siapa kerja berapa) |
| `rankHostnames` | nama komputer tiap rank (bukti multi-PC) |

`statsToJson`/`statsFromJson` dipakai MPI: worker mengirim statistiknya sebagai JSON ke
master, master menggabungkannya (`addStats`). Detail di [09-mpi.md](09-mpi.md).

---

## 8. `Visualizer` — menulis output

Lihat `Visualizer.cpp`. Menghasilkan:

- **JSON** (selalu) — bentuk kanonik hasil + statistik.
- **DOT + PNG/SVG** (hanya kalau `--render full`) — gambar graf via Graphviz (`dot`).
  - Mode visual `full`: gambar tiap subtree penuh.
  - Mode visual `shared`: kemunculan pertama digambar penuh, kemunculan berikut jadi
    rujukan (`shared, see Recipe N`) supaya gambar tak meledak.
  - `--max-visual-depth` memotong graf besar (node dilabeli `truncated`).

Detail format output di [14-cli-build-run.md](14-cli-build-run.md) dan
[15-benchmark-dan-interpretasi.md](15-benchmark-dan-interpretasi.md).

---

**Lanjut ke:** [04-searching-overview.md](04-searching-overview.md) — peta semua
algoritma pencarian di engine sebelum kita menyelam ke BFS. →

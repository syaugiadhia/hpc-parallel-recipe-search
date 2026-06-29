# 05 — BFS Lazy Frontier (Deep Dive: Jantung Proyek)

> **Prasyarat baca:** [03-struktur-data.md](03-struktur-data.md) (CompactNode, signature)
> dan [04-searching-overview.md](04-searching-overview.md) (heuristik, estimate).
> **Setelah ini kamu paham:** persis bagaimana algoritma pencarian utama bekerja, baris
> demi baris konsepnya, lengkap contoh jejak. Ini fungsi terpenting di seluruh proyek.

Kode: `expandPartialBfsLazy` di
[Search.cpp:588-675](src/common/Search.cpp#L588-L675).

---

## 1. Intuisi besar: "menumbuhkan banyak pohon setengah jadi sekaligus"

Bayangkan kamu menanam banyak bibit pohon resep secara bersamaan. Tiap bibit adalah
**pohon parsial** (setengah jadi): akarnya = target, tapi sebagian daunnya masih "belum
selesai" (masih elemen non-dasar yang perlu diuraikan lagi).

Kamu menyimpan semua bibit ini di sebuah **antrian prioritas**. Aturannya:

> "Selalu rawat dulu bibit yang **paling menjanjikan** — yaitu yang perkiraan resep
> akhirnya paling **pendek**."

Tiap putaran:
1. Ambil bibit paling menjanjikan.
2. Kalau semua daunnya sudah elemen dasar → **pohon selesai!** Catat sebagai satu resep
   (kalau belum pernah ketemu yang identik).
3. Kalau masih ada daun "belum selesai" → pilih satu, lihat semua resep untuk daun itu,
   dan untuk tiap resep buat **bibit baru** (salinan pohon dengan daun itu diuraikan).
   Masukkan semua bibit baru kembali ke antrian.

Ulangi sampai sudah mengumpulkan cukup resep (`limit`) atau antrian habis.

Kenapa disebut **"lazy"** (malas)? Karena kita **tidak** menguraikan semua daun sekaligus.
Tiap langkah hanya menguraikan **satu** daun dari satu bibit. Pertumbuhan terjadi
sedikit-sedikit, dipandu prioritas. Ini menghemat kerja: bibit yang ternyata buruk tak
pernah diuraikan sampai habis.

> **Istilah — frontier (perbatasan):** kumpulan keadaan yang "sedang dipertimbangkan"
> tapi belum selesai diproses. Di sini, frontier = antrian berisi semua pohon parsial.

---

## 2. Anatomi satu "bibit": `FrontierNode`

Lihat [Search.cpp:34-39](src/common/Search.cpp#L34-L39).

```cpp
struct FrontierNode {
    std::vector<CompactNode> nodes;  // seluruh pohon parsial (array padat)
    std::vector<int> openLeaves;     // indeks daun yang MASIH perlu diuraikan
    int estimate;                    // perkiraan kedalaman akhir pohon ini
    std::size_t sequence;            // nomor urut lahir (untuk tie-break FIFO)
};
```

- `nodes` — pohon parsial dalam bentuk `CompactNode` (padat, cepat disalin; lihat
  [03 §3](03-struktur-data.md#3--compactnode--pohon-padat-untuk-pencarian-cepat)).
- `openLeaves` — "daftar PR": daun mana saja yang belum jadi elemen dasar dan masih bisa
  diuraikan. Kalau kosong → pohon selesai.
- `estimate` — perkiraan kedalaman akhir = `1 + max(estimate anak)`. Makin kecil makin
  menjanjikan (resep makin pendek).
- `sequence` — nomor lahir, dipakai memecah seri kalau dua bibit punya `estimate` sama
  (yang lebih dulu lahir diproses dulu → adil, FIFO).

> **Istilah — FIFO (First In First Out):** "yang datang duluan dilayani duluan", seperti
> antrian biasa.

---

## 3. Antrian prioritas: `priority_queue` + `FrontierNodeCompare`

Lihat [Search.cpp:41-48](src/common/Search.cpp#L41-L48).

```cpp
struct FrontierNodeCompare {
    bool operator()(const FrontierNode& a, const FrontierNode& b) const {
        if (a.estimate != b.estimate) return a.estimate > b.estimate;  // estimate kecil = prioritas
        return a.sequence > b.sequence;                                 // seri → yang lebih awal
    }
};
```

> **Istilah — priority_queue (antrian prioritas):** struktur yang selalu mengeluarkan
> elemen "terbaik" lebih dulu (di sini: estimate terkecil), bukan urutan masuk. C++
> mengimplementasikannya sebagai heap. Operasi ambil-terbaik dan masukkan-baru sama-sama
> cepat (logaritmik).

Inilah yang membuat algoritma ini **best-first**: bibit dengan tebakan resep terpendek
selalu dirawat lebih dulu, sehingga resep-resep pendek keluar lebih awal.

---

## 4. Loop utama, baris demi baris (konsep)

Berikut kerangka [Search.cpp:610-672](src/common/Search.cpp#L610-L672), diberi anotasi:

```cpp
while (!frontier.empty() && !limitReached(result.trees.size(), limit)) {
    auto current = frontier.top();   // (A) ambil bibit paling menjanjikan
    frontier.pop();

    if (current.openLeaves.empty()) {           // (B) pohon ini SUDAH SELESAI
        auto sig = compactSignature(current.nodes, 0);
        if (seenFinalSignatures.insert(sig).second) {   // (C) cek duplikat
            result.trees.push_back(compactToRecipeTree(graph_, current.nodes, 0));
        }
        continue;                                // lanjut ambil bibit berikutnya
    }

    int leafIndex = current.openLeaves.front();  // (D) pilih SATU daun belum-selesai
    current.openLeaves.erase(...);
    ++stats_.nodesVisited;                       // ukur beban kerja

    for (const auto& recipe : graph_.recipesFor(canonicalLeaf)) {  // (E) tiap resep daun
        // (F) DETEKSI SIKLUS: kalau bahan sudah ada di jalur menuju akar → tolak
        if (pathContainsId(... firstId) || pathContainsId(... secondId)) {
            result.cycleBlocked = true; continue;
        }
        FrontierNode expanded = current;         // (G) SALIN pohon (murah! CompactNode)
        // (H) tambahkan dua anak (kiri & kanan) ke daun tsb
        // (I) perbarui estimate daun, lalu estimate semua leluhurnya
        refreshAncestorEstimates(expanded.nodes, leafIndex);
        // (J) anak yang masih bisa diuraikan → masuk openLeaves bibit baru
        pushState(std::move(expanded));          // (K) dorong bibit baru ke frontier
    }
}
```

Mari bedah tiap titik:

- **(A) Ambil terbaik.** `frontier.top()` mengembalikan bibit dengan `estimate` terkecil.
- **(B) Pohon selesai.** `openLeaves` kosong artinya tak ada lagi PR — semua daun sudah
  elemen dasar. Ini satu resep utuh.
- **(C) Cek duplikat.** `seenFinalSignatures.insert(sig).second` bernilai `true` hanya
  kalau signature **belum pernah** dilihat. Jadi resep identik (struktur sama) tak
  dihitung dua kali. Lalu pohon padat dikonversi ke `RecipeTree` ramah-manusia.
- **(D) Pilih satu daun.** Ambil daun pertama dari `openLeaves`. (Hanya satu — inilah
  "lazy".)
- **(E) Setiap resep.** Daun (mis. "Mud") bisa punya beberapa resep. Tiap resep memunculkan
  satu cabang baru.
- **(F) Deteksi siklus.** `pathContainsId` menelusuri rantai `parent` dari daun ke akar.
  Kalau bahan resep sudah muncul di jalur itu, menambahkannya akan membuat **lingkaran**
  (mis. Brick butuh X yang butuh Brick) → cabang ditolak (`cycleBlocked`). Ini menjamin
  hasil tetap **pohon** (acyclic), bukan graf berputar.
  > **Istilah — siklus:** jalur yang berputar balik ke dirinya. Di pencarian resep, siklus
  > = ketergantungan melingkar yang tak punya dasar → harus dicegah.
- **(G) Salin pohon.** `FrontierNode expanded = current;` menyalin seluruh pohon parsial.
  Inilah kenapa `CompactNode` dibuat padat: salinan ini terjadi **sangat sering**, jadi
  harus murah.
- **(H–I) Tumbuhkan & perbarui estimate.** Dua anak ditambahkan; estimate daun jadi
  `1 + max(anak)`. Karena estimate berubah, semua **leluhur** (induk, kakek, ...) ikut
  diperbarui lewat `refreshAncestorEstimates`
  ([Search.cpp:174-182](src/common/Search.cpp#L174-L182)) — naik dari daun ke akar.
- **(J) Daftar PR baru.** Anak yang masih non-terminal & punya resep → dimasukkan ke
  `openLeaves` bibit baru (lewat `isExpandableCompactLeaf`).
- **(K) Dorong balik.** Bibit baru masuk frontier dengan `sequence` baru. Karena
  estimate-nya sudah diperbarui, posisinya di antrian otomatis sesuai prioritas.

Loop berhenti saat **jumlah resep mencapai `limit`** atau **frontier habis**.

---

## 5. Contoh jejak langkah-demi-langkah (disederhanakan)

Misal data mini (angka estimate dari `shortestDepths`):

```
Brick = Mud + Fire
Mud   = Earth + Water
Earth, Water, Fire = dasar (estimate 0)
→ estimate Mud = 1, estimate Brick = 2
```

Kita minta **1 resep** untuk `Brick`. Jejaknya:

```
Langkah 0 — frontier awal:
  Bibit#0 = { Brick }                 openLeaves=[Brick]  estimate=2
  Frontier: [#0]

Langkah 1 — ambil #0, daun "Brick" punya 1 resep (Mud,Fire):
  buat Bibit#1 = Brick(Mud?, Fire)    openLeaves=[Mud]    estimate=2
  (Fire dasar → bukan open leaf; Mud non-dasar → open leaf)
  Frontier: [#1]

Langkah 2 — ambil #1, daun "Mud" punya 1 resep (Earth,Water):
  buat Bibit#2 = Brick(Mud(Earth,Water), Fire)   openLeaves=[]   estimate=2
  (Earth & Water dasar → tak ada open leaf lagi)
  Frontier: [#2]

Langkah 3 — ambil #2, openLeaves KOSONG → POHON SELESAI:
  signature dihitung, belum pernah dilihat → EMIT sebagai Recipe 1:
      Brick
      ├── Mud
      │   ├── Earth
      │   └── Water
      └── Fire
  result.trees.size()==1 == limit → STOP.
```

Pada target nyata seperti Brick dengan `--limit 50`, frontier bisa berisi ribuan bibit,
dan prioritas estimate-lah yang memastikan 50 resep yang keluar adalah yang terpendek.

---

## 6. Kenapa desain ini bagus untuk diparalelkan

Perhatikan langkah **(E)–(K)**: untuk satu bibit, kita memproses tiap resep daun secara
independen, dan tiap ekspansi **tidak mengubah** data bersama (hanya membaca graf,
membuat bibit baru). Sifat "tidak saling ganggu" ini yang dieksploitasi versi OpenMP:
banyak bibit diekspansi **paralel** oleh banyak thread, lalu hasilnya digabung. Itu fungsi
`expandFrontierState` (versi murni) dan `expandPartialBfsLazyOpenmp` — dibahas di
[08-openmp.md](08-openmp.md).

> **Istilah — fungsi murni (pure function):** fungsi yang hasilnya hanya bergantung pada
> input dan tidak mengubah keadaan di luar dirinya (tidak ada efek samping). Fungsi murni
> aman dijalankan banyak thread sekaligus tanpa kunci.

---

**Lanjut ke:** [06-konsep-paralel-hpc.md](06-konsep-paralel-hpc.md) — fondasi HPC
(proses, thread, shared vs distributed memory, speedup, Amdahl) sebelum membahas tiap
mode paralel. →

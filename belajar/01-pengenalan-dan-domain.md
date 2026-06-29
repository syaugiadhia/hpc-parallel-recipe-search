# 01 — Pengenalan & Domain Masalah

> **Prasyarat baca:** tidak ada. Ini titik mulai.
> **Setelah ini kamu paham:** apa yang dikerjakan program ini, bagaimana permainan
> Little Alchemy diubah jadi "graf" dan "pohon", dan kenapa masalah ini cocok dipakai
> belajar High Performance Computing (HPC).

---

## 1. Intuisi: ini program apa, sih?

Bayangkan game **Little Alchemy 2**. Kamu mulai dengan 4 "elemen dasar":

```
🌬️ Air     🌍 Earth     🔥 Fire     💧 Water
```

Kamu menggabungkan **tepat dua** elemen untuk membuat elemen baru:

```
Earth + Water = Mud
Mud   + Fire  = Brick
```

Nah, program ini menjawab pertanyaan: **"Untuk membuat elemen X, langkah-langkah
penggabungan apa saja yang diperlukan, mulai dari 4 elemen dasar?"**

Misal kamu minta `Brick`, program akan menjawab dengan sebuah **pohon resep**:

```
Brick
├── Mud
│   ├── Earth   (dasar)
│   └── Water   (dasar)
└── Fire        (dasar)
```

Artinya: *"Buat Mud dari Earth+Water, lalu gabung Mud dengan Fire jadi Brick."*

Itu inti programnya. Sederhana di permukaan — tapi di balik layar, mencari resep ini
adalah masalah **pencarian** yang bisa jadi sangat besar, dan di situlah HPC masuk.

---

## 2. Detail teknis: dari permainan ke struktur data

### 2.1 Elemen → graf dependensi

Tiap elemen hasil bisa punya **banyak** pasangan bahan (banyak cara membuatnya):

```
Brick  →  (Mud, Fire)        ← resep 1
       →  (Clay, Fire)       ← resep 2
       →  (Stone, Stone)     ← resep 3 (contoh)
       ...
```

Kita simpan ini sebagai **graf berarah** (directed graph): dari elemen hasil menunjuk ke
pasangan-pasangan bahannya.

> **Istilah — graf:** struktur data berisi "node" (titik) yang dihubungkan "edge"
> (garis). Di sini node = elemen, dan hubungan = "elemen ini bisa dibuat dari pasangan
> elemen itu". Disebut **berarah** karena hubungannya satu arah: Brick *butuh* Mud+Fire,
> bukan sebaliknya.

Di kode, ini disimpan di kelas `RecipeGraph` sebagai sebuah `map`:
nama elemen → daftar pasangan bahan. Lihat [03-struktur-data.md](03-struktur-data.md).

### 2.2 Empat elemen dasar = "terminal"

4 elemen dasar (Air, Earth, Fire, Water) diperlakukan sebagai **terminal**.

> **Istilah — terminal:** titik berhenti. Saat pencarian sampai di elemen terminal, dia
> berhenti menggali lebih dalam — karena pemain memang *mulai* dari elemen dasar, jadi
> tak perlu "resep untuk membuat Air". Di kode, `RecipeGraph` langsung menandai 4 dasar
> sebagai terminal di konstruktornya, dan `TierCatalog::applyTerminals` menambahkan
> elemen *special* **Time** sebagai terminal juga.

### 2.3 Hasil pencarian = pohon resep biner

Karena tiap resep memakai **tepat dua** bahan, setiap node non-terminal pada hasil punya
**tepat dua anak**. Ini disebut **pohon biner**.

> **Istilah — pohon (tree):** graf khusus tanpa "siklus" (tidak ada jalur yang berputar
> balik ke dirinya), berbentuk bercabang seperti pohon terbalik: ada satu **akar** (di
> atas, yaitu target) dan **daun-daun** (di bawah, yaitu elemen terminal).
>
> **Istilah — biner:** tiap node punya paling banyak 2 anak. Di sini tepat 2 (karena
> resep selalu gabung 2 bahan), kecuali daun (0 anak).

Mencari resep `Brick` bukan sekadar mengambil satu pasangan langsung `(Mud, Fire)`. Kita
harus terus **mengekspansi** tiap bahan turunan (Mud perlu Earth+Water, dst.) sampai
**semua daun adalah elemen terminal**. Barulah pohonnya "lengkap" dan jadi satu resep
yang valid.

Struktur pohon ini di kode adalah `RecipeTree` — lihat
[03-struktur-data.md](03-struktur-data.md).

---

## 3. Kenapa ini butuh HPC?

Di sinilah masalahnya jadi menarik.

### 3.1 Ruang pencarian bisa meledak

Data yang dipakai punya **720 elemen**, dan banyak elemen punya **banyak resep**. Karena:

- tiap elemen bisa dibuat dengan beberapa cara (percabangan / *branching*),
- tiap bahan dari tiap cara bisa pula dibuat dengan beberapa cara,
- dan ini berlanjut berlapis-lapis ke bawah...

jumlah kemungkinan pohon resep tumbuh **eksponensial**. Mencari, katakanlah, 50 resep
berbeda untuk satu elemen kompleks bisa memaksa program menjelajahi puluhan ribu
kombinasi.

> **Istilah — eksponensial:** tumbuh sangat cepat. Kalau tiap level menggandakan jumlah
> kemungkinan, 10 level = 2¹⁰ ≈ 1000×, 20 level ≈ sejuta×. Ini musuh utama di pencarian.

### 3.2 HPC = membagi kerja

**High Performance Computing (HPC)** intinya: *kalau satu pekerja terlalu lambat,
pekerjakan banyak pekerja sekaligus.* Pekerjaan pencarian yang berat dibagi ke:

- banyak **thread** dalam satu komputer (OpenMP), atau
- banyak **proses** di banyak komputer (MPI), atau
- gabungan keduanya (Hybrid).

Tujuan akademik proyek ini bukan sekadar "menemukan resep", tapi **membandingkan**:
seberapa cepat tiap strategi paralel, dan kapan strategi tertentu menang atau kalah.
(Spoiler menarik: untuk workload ringan, paralel justru bisa *lebih lambat* karena ada
biaya koordinasi — dibahas tuntas di [15](15-benchmark-dan-interpretasi.md).)

---

## 4. Gambaran besar alur program

Apa pun modenya, tiap kali kamu menjalankan program, kira-kira ini yang terjadi:

```
   Kamu ketik perintah (target=Brick, mode, dll.)
                │
                ▼
   1. Baca argumen          (Cli)
   2. Muat data resep       (JsonLoader → RecipeGraph)
   3. Muat katalog tier     (TierCatalog → tandai terminal)
   4. CARI resep            (SearchEngine)  ◄── di sinilah Serial/OpenMP/MPI berbeda
   5. Tulis output          (Visualizer → JSON/PNG)
   6. Cetak ringkasan       (ke layar)
```

Yang membedakan keempat mode HPC **hanya langkah 4** (bagaimana pencarian dijalankan:
satu pekerja, banyak thread, banyak proses, atau campuran). Sisanya identik. Detail
pipeline ini di [02-arsitektur.md](02-arsitektur.md).

---

## 5. Siapa yang membuat & untuk apa

Proyek tugas besar mata kuliah **High Performance Computing**, Institut Teknologi Bandung.
Anggota: Muhammad Yusuf Al Azmi (13222062), Syaugi Adhia Feriyaldi (13222068), William
Gerald Briandelo (13222061).

---

**Lanjut ke:** [02-arsitektur.md](02-arsitektur.md) — bagaimana kode diatur supaya satu
engine pencarian bisa dipakai empat mode berbeda. →

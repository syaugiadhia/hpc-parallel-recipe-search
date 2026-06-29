# 04 — Searching Overview (Peta Semua Algoritma)

> **Prasyarat baca:** [03-struktur-data.md](03-struktur-data.md).
> **Setelah ini kamu paham:** algoritma-algoritma apa saja yang ada di engine, kapan
> masing-masing dipakai, peran heuristik kedalaman, memoization, dan deduplikasi. File
> ini adalah "peta"; detail BFS ada di [05](05-bfs-lazy-detail.md).

Semua kode di bab ini ada di [Search.cpp](src/common/Search.cpp) (kelas `SearchEngine`).

---

## 1. Intuisi: mencari resep itu seperti mencari jalan di labirin

Target = tujuan akhir. Elemen dasar = pintu keluar. Tiap elemen punya beberapa "resep"
(= beberapa jalan menuju ke sana). Kita ingin menemukan jalan **valid** (semua daun
sampai ke elemen dasar), dan biasanya **yang terpendek dulu**.

Ada dua gaya menjelajah labirin:

- **BFS (Breadth-First Search)** — jelajahi selapis demi selapis, dari yang paling dekat
  ke tujuan. Cenderung menemukan resep **terpendek** lebih dulu. **Ini default proyek.**
- **DFS (Depth-First Search)** — susuri satu jalan sampai mentok, baru mundur. Cenderung
  mengikuti **urutan resep di data**.

> **Istilah — BFS / DFS:** dua cara klasik menjelajah graf/pohon. BFS = melebar dulu
> (pakai antrian), DFS = mendalam dulu (pakai rekursi/tumpukan). Detail beda keduanya di
> [16-glossary-dan-faq.md](16-glossary-dan-faq.md).

---

## 2. Titik masuk: `SearchEngine::search()`

Lihat [Search.cpp:390-420](src/common/Search.cpp#L390-L420). Alurnya:

```
search(target):
    pastikan target ada di graf, reset memo, reset statistik, mulai timer
    kalau mode == All  → expandDirectRecipes(target)   ← jalur khusus "all"
    kalau bukan        → expandElementAny(target, limit) ← BFS atau DFS
    rapikan hasil (dedup utk DFS), catat waktu & statistik
    kembalikan SearchResult { target, recipes, stats }
```

`expandElementAny` ([Search.cpp:573-579](src/common/Search.cpp#L573-L579)) memilih:

```
expandElementAny:
    kalau mode != All DAN algoritma == Dfs  → expandElementDfs
    selain itu                              → expandElementBfsLazy   (default)
```

Jadi **secara default** (BFS, mode multiple), kamu masuk ke `expandElementBfsLazy` →
`expandPartialBfsLazy`, yang dibahas tuntas di [05](05-bfs-lazy-detail.md).

---

## 3. Heuristik kedalaman terpendek: `shortestDepths()`

Sebelum BFS jalan, engine menghitung **perkiraan kedalaman minimum** untuk membangun tiap
elemen. Lihat [Search.cpp:478-525](src/common/Search.cpp#L478-L525).

### Intuisi

Sebelum mulai mencari, kita bikin "peta perkiraan jarak": *"Untuk membuat Mud minimal
butuh berapa langkah? Brick? Glass?"* Peta ini memandu BFS supaya memprioritaskan jalan
yang **menjanjikan resep pendek** lebih dulu (mirip A* / best-first search).

> **Istilah — heuristik:** tebakan cerdas (belum tentu tepat, tapi murah dihitung) yang
> memandu pencarian. Di sini: perkiraan kedalaman akhir.
>
> **Istilah — best-first / A\*:** strategi pencarian yang selalu mengeksplorasi kandidat
> dengan "tebakan biaya" terkecil dulu, bukan asal urutan.

### Detail teknis

Algoritmanya **relaksasi titik-tetap** (mirip Bellman–Ford):

```
1. depth[terminal] = 0
2. ulangi sampai tidak ada perubahan:
     untuk tiap elemen non-terminal:
         untuk tiap resepnya (kiri, kanan):
             kandidat = 1 + max(depth[kiri], depth[kanan])
             kalau kandidat < depth[elemen]: perbarui, tandai "berubah"
```

> **Istilah — relaksasi titik-tetap (fixed-point):** ulangi memperbaiki nilai sampai
> stabil (tak ada yang berubah lagi). "Titik tetap" = kondisi di mana satu iterasi lagi
> tidak mengubah apa pun.

Hasilnya dipakai sebagai field `estimate` di tiap `CompactNode`. Dihitung **sekali** dan
di-cache (`shortestDepthsReady_`).

---

## 4. Mode pencarian: `single` / `multiple` / `all`

Diatur lewat `--mode`. Lihat `effectiveLimit` di
[Search.cpp:464-472](src/common/Search.cpp#L464-L472).

| Mode | Arti | Limit efektif |
|---|---|---|
| `single` | berhenti pada **1 resep** | 1 |
| `multiple` | cari sampai **N resep** (`--limit N`) | N |
| `all` | semua resep **langsung** unik target | `--limit` diabaikan |

**Mode `all` istimewa** (`expandDirectRecipes`,
[Search.cpp:757-822](src/common/Search.cpp#L757-L822)): ambil tiap resep *langsung* dari
JSON untuk target, lalu tiap bahan diekspansi pakai **satu subtree representatif
terpendek** (via BFS), bukan semua kombinasi. Jadi "all" = "semua cara langsung membuat
target, masing-masing dengan jalan terpendek ke dasar" — bukan ledakan semua kombinasi.

---

## 5. DFS: `expandElementDfs`

Lihat [Search.cpp:824-886](src/common/Search.cpp#L824-L886). Rekursif:

- untuk tiap resep, ekspansi anak kiri & kanan,
- `combineChoices` membentuk **hasil kali kartesian** (semua kombinasi subtree kiri ×
  kanan), dibatasi `limit`,
- deteksi siklus pakai himpunan `active` (elemen yang sedang dalam jalur).

> **Istilah — hasil kali kartesian (cartesian product):** semua pasangan kombinasi. Kalau
> kiri punya 3 cara dan kanan 4 cara, ada 3×4 = 12 kombinasi.

DFS cocok untuk "resep pertama menurut urutan data". BFS lebih cocok untuk "resep
terpendek dulu". Ada juga `expandElementBfsBounded` (BFS rekursif dengan batas kedalaman)
sebagai varian internal.

---

## 6. Memoization: `--trace-mode memo` vs `full`

Lihat `memoHit` / `memoStore`
([Search.cpp:527-548](src/common/Search.cpp#L527-L548)).

### Intuisi

Kalau kamu sudah menghitung "cara membuat Glass", dan Glass dibutuhkan lagi di tempat
lain, kenapa hitung ulang? Simpan hasilnya, ambil dari catatan. Itu **memoization**.

> **Istilah — memoization / cache:** menyimpan hasil sub-perhitungan supaya saat diminta
> lagi tinggal ambil, tak usah hitung ulang. "Cache hit" = berhasil ambil dari catatan.

### Detail teknis

- `--trace-mode memo` (default) — `memo_` menyimpan hasil subproblem per elemen. Saat
  elemen sama dibutuhkan lagi → `cacheHits++`, hasil diambil, `nodesVisited` turun.
- `--trace-mode full` — matikan cache (`memoEnabled()` false), selalu hitung ulang,
  `cacheHits = 0`. Berguna untuk membandingkan beban kerja "mentah".

> ⚠️ **Catatan jujur:** pada eksperimen Brick limit 50, cache hit kebetulan **0** karena
> jalur ekspansi BFS yang diambil tidak menghasilkan reuse. Mekanismenya tetap ada dan
> bermanfaat untuk target lebih kompleks. (Lihat FAQ di [16](16-glossary-dan-faq.md).)
> Catatan: jalur BFS lazy memakai dedup `seenFinalSignatures`, bukan `memo_` per elemen,
> jadi statistik cache lebih relevan di jalur DFS/bounded.

---

## 7. Deduplikasi & pengurutan akhir

Setelah semua resep terkumpul:

- **Dedup** via signature struktur (lihat [03 §4](03-struktur-data.md#4--signature--kunci-deduplikasi)).
- **Urutan:**
  - BFS → **terpendek dulu** (`treeDepth`), lalu signature sebagai pemecah seri.
  - DFS → urut signature.

Di BFS lazy, dedup terjadi *saat emit* (lewat `seenFinalSignatures`), dan pengurutan
"terpendek dulu" otomatis karena `priority_queue` mengeluarkan state ber-estimate kecil
dulu (lihat [05](05-bfs-lazy-detail.md)).

---

## 8. Tabel ringkas "fungsi mana dipanggil kapan"

```
search(target)
 ├─ mode==All ────────────────► expandDirectRecipes ──► expandElementAny(bahan, 1)
 ├─ algoritma==Bfs (default) ─► expandElementBfsLazy ─► expandPartialBfsLazy
 │                                                       └─(jika OpenMP & threads>1)─► expandPartialBfsLazyOpenmp
 └─ algoritma==Dfs ───────────► expandElementDfs

completePartial(partial)   ← dipakai MPI worker untuk melanjutkan pohon parsial
 ├─ Bfs/All ──► expandPartialBfsLazy
 └─ Dfs ─────► completePartialNode
```

`completePartial` ([Search.cpp:422-450](src/common/Search.cpp#L422-L450)) penting untuk
MPI: master mengirim **pohon setengah jadi**, worker **menyelesaikannya** lewat fungsi
ini. Detail di [09-mpi.md](09-mpi.md).

---

**Lanjut ke:** [05-bfs-lazy-detail.md](05-bfs-lazy-detail.md) — bedah tuntas algoritma
inti BFS lazy frontier, lengkap dengan contoh jejak langkah-demi-langkah. →

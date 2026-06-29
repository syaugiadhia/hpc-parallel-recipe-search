# 12 — Kenapa OpenMP TIDAK Bisa Multinode (dan MPI Bisa)

> **Prasyarat baca:** [06-konsep-paralel-hpc.md](06-konsep-paralel-hpc.md) (shared vs
> distributed memory), [08-openmp.md](08-openmp.md), [09-mpi.md](09-mpi.md).
> **Setelah ini kamu paham:** alasan **fundamental** kenapa OpenMP terkurung di satu
> komputer sementara MPI bisa menyeberang ke komputer lain — bukan sekadar "OpenMP belum
> mendukung", tapi konsekuensi langsung dari model memorinya.

Ini pertanyaan yang sering muncul dan inti pemahaman HPC. Mari tuntaskan.

---

## 1. Jawaban singkat

**OpenMP berbasis shared-memory: semua thread-nya harus bisa membaca & menulis RAM yang
sama secara langsung. Komputer lain punya RAM yang terpisah secara fisik — tidak ada kabel
yang membuat RAM komputer A muncul sebagai variabel di komputer B. Maka thread OpenMP tidak
punya cara menyentuh memori mesin lain, sehingga OpenMP tidak bisa menyeberang komputer.**

MPI berbeda: ia **tidak** mengandalkan memori bersama. Tiap proses punya RAM sendiri dan
bertukar data dengan **mengirim pesan eksplisit** — dan pesan bisa dikirim ke mana saja,
termasuk lewat jaringan ke komputer lain.

---

## 2. Analogi: papan tulis vs surat

```
OPENMP — Tim di satu ruangan, satu papan tulis
┌────────────────────────────────────────────┐
│   🧑 🧑 🧑 🧑   ← semua orang (thread)        │
│      ▼ ▼ ▼ ▼                                 │
│   ┌──────────────┐                           │
│   │  PAPAN TULIS  │  ← RAM bersama. Semua     │
│   │   (bersama)   │     menulis & membaca     │
│   └──────────────┘     LANGSUNG di sini.      │
└────────────────────────────────────────────┘
  Cepat, tapi semua HARUS di ruangan yang sama.
  Orang di gedung lain TIDAK BISA lihat papan ini.

MPI — Orang di gedung berbeda, saling kirim surat
┌──────────────┐   📨 surat (pesan)   ┌──────────────┐
│ Gedung A      │ ◄─────────────────► │ Gedung B      │
│ papan sendiri │                     │ papan sendiri │
└──────────────┘                     └──────────────┘
  Lebih lambat (harus nulis & kirim surat),
  tapi bisa antar gedung mana pun.
```

OpenMP = orang sekamar berbagi satu papan tulis. Tambah orang dari gedung sebelah? Mereka
tak bisa melihat papanmu. Untuk melibatkan mereka, kamu **harus** beralih ke "kirim surat"
— dan itulah MPI.

---

## 3. Detail teknis: bagaimana OpenMP bekerja

OpenMP adalah **paralelisme tingkat thread dalam satu proses**. Lihat bagaimana proyek
memakainya ([Search.cpp:731](src/common/Search.cpp#L731)):

```cpp
#pragma omp parallel for schedule(dynamic) num_threads(threadCount)
for (int i = 0; i < batch.size(); ++i)
    expansions[i] = expandFrontierState(graph_, depthMap, std::move(batch[i]));
```

- `#pragma omp parallel for` menyuruh compiler membagi iterasi loop ke beberapa **thread**.
- Thread-thread ini berbagi **ruang alamat (address space)** proses yang sama. Variabel
  `graph_`, `depthMap`, `batch`, `expansions` — semuanya **satu salinan di RAM**, diakses
  semua thread lewat alamat memori yang sama.

> **Istilah — address space (ruang alamat):** "peta alamat" memori milik satu proses. Saat
> kode menyebut variabel `graph_`, sebenarnya itu alamat tertentu di RAM. Semua thread
> dalam proses itu memakai peta yang sama, jadi `graph_` menunjuk ke byte fisik yang sama.

Kunci masalahnya: **alamat memori itu hanya bermakna di dalam satu proses di satu mesin.**
Alamat `0x7ff...` di laptop A menunjuk RAM laptop A. Kalau thread "ada" di laptop B, alamat
itu tak menunjuk apa pun yang relevan — RAM laptop B isinya beda. Tidak ada mekanisme di
OpenMP untuk "mengintip" RAM mesin lain.

### Kenapa secara teknis tak mungkin diakses langsung?
- RAM adalah perangkat keras fisik yang menempel di satu motherboard.
- CPU hanya bisa membaca RAM yang terhubung ke dirinya (lewat bus memori).
- CPU laptop B **tidak punya kabel** ke RAM laptop A. Satu-satunya penghubung adalah
  **jaringan** — dan jaringan bukan memori; ia kanal pesan. Mengaksesnya butuh protokol
  kirim-terima (socket), bukan "baca alamat".

OpenMP dirancang untuk kasus "semua thread satu CPU/satu mesin berbagi RAM". Ia tak punya —
dan memang **bukan tugasnya** — lapisan jaringan untuk menyatukan RAM antar mesin.

---

## 4. Detail teknis: bagaimana MPI menyeberang mesin

MPI tak pernah berasumsi memori bersama. Tiap **proses** punya RAM sendiri. Untuk berbagi,
ia memanggil fungsi kirim/terima eksplisit (lihat [09-mpi.md](09-mpi.md)):

```cpp
MPI_Send(payload.data(), size, MPI_CHAR, dest, tag, MPI_COMM_WORLD);  // kirim byte
MPI_Recv(buffer.data(),  size, MPI_CHAR, src,  tag, MPI_COMM_WORLD, ...); // terima byte
```

`MPI_Send` di balik layar bisa berupa:
- copy memori lokal (kalau dua rank di mesin sama), atau
- **paket jaringan TCP/IP** (kalau rank di mesin berbeda).

Yang penting: **kodenya sama**. Aplikasi tidak peduli apakah lawan bicaranya di mesin yang
sama atau seberang jaringan — `mpiexec`/`smpd` yang mengurus rute. Itu sebabnya program MPI
yang sama bisa jalan `-n 4` di satu PC **atau** `-hosts 2 ...` lintas PC tanpa ubah kode.

Inilah mengapa di proyek ini:
- task & hasil **diserialisasi ke JSON** dulu (lihat [09 §5](09-mpi.md#5-serialisasi-kenapa-pakai-json))
  — karena yang bisa dikirim antar-mesin hanyalah **byte**, bukan objek/pointer di RAM;
- tiap rank **membaca `recipes.json` sendiri** — karena tak ada RAM bersama untuk menaruh
  satu graf yang dilihat semua.

---

## 5. Tabel perbandingan langsung

| Aspek | OpenMP | MPI |
|---|---|---|
| Unit paralel | thread | proses |
| Model memori | **shared** (RAM bersama) | **distributed** (RAM terpisah) |
| Cara berbagi data | baca/tulis variabel langsung | kirim/terima pesan (eksplisit) |
| Jangkauan | **1 komputer** | 1 **atau banyak** komputer |
| Biaya komunikasi | sangat rendah (akses RAM) | lebih tinggi (serialisasi + jaringan) |
| Di proyek ini | `#pragma omp parallel for` | `MPI_Send/Recv/Probe`, JSON |
| Bisa multinode? | ❌ tidak | ✅ ya |

---

## 6. "Tapi kan ada cara bikin OpenMP multinode?"

Ada teknologi yang *mencoba* membuat OpenMP lintas mesin (mis. **distributed shared memory**
/ cluster OpenMP), dengan menipu program seolah ada RAM bersama padahal di belakangnya
menyalin halaman memori lewat jaringan. Tapi:
- itu **bukan OpenMP standar** dan jarang dipakai,
- performanya buruk (akses "memori" yang sebenarnya jaringan jadi sangat lambat &
  tak terduga),
- proyek ini **tidak** memakainya.

Untuk tujuan praktis & untuk proyek ini: **OpenMP = satu mesin, titik.** Multinode = MPI.

---

## 7. Karena itu: Hybrid = MPI antar-mesin + OpenMP di dalam mesin

Sekarang desain hybrid ([10-hybrid-mpi-openmp.md](10-hybrid-mpi-openmp.md)) jadi masuk akal
sepenuhnya:

```
Antar komputer  →  WAJIB MPI   (cuma message passing yang bisa menyeberang)
Di dalam tiap   →  pilih OpenMP (lebih murah daripada banyak proses MPI di satu RAM:
komputer            tak perlu serialisasi/kirim pesan untuk berbagi)
```

Jadi kamu pakai **MPI untuk yang OpenMP tak bisa** (menyeberang mesin), dan **OpenMP untuk
yang ia paling efisien** (memanfaatkan banyak core dalam satu RAM bersama). Dua alat,
masing-masing untuk lapisan yang tepat.

---

**Lanjut ke:** [13-gui.md](13-gui.md) — pembungkus GUI yang mengorkestrasi semua mode ini.
→

# 13 — GUI Python (Orkestrator)

> **Prasyarat baca:** [09-mpi.md](09-mpi.md), [11-multinode-lan.md](11-multinode-lan.md).
> **Setelah ini kamu paham:** apa peran GUI (dan apa yang BUKAN perannya), fitur Run
> Compare, dan bagaimana mekanisme Master/Slave LAN bekerja.

Kode: [gui/alchemy_gui.py](gui/alchemy_gui.py) (CustomTkinter).

---

## 1. Intuisi: GUI itu remote control, bukan mesinnya

Poin paling penting: **GUI tidak melakukan pencarian apa pun.** Engine C++ tetap yang
bekerja. GUI hanya **pembungkus/orkestrator** — ia menyusun perintah command-line, memanggil
executable (`alchemy_serial`/`alchemy_openmp`/`alchemy_mpi`), lalu menampilkan hasilnya
dengan rapi.

> **Istilah — orkestrator:** komponen yang mengatur & menjalankan komponen lain, tanpa
> melakukan pekerjaan inti sendiri. GUI di sini "menyetir" executable C++.

Analogi: GUI adalah remote control; engine C++ adalah TV-nya. Remote tak menampilkan gambar
sendiri — ia menyuruh TV bekerja.

---

## 2. Fitur utama

### 2.1 Run tunggal
Pilih engine (serial/openmp/mpi), target (dari `data/tiers.json`, divalidasi ke
`recipes.json`), algoritma, mode, trace/visual mode, limit, threads, split-depth, baseline
→ klik Run → GUI menyusun perintah CLI yang sesuai (lihat [14-cli-build-run.md](14-cli-build-run.md))
dan menjalankannya.

### 2.2 Run Compare (fitur kunci untuk akademik)
Menjalankan **beberapa varian sekaligus** lalu menampilkan tabel perbandingan + menulis
CSV. Format input (satu varian per baris), dari kode
[gui/alchemy_gui.py:568](gui/alchemy_gui.py#L568):

```
serial
openmp 4
mpi 2
hybrid-local 2x2
multi a,b,c          ← MPI multi-host (master + slave), nilai = slot tiap host
hybrid axT,bxT       ← hybrid multi-host (thread-profile per host)
```

> Catatan: untuk `multi`/`hybrid`, nilai pertama selalu **master**. Contoh
> `hybrid 1x2,2x4,2x4` = master 1 rank × 2 thread + tiap slave 2 rank × 4 thread (ini
> langsung dipetakan ke `--thread-profile`, lihat [10-hybrid-mpi-openmp.md](10-hybrid-mpi-openmp.md)).

Inilah cara menghasilkan tabel benchmark yang dibahas di
[15-benchmark-dan-interpretasi.md](15-benchmark-dan-interpretasi.md).

### 2.3 Preview gambar (lazy)
Tab Image merender **satu** resep dari JSON ke file sementara untuk pratinjau cepat. Render
**graph penuh** (mahal, butuh Graphviz) hanya dilakukan saat tombol `Render Full Graph`
ditekan ([gui/alchemy_gui.py:598](gui/alchemy_gui.py#L598)). Ini agar GUI tetap responsif.

---

## 3. Master/Slave LAN (untuk demo multi-PC)

GUI punya mekanisme handshake supaya kamu bisa "mengundang" laptop lain ikut MPI. Konstanta
dari [gui/alchemy_gui.py:59-61](gui/alchemy_gui.py#L59-L61):

```python
SLAVE_AGENT_PORT     = 50555   # TCP: master mengundang slave
SLAVE_DISCOVERY_PORT = 50556   # UDP: scan/discovery slave di LAN
INVITE_TIMEOUT_SECONDS = 30    # slave harus Accept dalam 30 detik
```

### Alur handshake
```
SLAVE                              MASTER
  role=Slave                         role=Master, engine=mpi
  Start Slave                        Scan LAN (UDP 50556) / Add manual IP
  (buka TCP server :50555,           │
   UDP discovery :50556)             │
        ◄──── invite (TCP 50555) ────┤  klik Connect
  muncul "Incoming master request"   │  status: waiting approval (≤30s)
  klik Accept  ────────────────────► │  status: connected
                                     │  klik Run/Compare →
                                     │  mpiexec -hosts ... (master=localhost rank 0)
```

Poin penting (sinkron dengan [11-multinode-lan.md](11-multinode-lan.md)):
- **Host pertama selalu master** (rank 0), di-address `localhost`.
- Handshake GUI (status `connected`) hanya membuktikan GUI bisa bicara lewat port 50555 —
  **tidak** menjamin MS-MPI bisa meluncurkan proses ke slave. Kalau MPI gagal setelah
  `connected`, masalahnya di smpd/jaringan, bukan GUI (lihat
  [11 §5](11-multinode-lan.md#5-tantangan-jaringan-dan-kenapa-terjadi)).

---

## 4. Menjalankan GUI

```powershell
pip install -r requirements.txt   # customtkinter dll.
python gui/alchemy_gui.py
```

Untuk demo multi-PC otomatis (Windows), pakai script di `commands/` (`run_master.bat`,
`run_slave1.bat`, ...) yang membereskan konfigurasi MS-MPI sekaligus membuka GUI di role
yang benar. Detail di [tutor_multi_pc.md](tutor_multi_pc.md).

---

**Lanjut ke:** [14-cli-build-run.md](14-cli-build-run.md) — cheat-sheet build & semua flag
CLI untuk menjalankan tiap mode langsung dari terminal. →

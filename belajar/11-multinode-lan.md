# 11 — Multinode / Multi-PC via LAN

> **Prasyarat baca:** [09-mpi.md](09-mpi.md).
> **Setelah ini kamu paham:** bagaimana menjalankan MPI di beberapa komputer sungguhan,
> bagaimana program membuktikan rank benar-benar jalan di mesin lain, dan masalah jaringan
> umum (smpd, error 1726, deadlock multi-adapter) beserta solusinya.

Sumber praktis lengkap: [tutor_multi_pc.md](tutor_multi_pc.md). File ini merangkum &
menjelaskan "kenapa"-nya.

---

## 1. Intuisi: dari banyak proses → banyak komputer

Di [09-mpi.md](09-mpi.md), `mpiexec -n 4` menjalankan 4 proses **di satu komputer**.
Karena MPI memakai **message passing** (bukan shared memory), proses-proses itu sebenarnya
**tidak harus** di komputer yang sama — mereka bisa tersebar di beberapa komputer di
jaringan yang sama (LAN), dan tetap bertukar pesan lewat jaringan.

> **Istilah — multinode:** menjalankan di banyak "node" (komputer/mesin) sekaligus. Lawan
> dari single-node (satu mesin).
>
> **Istilah — LAN (Local Area Network):** jaringan lokal, mis. semua laptop tersambung ke
> Wi-Fi/hotspot yang sama.

Inilah keunggulan unik MPI yang **tidak dimiliki OpenMP** — alasannya dijelaskan tuntas di
[12-kenapa-openmp-tidak-multinode.md](12-kenapa-openmp-tidak-multinode.md).

---

## 2. Menjalankan lintas komputer

Dua gaya peluncuran:

**OpenMPI/MPICH (Linux) — hostfile:**
```bash
mpirun --hostfile hosts.txt build/alchemy_mpi ...
```
`hosts.txt` berisi daftar host + jumlah slot (lihat contoh `hosts.txt` di repo).

**MS-MPI (Windows) — `-hosts`:**
```powershell
mpiexec -hosts 2 localhost 4 desktop-s3pfjin 2 -wdir C:\tubes-2 ^
    C:\tubes-2\build\alchemy_mpi.exe --data C:\tubes-2\data\recipes.json ...
```
Artinya 2 host: `localhost` (master) 4 slot, `desktop-s3pfjin` (slave) 2 slot → total 6
rank.

> **Istilah — slot:** jumlah proses MPI yang boleh berjalan di satu host. Master 4 + slave
> 2 = 6 proses MPI total.

Poin desain penting: **master selalu di-address sebagai `localhost`** (rank 0 ada di mesin
master). Alasannya teknis-jaringan — supaya rank 0 tak melewati jalur smpd remote yang
rawan salah-adapter (penyebab umum hang/error 1726). Lihat §5.

---

## 3. Syarat agar multi-PC jalan

Dari [tutor_multi_pc.md](tutor_multi_pc.md), di **tiap** komputer:

1. Project ada di **path yang sama** (mis. `C:\tubes-2`).
2. File ini ada di semua komputer: `build/alchemy_mpi.exe`, `data/recipes.json`,
   `data/tiers.json`. (Ingat dari [09 §2](09-mpi.md#2-setiap-rank-baca-datanya-sendiri-keputusan-desain-penting):
   tiap rank baca data sendiri, jadi file wajib ada di mana-mana.)
3. MS-MPI terpasang, `smpd` berjalan, firewall mengizinkan.

---

## 4. Bukti rank benar-benar di mesin lain: `collectRankHostnames`

[main_mpi.cpp:125-152](src/mpi/main_mpi.cpp#L125-L152). Saat start, tiap rank mengambil
nama komputernya (`MPI_Get_processor_name`), lalu dikumpulkan ke master via `MPI_Gather`:

> **Istilah — `MPI_Gather`:** operasi "kumpulkan" — tiap rank mengirim sepotong data, dan
> rank 0 menerima semuanya sebagai satu array berurutan. Di sini: mengumpulkan nama
> hostname tiap rank.

Hasilnya muncul di ringkasan:

```
Rank hostnames:
  Rank 0: MASTER_HOST
  Rank 1: MASTER_HOST
  Rank 2: SLAVE_HOST     ← kalau ini hostname lain, multi-PC SUNGGUHAN terbukti
```

Kalau hostname slave muncul, berarti MPI benar-benar memakai komputer lain — bukan cuma
banyak proses di satu mesin.

---

## 5. Tantangan jaringan (dan kenapa terjadi)

Multi-PC MPI di Windows penuh jebakan jaringan. Ringkasan dari
[tutor_multi_pc.md](tutor_multi_pc.md):

### 5.1 smpd + akun bersama
> **Istilah — smpd:** *Service Manager Process Daemon* MS-MPI — program latar yang
> meluncurkan proses MPI di tiap komputer atas perintah `mpiexec`.

MS-MPI butuh `smpd` + `mpiexec` berjalan sebagai **akun Windows yang sama (username +
password sama) di semua PC**. Sebab autentikasi smpd **dua arah**: master meluncurkan job
ke slave (forward) **dan** slave harus *connect back* ke master. Kalau akun beda,
connect-back ditolak → master melaporkan `error 1726`. Karena itu folder `commands/`
otomatis membuat akun cluster bersama (`CLUSTER_USER`/`CLUSTER_PASS`) di tiap PC.

### 5.2 Error 1726 — IPv6 & host multi-homed
> **Istilah — multi-homed:** satu komputer punya banyak alamat jaringan (Wi-Fi, Ethernet,
> VPN seperti Tailscale, alamat APIPA `169.254.x`, adapter virtual VMware/Hyper-V).

Kalau IPv6 didahulukan atau ada banyak adapter, smpd bisa mencoba jalur yang tak
terjangkau → `error 1726` ("Failed to connect to SMPD Manager Instance"). Solusi (otomatis
lewat `commands/_netfix.bat`): matikan IPv6, nonaktifkan Tailscale & adapter `169.254.x`,
sisakan satu IPv4 per PC.

### 5.3 Deadlock saat run (multi-adapter)
> **Istilah — deadlock:** dua pihak saling menunggu selamanya, tak ada yang maju.

Tiap rank harus saling membuka koneksi socket. Di mesin multi-homed, MPI bisa memberi
alamat di adapter yang salah → rank mencoba connect ke alamat tak terjangkau dan menggantung
di `MPI_Init` (sebelum ada output). Solusi sama: sederhanakan jaringan jadi satu IPv4,
matikan VPN, address slave pakai **hostname** (cocok dengan kredensial `cmdkey`).

### 5.4 Tes cepat memisahkan masalah
```powershell
mpiexec -hosts 2 localhost 1 desktop-s3pfjin 1 hostname
```
- Cepat mencetak dua hostname → MS-MPI remote sudah jalan; masalah (kalau ada) di kode/GUI.
- Hang/timeout → masalah di `smpd`/firewall/jaringan slave, **bukan** kode aplikasi.

---

## 6. Cara termudah: pakai GUI + script `commands/`

Untuk demo, jalur termudah adalah GUI Master/Slave (lihat [13-gui.md](13-gui.md)) yang
dibantu script di `commands/` (mengurus registry, firewall, kredensial, smpd otomatis).
Langkah ringkas:
- Di slave: GUI → role Slave → Start Slave → tunggu undangan → Accept (dalam 30 detik).
- Di master: GUI → role Master → engine mpi → Add slave (hostname) → Connect → Run.

Detail langkah lengkap ada di [tutor_multi_pc.md](tutor_multi_pc.md).

---

**Lanjut ke:** [12-kenapa-openmp-tidak-multinode.md](12-kenapa-openmp-tidak-multinode.md)
— pertanyaan inti: kenapa OpenMP tak bisa melakukan apa yang baru saja kita lakukan dengan
MPI? →

# Tutorial Multi-PC MPI Dengan GUI Master/Slave

Dokumen ini menjelaskan cara menjalankan project Little Alchemy HPC di 2 komputer atau lebih memakai GUI Python dan MS-MPI di Windows.

## Kunci: Jalur `-hosts` Butuh `smpd` di Port 8677

Multi-node memakai `mpiexec -hosts ...`. Jalur ini **butuh process manager MS-MPI (`smpd`)
mendengarkan di port `8677` di SETIAP node** (master juga, karena master = host pertama). Kalau
`smpd` tidak hidup, gejalanya:

```text
ERROR: Failed RpcCliCreateContext error 1722
Aborting: mpiexec ... is unable to connect to the smpd service on <host>:8677
```

atau hang ~60 detik lalu gagal. Inilah penyebab paling umum "multi-PC tidak jalan".

Sekarang GUI sudah lebih pintar:

- **Start Slave** otomatis menyalakan `smpd` lokal (stop `MsMpiLaunchSvc` dulu agar tidak rebutan
  port), jadi node slave langsung siap di-launch master.
- Sebelum `Run`/`Compare` multi-node, master melakukan **preflight**: cek `smpd` di tiap host. Kalau
  ada yang mati, muncul pesan jelas (bukan hang 60 detik) menyuruh jalankan `run_*.bat`/`Start Slave`.

### Tes Cepat 1 PC (Loopback) — Tanpa PC Kedua

Untuk memastikan jalur `-hosts` benar-benar jalan di mesinmu, dobel-klik
`commands/_smoke_local.bat`. Script ini start `smpd` lokal lalu menjalankan
`mpiexec -hosts 1 localhost 2 ...` dan `mpiexec -hosts 1 %COMPUTERNAME% 2 ...`. **Lulus** bila
output memuat `Processes: 2`, `Recipes found` > 0, dan `Rank hostnames: [...]` berisi hostname.
Kalau loopback lulus, mekanisme multi-node sudah benar; tinggal urusan jaringan antar-PC.

## Cara Cepat (Pakai Script Otomatis)

Folder `commands/` berisi script yang membereskan semua konfigurasi MS-MPI secara otomatis (registry, firewall, kredensial `cmdkey`, `smpd` daemon, stop launch service) lalu membuka GUI di role yang benar.

**Sekali edit:** buka `commands/_config.bat`, sesuaikan `PROJECT_DIR`, `MSMPI_BIN`, serta hostname/username/password/slots tiap slave.

Lalu cukup **dobel-klik** (script minta hak Administrator otomatis):

- Di laptop master: `commands/run_master.bat`
- Di laptop slave 1: `commands/run_slave1.bat`
- Di laptop slave 2: `commands/run_slave2.bat`

Tiap script akan: membuka window `SMPD` (jangan ditutup) dan membuka GUI. GUI master sudah di role Master + engine `mpi`; GUI slave sudah auto Start Slave dan menunggu undangan.

Setelah itu tinggal: di GUI master `Add manual` **hostname** slave (mis. `desktop-s3pfjin`, bukan IP) → `Connect` → slave klik `Accept` → `Run`/`Compare`.

Selesai pakai, jalankan `commands/deactivate.bat` di tiap PC untuk mengembalikan firewall/registry dan menghapus kredensial.

> Penting: `cmdkey` mendaftarkan kredensial per **nama target**. Karena itu di GUI tambahkan slave memakai **hostname**, supaya cocok dengan target `cmdkey`. Kalau terpaksa pakai IP, isi `S1_IP`/`S2_IP` di `_config.bat` agar IP ikut didaftarkan.

## Konsep Singkat

**Master** adalah komputer utama. Komputer ini membuka GUI, memilih target, menjalankan command, dan menjadi MPI rank 0.

**Slave** adalah komputer tambahan. Komputer ini juga membuka GUI, memilih role `Slave`, lalu menunggu undangan dari master.

**Slots** adalah jumlah process MPI yang boleh dijalankan di satu komputer. Contoh:

- Master slots `4` berarti master menjalankan 4 process.
- Slave slots `2` berarti slave itu menjalankan 2 process.
- Jika master slots `4` dan satu slave slots `2`, total process MPI adalah 6.

Master selalu diletakkan sebagai host pertama supaya rank 0 ada di laptop master. Slave hanya dipakai kalau statusnya sudah `connected`.

## Apakah Kedua Komputer Harus Buka GUI?

Ya, untuk mode GUI master/slave:

- Komputer master membuka GUI untuk scan, invite, dan run.
- Komputer slave membuka GUI untuk klik `Start Slave` dan menerima atau menolak invite.

Slave tidak menjalankan pencarian sendiri dari tombol `Run`. Slave hanya menunggu MPI launch dari master setelah menerima invite.

## Syarat Sebelum Mulai

Lakukan ini di semua komputer:

1. Install Python, CustomTkinter dependency, CMake, compiler C++, dan MS-MPI.
2. Pastikan project ada di path yang sama di semua komputer, misalnya:

```text
C:\Users\HP\Documents\C++\HPC\Tubes
```

3. Pastikan file ini ada di semua komputer:

```text
build\alchemy_mpi.exe
data\recipes.json
data\tiers.json
```

4. Build project di semua komputer jika belum:

```powershell
cmake -S . -B build
cmake --build build
```

5. Jalankan GUI dari folder project:

```powershell
python gui\alchemy_gui.py
```

## Firewall Dan Port

GUI slave memakai:

- TCP `50555` untuk invite master ke slave.
- UDP `50556` untuk discovery atau scan slave agent.

MS-MPI juga butuh izin firewall sendiri. Jika Windows meminta izin akses jaringan untuk Python atau MS-MPI, pilih allow pada private network.

Jika scan tidak menemukan laptop teman, itu tidak selalu berarti gagal. Hotspot atau firewall sering memblokir ping/broadcast. Pakai `Add manual` dengan IP slave.

## Cara Mengetahui IP Slave

Di komputer slave, jalankan:

```powershell
ipconfig
```

Cari adapter Wi-Fi atau hotspot yang sedang dipakai, lalu ambil `IPv4 Address`, contoh:

```text
192.168.137.24
```

IP itu dimasukkan di GUI master lewat `Add manual`.

## Langkah 2 Komputer

Misal:

- Laptop kamu = master.
- Laptop teman = slave.

### 1. Di Laptop Slave

1. Buka GUI:

```powershell
python gui\alchemy_gui.py
```

2. Pilih role `Slave`.
3. Isi `Offered slots`, misalnya `2`.
4. Klik `Start Slave`.
5. Status akan menjadi kira-kira:

```text
Slave waiting on 192.168.x.x:50555
```

Jangan tutup GUI slave.

### 2. Di Laptop Master

1. Buka GUI:

```powershell
python gui\alchemy_gui.py
```

2. Pilih role `Master`.
3. Pilih engine `mpi`.
4. Centang `Use accepted master/slave host list`.
5. Pastikan baris host master ada di list dengan status `master`.
6. Atur slots master, misalnya `4`.

### 3. Tambahkan Slave

Ada dua cara:

**Scan LAN**

1. Klik `Scan LAN`.
2. Jika IP slave muncul, biarkan statusnya `candidate`.

**Manual**

1. Masukkan IP slave di field manual host, misalnya:

```text
192.168.137.24
```

2. Klik `Add manual`.

### 4. Master Mengirim Invite

Di GUI master:

1. Klik tombol `Connect` pada baris IP slave.
2. Status slave di master menjadi `waiting approval`.
3. Master menunggu maksimal 30 detik.

Di GUI slave:

1. Akan muncul bagian `Incoming master request`.
2. Terlihat nama/IP master dan countdown 30 detik.
3. Klik `Accept` untuk menerima.
4. Klik `Reject` untuk menolak.

Jika slave menerima, status di master berubah menjadi `connected`.

Jika slave menolak, status menjadi `rejected`.

Jika tidak ada aksi sampai countdown habis, status menjadi `timeout`.

### 5. Jalankan MPI

Di GUI master:

1. Pilih target, algorithm, mode, trace mode, visual mode, output, dan split depth.
2. Klik `Run`.

GUI master akan membuat command seperti:

```powershell
mpiexec -hosts 2 MASTER_HOST 4 desktop-s3pfjin 2 -wdir C:\tubes-2 C:\tubes-2\build\alchemy_mpi.exe ...
```

Artinya ada 2 host:

- `MASTER_HOST` dengan 4 slots.
- `desktop-s3pfjin` (hostname slave) dengan 2 slots.
- `-wdir C:\tubes-2` menetapkan working directory di tiap host agar konsisten.

## Menambah Slave Lebih Dari Satu

Untuk slave kedua, ketiga, dan seterusnya:

1. Buka GUI di komputer slave tersebut.
2. Pilih role `Slave`.
3. Klik `Start Slave`.
4. Di master, scan atau add manual IP.
5. Klik `Connect`.
6. Slave harus `Accept` dalam 30 detik.

Master bisa mengatur slots masing-masing slave dari tabel host sebelum run.

## Split Depth Itu Apa?

`split-depth` menentukan seberapa jauh master memecah pencarian menjadi task sebelum dibagikan ke worker MPI.

Contoh target `Brick`:

- `--split-depth 1`: task biasanya berasal dari recipe langsung `Brick`, seperti `Brick = Mud + Fire`.
- `--split-depth 2`: master membuka satu lapis lagi dari ingredient, sehingga task lebih banyak.

Semakin banyak komputer atau process, biasanya butuh task lebih banyak supaya semua worker kebagian kerja. Nilai `2` sering cukup untuk tes. Nilai terlalu besar bisa menambah overhead.

Khusus `--mode all`, split depth diabaikan karena `all` berarti direct recipe unik target dari JSON dengan subtree representatif terpendek, bukan semua kombinasi subtree.

## Cara Mengecek Komputer Benar-Benar Dipakai

Setelah run MPI selesai, lihat output log atau JSON. Statistik MPI menampilkan rank hostname, misalnya:

```text
Rank hostnames:
  Rank 0: MASTER_HOST
  Rank 1: MASTER_HOST
  Rank 2: SLAVE_HOST
```

Kalau hostname slave muncul, berarti MPI benar-benar jalan di komputer slave.

## Troubleshooting

### Scan Tidak Menemukan Slave

Penyebab umum:

- Firewall memblokir UDP broadcast.
- Hotspot tidak meneruskan broadcast.
- Ping/ICMP diblokir.

Solusi:

- Pakai `Add manual` dengan IP slave.
- Pastikan slave sudah klik `Start Slave`.
- Pastikan kedua komputer berada di jaringan yang sama.
- Izinkan Python di firewall private network.

### Master Connect Timeout

Penyebab umum:

- Slave belum klik `Start Slave`.
- IP salah.
- Firewall memblokir TCP `50555`.
- Slave tidak menekan `Accept` dalam 30 detik.

Solusi:

- Cek IP slave dengan `ipconfig`.
- Klik `Start Slave` ulang.
- Coba `Add manual` lagi.
- Pastikan firewall mengizinkan Python.

### Status Connected Tetapi MPI Gagal (nunggu lama lalu error)

Handshake GUI (status `connected`) hanya membuktikan GUI master bisa bicara dengan GUI slave lewat port 50555. Itu **tidak** menjamin MS-MPI bisa meluncurkan process ke slave. Kalau saat `Run`/`Compare` GUI menunggu lama (sekitar 60 detik) lalu gagal, hampir selalu MS-MPI gagal launch remote.

Project ini memakai pendekatan **`smpd` daemon + `cmdkey`** (lihat folder `commands/`), bukan MS-MPI Launch Service. Keuntungannya: tiap PC boleh memakai **akun Windows yang berbeda** (username/password berbeda), karena `cmdkey` menyimpan kredensial per-host. (Sebaliknya, opsi `mpiexec -pwd` hanya mendukung satu akun untuk semua host.)

Agar berhasil, di tiap PC harus terpenuhi:

- `smpd -d` sedang berjalan (window `SMPD` dari `run_*.bat` jangan ditutup).
- MS-MPI Launch Service di-stop (`net stop MsMpiLaunchSvc`) supaya tidak rebutan port 8677. Script sudah melakukannya.
- `LocalAccountTokenFilterPolicy=1` dan firewall diizinkan (script mematikan firewall sementara).
- Di master, kredensial tiap slave sudah didaftarkan: `cmdkey /add:HOST /user:HOST\user /pass:...`.
- `build\alchemy_mpi.exe` dan `data\` ada di path yang sama (`C:\tubes-2`) di semua PC.

Tes paling cepat untuk memisahkan masalah MPI dari GUI: **gunakan `commands/_test_2pc.bat`** (di
master). Script ini otomatis: matikan smpd lama, set `MSMPI_NETMASK` ke subnet slave (`S1_IP`),
start smpd, deteksi IP hotspot master, lalu jalankan `mpiexec ... hostname` ke master+slave dan
mencetak panduan baca hasil.

> PENTING: jangan jalankan `mpiexec` dari **cmd biasa**. smpd master harus hidup (lewat
> `run_master.bat` atau `_test_2pc.bat`). Kalau dari cmd kosong, smpd master mati → `error 1722`
> ke IP master sendiri.

Manual (kalau mau eksplisit), **dari window `run_master.bat`** atau setelah smpd master hidup:

```powershell
mpiexec -genv MSMPI_NETMASK 172.20.10.0/255.255.255.0 -hosts 2 <IP_MASTER_HOTSPOT> 1 172.20.10.2 1 hostname
```

- Cepat mencetak dua hostname → MS-MPI remote jalan; tinggal pakai GUI.
- `error 1722` → smpd di IP itu mati (master start lewat cmd kosong, atau slave belum `run_slaveN.bat`).
- `error 1726` → smpd advertise adapter salah; lihat bagian Error 1726 di bawah.

### Error 1726 "unable to connect to the smpd manager ... error 1726"

Gejala: koneksi awal master→slave port `8677` **berhasil**, tapi gagal saat **re-connect ke port
manager dinamis** (port tinggi acak):

```text
Aborting: smpd on DESKTOP-S3PFJIN is unable to connect to the smpd manager on 172.20.10.2:65052 error 1726
```

**Penyebab utama yang sering terlewat: master di-list pakai HOSTNAME, dan hostname itu resolve ke
`::1` (IPv6 loopback) duluan.** Cek di master:

```powershell
Resolve-DnsName DESKTOP-S3PFJIN
```

Kalau muncul `AAAA  ::1` di atas `A  <ipv4>`, maka begitu master dirujuk lewat nama, smpd/MPI bisa
mengikat/mereferensikan `::1` yang tak terjangkau dari slave → **1726**. Penyebab lain: host
multi-homed (adapter `169.254.x` APIPA / VPN) sehingga manager smpd bind ke adapter salah.

**Catatan penting soal adapter APIPA:** adapter `169.254.x` bernama `Local Area Connection* N`
(Microsoft Wi-Fi Direct Virtual Adapter) **sering TIDAK bisa dimatikan** oleh `_netfix.bat` (dikelola
WLAN service, langsung muncul lagi). Jadi jangan mengandalkan disable adapter. **Lever yang andal =
`MSMPI_NETMASK` dipatok ke subnet slave**, supaya smpd HANYA memakai `172.20.10.x` dan mengabaikan
semua `169.254.x`/`192.168.x`/IPv6 — tanpa perlu men-disable apa pun.

**Sudah diperbaiki di GUI + script (tidak perlu langkah manual):**

- GUI sekarang **selalu memakai IP hotspot master** di `mpiexec -hosts`, **tidak pernah hostname**
  (jebakan `::1` tidak kena). IP master dipilih yang **se-subnet dengan slave**.
- `MSMPI_NETMASK` di-set di **environment daemon `smpd`** (lewat `commands/_netmask.bat`), dan
  subnetnya **diturunkan dari `S1_IP` (IP slave)** — bukan dari default-route master (yang bisa salah
  kalau master juga tersambung internet di adapter lain). Pastikan `S1_IP` di `_config.bat` benar.
- `_netfix.bat` tetap dipakai best-effort (matikan IPv6 + APIPA yang bisa dimatikan), tapi bukan
  andalan utama.

> Kalau `_test_2pc.bat` mencetak `MSMPI_NETMASK=<bukan subnet hotspot>` (mis. `192.168.x`), berarti
> `S1_IP` di `_config.bat` belum diisi/ salah. Isi `S1_IP` dengan IP hotspot slave, ulangi.

Tes manual cepat (all-IP, paling pasti mengisolasi 1726) — jalankan di master, ganti IP sesuai
hotspot kalian:

```powershell
mpiexec -genv MSMPI_NETMASK 172.20.10.0/255.255.255.0 -hosts 2 <IP_MASTER_HOTSPOT> 1 172.20.10.2 1 hostname
```

Harus cetak **dua hostname** cepat. Kalau ini sukses tapi GUI tidak, berarti masalah di host list GUI
(pastikan slave berstatus `connected`). Kalau ini pun gagal 1726, lanjut cek IPv6/adapter di bawah.

Penyebab tambahan: **IPv6 didahulukan + host multi-homed.** Cek dengan:

```powershell
Resolve-DnsName <hostname-lawan>
```

Kalau muncul record `AAAA` (IPv6, mis. `2400:...` atau `fe80::`) di atas `A` (IPv4), smpd memakai jalur IPv6. Di hotspot HP, IPv6 cellular tidak bisa peer-to-peer, jadi manager smpd tak terjangkau → 1726. Adapter `169.254.x` (APIPA) dan VPN (Tailscale) memperparah.

Solusi (sudah otomatis lewat `commands/_netfix.bat` yang dipanggil tiap `run_*.bat`):

- Matikan IPv6 di semua adapter.
- Disable adapter Tailscale dan adapter ber-IP `169.254.x`.
- Sisakan hanya satu IPv4 `10.190.116.x` per PC.

Manual (kalau perlu, PowerShell as Admin, di tiap PC):

```powershell
Get-NetAdapter | Disable-NetAdapterBinding -ComponentID ms_tcpip6
Disable-NetAdapter -InterfaceAlias 'Tailscale' -Confirm:$false
Get-NetIPAddress -AddressFamily IPv4 | ? { $_.IPAddress -like '169.254.*' } |
  % { Disable-NetAdapter -InterfaceIndex $_.InterfaceIndex -Confirm:$false }
```

Setelah itu **restart smpd** (tutup window SMPD → jalankan ulang `run_*.bat`). `deactivate.bat` mengembalikan IPv6 dan adapter.

### Run/Compare Jalan Tapi Menggantung (tidak selesai-selesai)

Kalau process sudah ke-launch (command muncul) tapi tidak ada output dan tidak selesai, biasanya **deadlock di komunikasi antar-rank MPI** karena **host multi-homed**.

Tiap rank MPI harus saling membuka koneksi socket. Kalau sebuah PC punya banyak adapter — Wi-Fi, Ethernet, **VPN (Tailscale/ZeroTier)**, alamat **169.254.x.x** (APIPA), atau adapter virtual VMware/VirtualBox/Hyper-V — MPI bisa memberi slave alamat di adapter yang salah. Slave lalu mencoba connect ke alamat tak terjangkau dan menggantung di `MPI_Init` (sebelum ada output).

Solusi:

- GUI sudah otomatis menambahkan `-genv MSMPI_NETMASK <subnet>/255.255.255.0` (diambil dari IP slave yang di-Connect), memaksa MPI hanya memakai subnet hotspot. (Catatan penting: MS-MPI memakai `MSMPI_NETMASK`, **bukan** `MPICH_NETMASK` milik MPICH/Hydra — env var yang salah akan diabaikan diam-diam.)
- **Matikan VPN seperti Tailscale** selama run (paling sering jadi biang hang).
- Pakai **IP** (bukan hostname) saat Add manual, supaya subnet bisa diturunkan dengan benar.
- Tes manual dari master untuk memastikan:

```powershell
mpiexec -genv MSMPI_NETMASK 10.190.116.0/255.255.255.0 -hosts 2 <IP_MASTER> 1 <IP_SLAVE> 1 -wdir C:\tubes-2 C:\tubes-2\build\alchemy_mpi.exe --data C:\tubes-2\data\recipes.json --tiers C:\tubes-2\data\tiers.json --algorithm bfs --mode multiple --render json --output C:\tubes-2\results\cli_test --target Brick --limit 5 --split-depth 1
```

Ganti `<IP_MASTER>`/`<IP_SLAVE>` dengan IP di subnet yang sama, dan sesuaikan angka `255.255.255.0` bila subnet bukan /24.

### MPI Lokal Berhasil Tapi Multi-PC Gagal

Ini normal saat konfigurasi remote belum siap. Command lokal:

```powershell
mpiexec -n 4 build\alchemy_mpi.exe ...
```

bisa berhasil walaupun command multi-host:

```powershell
mpiexec -hosts 2 MASTER 4 SLAVE 2 build\alchemy_mpi.exe ...
```

masih gagal karena remote launch butuh izin jaringan dan konfigurasi tambahan.

## Ringkasan Paling Cepat

Slave:

1. Buka GUI.
2. Role `Slave`.
3. Set `Offered slots`.
4. Klik `Start Slave`.
5. Klik `Accept` saat master invite.

Master:

1. Buka GUI.
2. Role `Master`.
3. Engine `mpi`.
4. Centang `Use accepted master/slave host list`.
5. Scan atau add manual IP slave.
6. Klik `Connect`.
7. Tunggu status `connected`.
8. Klik `Run`.

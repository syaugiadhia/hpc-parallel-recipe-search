# Tutorial Multi-PC MPI Dengan GUI Master/Slave

Dokumen ini menjelaskan cara menjalankan project Little Alchemy HPC di 2 komputer atau lebih memakai GUI Python dan MS-MPI di Windows.

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
mpiexec -hosts 2 MASTER_HOST 4 192.168.137.24 2 build\alchemy_mpi.exe ...
```

Artinya ada 2 host:

- `MASTER_HOST` dengan 4 slots.
- `192.168.137.24` dengan 2 slots.

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

### Status Connected Tetapi MPI Gagal

Handshake GUI hanya membuktikan GUI master bisa bicara dengan GUI slave. Setelah itu, MS-MPI masih harus bisa meluncurkan process ke host slave.

Penyebab umum:

- MS-MPI belum terinstall di slave.
- `build\alchemy_mpi.exe` tidak ada di path yang sama.
- Firewall memblokir MS-MPI.
- Akun Windows atau konfigurasi MS-MPI belum mengizinkan launch remote.

Jika muncul:

```text
CreateRpcBinding error 1749
```

Artinya MS-MPI belum berhasil membuat koneksi remote host. Cek firewall, credential, hostname/IP, dan instalasi MS-MPI di komputer target.

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

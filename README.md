# Little Alchemy HPC Recipe Search

Project ini adalah refactor tugas Little Alchemy menjadi aplikasi HPC berbasis C++ dan MPI. Fokusnya bukan web app Go/React, tetapi perbandingan pencarian serial vs paralel distributed-memory, statistik performa, benchmark, dan visualisasi graf recipe.

## Deskripsi Masalah

Data recipe direpresentasikan sebagai dependency graph:

```text
result -> list of ingredient pairs
Brick  -> Mud + Fire, Clay + Stone, ...
```

Pencarian membangun recipe tree dari target sampai semua leaf mencapai elemen dasar:

```text
Air, Earth, Fire, Water
```

Walaupun data JSON memiliki recipe untuk elemen dasar, program memperlakukan empat elemen tersebut sebagai terminal/basic.

## Dependensi

- C++17 compiler
- CMake 3.16+
- nlohmann/json
- OpenMPI, MPICH, atau MS-MPI development package untuk executable MPI
- Graphviz untuk render PNG/SVG dari DOT
- Python 3 dan CustomTkinter untuk GUI wrapper opsional

Linux Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake nlohmann-json3-dev graphviz openmpi-bin libopenmpi-dev
```

MSYS2 UCRT64 Windows:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake
pacman -S mingw-w64-ucrt-x86_64-nlohmann-json mingw-w64-ucrt-x86_64-graphviz
pacman -S mingw-w64-ucrt-x86_64-msmpi
```

Paket OpenMPI tidak tersedia di semua repo MSYS2 UCRT64. Untuk Windows, gunakan MS-MPI; CMake `find_package(MPI)` dapat mendeteksi Microsoft MPI SDK jika runtime/SDK sudah terpasang.

Jika Graphviz tidak tersedia, program tetap membuat file `.dot` dan memberi warning bahwa render image dilewati.

Dependensi GUI:

```bash
pip install -r requirements.txt
```

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Jika MPI development package tidak ditemukan, CMake tetap dapat membangun `alchemy_serial` dan test, tetapi `alchemy_mpi` akan dilewati dengan warning.
Pada Windows/MS-MPI, pastikan `mpiexec.exe` tersedia di `PATH`.

## Menjalankan GUI

GUI CustomTkinter tersedia sebagai wrapper untuk executable C++/MPI. GUI tidak mengganti engine pencarian; semua komputasi tetap berjalan lewat `alchemy_serial` atau `alchemy_mpi`.

```bash
python gui/alchemy_gui.py
```

Fitur GUI:

- Pilih serial atau MPI.
- Pilih target atau benchmark.
- Pilih target berdasarkan tier resmi Fandom: `all`, `starter`, `special`, `tier1` sampai `tier15`.
- Search nama elemen supaya tidak perlu scroll daftar panjang.
- Atur algorithm, mode search, trace mode, visual mode, output format, dan output prefix.
- Atur MPI process count, split depth, dan baseline time.
- Pilih role `Master` atau `Slave`; master mengundang slave, slave harus accept dalam 30 detik, lalu MPI multi-komputer dijalankan dari master.
- Build project, run command, stop process, dan buka file JSON/DOT/image/CSV.
- Target dropdown otomatis dimuat dari `data/tiers.json`, lalu divalidasi terhadap `recipes.json`.

## Menjalankan Serial

```bash
./build/alchemy_serial \
  --data data/recipes.json \
  --target "Brick" \
  --algorithm bfs \
  --mode multiple \
  --limit 5 \
  --trace-mode memo \
  --visual-mode shared \
  --output results/brick_serial
```

Via script:

```bash
./scripts/run_serial.sh --target "Brick" --algorithm dfs --visual-mode full
```

## Menjalankan MPI Satu Komputer

Linux/OpenMPI atau MPICH:

```bash
mpirun -np 4 ./build/alchemy_mpi \
  --data data/recipes.json \
  --target "Brick" \
  --algorithm bfs \
  --mode multiple \
  --limit 10 \
  --trace-mode memo \
  --visual-mode shared \
  --split-depth 2 \
  --output results/brick_mpi_np4
```

Windows/MS-MPI:

```bash
mpiexec -n 4 ./build/alchemy_mpi.exe \
  --data data/recipes.json \
  --target "Brick" \
  --algorithm bfs \
  --mode multiple \
  --limit 10 \
  --trace-mode memo \
  --visual-mode shared \
  --split-depth 2 \
  --output results/brick_mpi_np4
```

Via script:

```bash
./scripts/run_mpi_local.sh --np auto --target "Brick" --limit 10
./scripts/run_mpi_local.sh --np 4 --target "Brick" --split-depth 2
```

`--np auto` memakai `nproc`, atau fallback ke `4`.

## Menjalankan MPI Multi-Komputer

Panduan lengkap GUI master/slave ada di [`tutor_multi_pc.md`](tutor_multi_pc.md).

Gunakan hostfile, misalnya `hosts.txt`:

```text
localhost slots=4
node2 slots=4
```

Jalankan:

```bash
./scripts/run_mpi_hosts.sh \
  --hostfile hosts.txt \
  --np 8 \
  --target "Brick" \
  --algorithm bfs \
  --mode multiple \
  --limit 10 \
  --trace-mode memo \
  --visual-mode shared
```

Windows/MS-MPI juga bisa memakai script PowerShell:

```powershell
python scripts\scan_hosts.py --output hosts.txt
powershell -ExecutionPolicy Bypass -File scripts\run_mpi_hosts.ps1 -Hostfile hosts.txt --target Brick --limit 10
```

Di GUI, buka aplikasi di semua komputer. Laptop utama pilih role `Master`; laptop teman pilih role `Slave`.

1. Di komputer slave, klik `Start Slave`.
2. Di komputer master, pilih engine `mpi`, centang `Use accepted master/slave host list`.
3. Klik `Scan LAN` atau masukkan IP slave secara manual.
4. Klik `Connect` pada IP slave. Slave akan menerima invite dan harus klik `Accept` dalam 30 detik.
5. Atur `slots` master dan slave sesuai kebutuhan.
6. Klik `Run` dari komputer master.

Untuk multi-node, pastikan:

- MS-MPI atau MPI yang sesuai terinstall di semua komputer.
- Path project dan `data/recipes.json` tersedia di semua komputer.
- `build/alchemy_mpi.exe`, `data/recipes.json`, dan `data/tiers.json` ada pada path project yang sama di semua komputer.
- Firewall mengizinkan MS-MPI, TCP `50555` untuk invite slave, UDP `50556` untuk discovery GUI, dan ping/ICMP jika ingin memakai auto scan. Jika ping diblokir, tambahkan host manual di GUI.
- SSH passwordless antar node sudah siap untuk OpenMPI/MPICH. Untuk MS-MPI, pastikan konfigurasi host/user sesuai mekanisme Microsoft MPI.
- Hostfile menentukan jumlah process per komputer lewat `slots`.
- Dalam GUI role baru, host pertama selalu komputer master sehingga rank 0 berada di laptop master. Slave hanya dipakai jika statusnya `connected`.

Jika MS-MPI menampilkan `CreateRpcBinding error 1749` saat memakai `mpiexec -hosts`, berarti mode launch ke host tersebut belum berhasil dibuat oleh MS-MPI. Cek hostname/IP, firewall, izin akun, dan konfigurasi MS-MPI di komputer target. `mpiexec -n N` lokal bisa tetap berhasil walaupun mode `-hosts` belum siap.

Script `run_mpi_hosts.sh` menerima format `host slots=N` yang sama. Jika mendeteksi Microsoft MPI, script mengubah hostfile menjadi argumen MS-MPI:

```bash
mpiexec -hosts 2 localhost 4 node2 4 ./build/alchemy_mpi.exe ...
```

Untuk OpenMPI/MPICH, script tetap memakai hostfile langsung:

```bash
mpirun -np 8 --hostfile hosts.txt ./build/alchemy_mpi ...
```

## Algoritma

### DFS

DFS mengikuti urutan recipe sesuai JSON. Setiap result mencoba recipe pair pertama, lalu turun ke ingredient sampai elemen dasar. Cycle dicegah dengan recursion stack; branch yang menyebabkan cycle dianggap invalid.

Contoh:

```bash
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm dfs --mode multiple --limit 5 --trace-mode memo --visual-mode shared --output results/brick_dfs
```

### BFS

BFS memakai iterative depth-limited expansion. Hasil dengan depth lebih pendek ditemukan lebih dulu.

Contoh:

```bash
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm bfs --mode multiple --limit 5 --trace-mode memo --visual-mode full --output results/brick_bfs
```

## Memilih Target Dan Tier

Project menyertakan katalog tier resmi di `data/tiers.json`.

- `starter`: Air, Earth, Fire, Water.
- `special`: Time.
- `tier1` sampai `tier15`: daftar element Fandom yang sudah diekstrak.

Untuk `data/recipes.json` bawaan, katalog divalidasi harus berisi 720 elemen unik dan semua nama harus ada di data recipe. Jika memakai JSON kecil/custom untuk eksperimen, search tetap bisa berjalan dengan warning validasi; `--list-elements` tetap memakai validasi strict karena fitur itu memang bergantung pada katalog tier resmi.

CLI bisa dipakai untuk mencari nama target sebelum menjalankan search:

```bash
./build/alchemy_serial --data data/recipes.json --tiers data/tiers.json --list-elements --tier starter
./build/alchemy_serial --data data/recipes.json --tiers data/tiers.json --list-elements --tier special
./build/alchemy_serial --data data/recipes.json --tiers data/tiers.json --list-elements --tier tier6 --filter light
./build/alchemy_serial --data data/recipes.json --tiers data/tiers.json --list-elements --tier all --filter sword
```

`--tier` dan `--filter` hanya membantu memilih target. Pencarian recipe tetap ditentukan oleh `--target`, `--algorithm`, dan `--mode`.

## Mode Pencarian

- `--mode single`: berhenti pada satu recipe valid.
- `--mode multiple --limit N`: mencari beberapa recipe valid sampai limit.
- `--mode all`: mengambil semua recipe pair langsung untuk target dari `recipes.json`. Opsi `--limit` diabaikan.

Pada `--mode all`, setiap recipe langsung target tetap diexpand sampai leaf terminal (`Air`, `Earth`, `Fire`, `Water`, atau `Time`), tetapi ingredient yang punya banyak cara pembuatan hanya memakai satu subtree representatif. BFS memilih subtree terpendek; DFS memilih subtree pertama sesuai urutan JSON.

Recipe duplikat dihapus berdasarkan signature struktur tree. Ingredient pair diperlakukan sebagai kombinasi, jadi struktur dengan child tertukar dianggap duplikat.

Shortcut pemakaian:

```bash
# Salah satu recipe pertama menurut DFS / urutan JSON
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm dfs --mode single --trace-mode memo --visual-mode shared --output results/brick_one

# Recipe terpendek menurut kedalaman BFS
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm bfs --mode single --trace-mode memo --visual-mode shared --output results/brick_shortest

# Top N recipe dengan BFS
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm bfs --mode multiple --limit 10 --trace-mode memo --visual-mode shared --output results/brick_top10

# Semua recipe langsung target dari JSON
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm bfs --mode all --trace-mode memo --visual-mode shared --output results/brick_all
```

Special element seperti `Time` diperlakukan sebagai terminal valid:

```bash
./build/alchemy_serial --data data/recipes.json --target "Time" --algorithm bfs --mode single --trace-mode memo --visual-mode shared --output results/time
```

## Trace Mode

Trace mode memengaruhi proses komputasi.

- `--trace-mode full`: setiap kemunculan elemen dihitung ulang. Cache hit selalu `0`.
- `--trace-mode memo`: subtree valid disimpan di cache. Kemunculan elemen/subtree berikutnya dapat memakai cache, sehingga `nodes_visited` turun dan `cache_hits` naik.

Contoh:

```bash
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm bfs --mode multiple --limit 5 --trace-mode full --visual-mode full --output results/brick_trace_full
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm bfs --mode multiple --limit 5 --trace-mode memo --visual-mode full --output results/brick_trace_memo
```

## Visual Mode

Visual mode hanya memengaruhi gambar, bukan hasil pencarian.

- `--visual-mode full`: setiap kemunculan subtree digambar penuh sampai basic elements.
- `--visual-mode shared`: kemunculan pertama subtree digambar penuh, kemunculan berikutnya digambar sebagai reference dashed/shared.

Contoh konsep:

```text
A = B + C
A = C + D
C = Fire + Water
```

Pada visual full, `C` di kedua recipe digambar lengkap. Pada visual shared, `C` pertama digambar lengkap dan `C` kedua diberi label `shared, see Recipe 1`.

Contoh command:

```bash
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm bfs --mode multiple --limit 5 --trace-mode memo --visual-mode full --output results/brick_visual_full
./build/alchemy_serial --data data/recipes.json --target "Brick" --algorithm bfs --mode multiple --limit 5 --trace-mode memo --visual-mode shared --output results/brick_visual_shared
```

Kombinasi legal:

```bash
--trace-mode full --visual-mode full
--trace-mode full --visual-mode shared
--trace-mode memo --visual-mode full
--trace-mode memo --visual-mode shared
```

Gunakan `--max-visual-depth N` jika graph terlalu besar. Node yang dipotong akan diberi label `truncated`.

## Output

Untuk prefix `results/brick`, program membuat:

- `results/brick.dot`
- `results/brick.png` atau `results/brick.svg` jika Graphviz tersedia
- `results/brick.json`

Contoh JSON:

```json
{
  "target": "Brick",
  "algorithm": "bfs",
  "mode": "multiple",
  "trace_mode": "memo",
  "visual_mode": "shared",
  "limit": 5,
  "recipes_found": 5,
  "statistics": {
    "time_ms": 12.45,
    "nodes_visited": 183,
    "cache_hits": 27,
    "cache_entries": 42,
    "processes": 1
  },
  "recipes": []
}
```

## Implementasi MPI

Rank 0 adalah master. Rank lain adalah worker.

Semua rank membaca `recipes.json` sendiri. Pilihan ini lebih sederhana untuk multi-node karena tidak perlu broadcast data graph besar; syaratnya path project dan file data tersedia di semua node.

Pembagian kerja:

1. Master membuat partial recipe task dari target.
2. `--split-depth N` memperluas task sampai kedalaman N agar jumlah task cukup untuk banyak process.
3. Worker mengirim request task ke master.
4. Master mengirim task atau stop.
5. Worker menyelesaikan subtree secara lokal dengan DFS/BFS dan trace mode yang dipilih.
6. Worker mengirim recipe valid dan statistik ke master.
7. Master menghapus duplikat, membatasi sampai limit, dan membuat DOT/JSON/image akhir.

Khusus `--mode all`, master membuat satu task untuk setiap recipe langsung target dari JSON dan `--split-depth` diabaikan agar mode ini tetap berarti "semua recipe langsung", bukan semua kombinasi subtree.

Contoh `split-depth`: target `Brick` punya beberapa recipe langsung seperti `Brick = Mud + Fire` dan `Brick = Clay + Stone`. Dengan `--split-depth 1`, task biasanya berasal dari recipe langsung `Brick`. Dengan `--split-depth 2`, master juga membuka ingredient seperti `Clay` atau `Stone`, sehingga task lebih banyak dan banyak komputer/rank lebih mudah kebagian kerja. Nilai terlalu besar bisa menambah overhead task/komunikasi.

Memoization MPI bersifat lokal per worker dan dipertahankan selama worker memproses task-task untuk target yang sama.

Statistik MPI mencakup:

- total rank/process
- hostname per rank
- total time
- communication time
- nodes visited total/per rank
- tasks processed total/per rank
- cache hits total/per rank
- cache entries total
- speedup dan efficiency jika `--baseline-ms` diberikan

## Benchmark

Target benchmark ada di `benchmarks/targets.txt`.

Serial:

```bash
./build/alchemy_serial \
  --benchmark benchmarks/targets.txt \
  --data data/recipes.json \
  --algorithm bfs \
  --mode multiple \
  --limit 10 \
  --trace-mode full \
  --visual-mode full \
  --output results/bench_serial
```

MPI:

Linux/OpenMPI atau MPICH:

```bash
mpirun -np 4 ./build/alchemy_mpi \
  --benchmark benchmarks/targets.txt \
  --data data/recipes.json \
  --algorithm bfs \
  --mode multiple \
  --limit 10 \
  --trace-mode memo \
  --visual-mode shared \
  --split-depth 2 \
  --output results/bench_mpi_np4
```

Windows/MS-MPI:

```bash
mpiexec -n 4 ./build/alchemy_mpi.exe \
  --benchmark benchmarks/targets.txt \
  --data data/recipes.json \
  --algorithm bfs \
  --mode multiple \
  --limit 10 \
  --trace-mode memo \
  --visual-mode shared \
  --split-depth 2 \
  --output results/bench_mpi_np4
```

Script benchmark:

```bash
./scripts/benchmark.sh
```

CSV benchmark berisi:

```text
target,algorithm,mode,trace_mode,visual_mode,processes,recipes_found,nodes_visited,cache_hits,cache_entries,time_ms,tasks_processed,communication_ms,output_dot,output_image
```

Cara membaca:

- `time_ms`: waktu pencarian.
- `nodes_visited`: jumlah node/subproblem yang dievaluasi.
- `cache_hits`: berapa kali memoization dipakai.
- `tasks_processed`: jumlah task MPI yang selesai.
- `communication_ms`: estimasi waktu komunikasi MPI.
- `speedup`: `serial_time / parallel_time`, tersedia di output JSON jika `--baseline-ms` diisi.
- `efficiency`: `speedup / processes`.

## Format JSON Input

Loader mendukung tiga bentuk:

Format A:

```json
{
  "Brick": [["Mud", "Fire"], ["Clay", "Stone"]]
}
```

Format B:

```json
[
  {"result": "Brick", "ingredients": ["Mud", "Fire"]},
  {"result": "Brick", "ingredients": ["Clay", "Stone"]}
]
```

Format data project ini:

```json
[
  {
    "name": "Brick",
    "recipes": [
      {"elements": ["Mud", "Fire"]},
      {"elements": ["Clay", "Stone"]}
    ]
  }
]
```

Nama input target case-insensitive, tetapi output memakai nama asli dari JSON.

## Batasan

- `single` dan `multiple` dibatasi oleh `--limit`; graph Little Alchemy memiliki branching besar sehingga meminta terlalu banyak recipe bisa mahal.
- `--mode all` tidak memakai batas `--limit`, tetapi hanya mengambil recipe langsung target dari JSON; gunakan `multiple --limit N` jika ingin eksplorasi lebih banyak kombinasi subtree.
- MPI task generation berbasis partial tree dari `--split-depth`; target dengan sedikit cabang tetap bisa memiliki paralelisme terbatas.
- Cache MPI lokal per rank, belum distributed/shared cache antar rank.
- Graphviz render tergantung command `dot` tersedia di `PATH`.

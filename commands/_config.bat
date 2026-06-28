@echo off
:: =====================================================================
:: KONFIGURASI TERPUSAT MULTI-PC MS-MPI
:: Edit HANYA file ini. Dipanggil oleh run_master.bat / run_slaveN.bat
:: / deactivate.bat lewat perintah "call".
:: =====================================================================

:: Path project (HARUS sama persis di semua PC)
set "PROJECT_DIR=C:\tubes-2"

:: Lokasi binary MS-MPI (folder yang berisi mpiexec.exe & smpd.exe)
set "MSMPI_BIN=D:\Program Files\Microsoft MPI\Bin"

:: Python yang punya dependency GUI (customtkinter, pillow). Pakai path lengkap
:: karena GUI dijalankan sebagai akun cluster. Kosongkan untuk auto-detect.
set "PYTHON_EXE=D:\Program Files\Python310\python.exe"

:: =====================================================================
:: AKUN CLUSTER (INTI MULTI-PC). MS-MPI butuh smpd+mpiexec dijalankan
:: sebagai akun Windows yang SAMA (nama+password) di SEMUA PC, supaya
:: auth dua-arah smpd lolos (slave manager connect-back ke master).
:: Script auto-membuat akun ini (admin) di tiap PC saat run_*.bat, dan
:: menjalankan smpd + GUI sebagai akun ini. Hapus dgn remove_users.bat.
:: PASSWORD di sini HARUS sama dengan password akun itu di tiap PC.
:: =====================================================================
set "CLUSTER_USER=hp"
set "CLUSTER_PASS=mrdx"

:: --------- MASTER (untuk pemetaan hosts di SEMUA PC, termasuk slave) ---------
:: Isi hostname + IP PC master. Dipakai _hostsfix supaya slave bisa resolve
:: nama master (bantu handshake smpd dua arah / 1726). IP boleh dikosongkan
:: di master sendiri (auto-detect), tapi WAJIB diisi supaya slave tahu master.
set "MASTER_HOST=HP240G7-2021"
set "MASTER_IP=192.168.2.168"

:: --------- SLAVE 1 ---------
:: Tambahkan slave di GUI master memakai HOSTNAME ini (cocok dgn cmdkey).
set "S1_HOST=desktop-s3pfjin"
set "S1_USER=desktop-s3pfjin\hp"
set "S1_PASS=mrdx"
set "S1_SLOTS=8"
:: (opsional) IP slave 1. Boleh dikosongkan. Isi hanya kalau mau daftarkan cmdkey
:: by-IP juga / mau pin entri hosts file, mis. 192.168.137.24
set "S1_IP=192.168.2.101"

:: --------- SLAVE 2 ---------
set "S2_HOST=laptop-a5v4phah"
set "S2_USER=laptop-a5v4phah\hallo"
set "S2_PASS=12345678"
set "S2_SLOTS=20"
:: (opsional) IP slave 2. Boleh dikosongkan.
set "S2_IP="

:: Prefix subnet cluster (opsional, akhiri titik). KOSONGKAN untuk AUTO-DETECT
:: dari adapter default-route (disarankan). Isi hanya kalau mau memaksa subnet
:: tertentu saat satu PC punya banyak IP "asli", mis. set "CLUSTER_PREFIX=192.168.137."
set "CLUSTER_PREFIX="

:: Jumlah debug log smpd (0=diam/cepat, 3=verbose).
:: Pakai 0 untuk run normal (verbose 3 bikin lambat & output penuh log).
:: Naikkan ke 3 hanya saat mau debug masalah koneksi.
set "SMPD_DEBUG=0"

:: Paksa NTLM (bukan Kerberos). WAJIB di jaringan workgroup/hotspot tanpa domain,
:: kalau tidak smpd error 1726 "RestrictedKrbHost ... unable to connect".
:: Dibaca oleh smpd & mpiexec saat start (lewat environment), bukan via -genv.
set "MPIEXEC_DISABLE_KERB=2"

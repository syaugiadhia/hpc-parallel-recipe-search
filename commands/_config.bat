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

:: --------- SLAVE 1 ---------
:: Tambahkan slave di GUI master memakai HOSTNAME ini (cocok dgn cmdkey).
set "S1_HOST=desktop-s3pfjin"
set "S1_USER=desktop-s3pfjin\hp"
set "S1_PASS=mrdx"
set "S1_SLOTS=8"
:: (opsional) IP slave 1. Boleh dikosongkan. Isi hanya kalau mau daftarkan cmdkey
:: by-IP juga / mau pin entri hosts file, mis. 192.168.137.24
set "S1_IP="

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

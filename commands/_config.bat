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
set "S1_HOST=desktop-s3pfjin"
set "S1_USER=desktop-s3pfjin\hp"
set "S1_PASS=mrdx"
set "S1_SLOTS=2"
:: (opsional) IP slave 1, isi kalau mau daftarkan cmdkey by-IP juga, mis. 192.168.137.24
set "S1_IP="

:: --------- SLAVE 2 ---------
set "S2_HOST=laptop-a5v4phah"
set "S2_USER=laptop-a5v4phah\hallo"
set "S2_PASS=12345678"
set "S2_SLOTS=2"
:: (opsional) IP slave 2
set "S2_IP="

:: Jumlah debug log smpd (0=diam, 3=verbose). 3 enak buat lihat aktivitas jaringan.
set "SMPD_DEBUG=3"

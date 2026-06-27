@echo off
:: Konfigurasi
:: Edit HANYA file ini. Dipanggil oleh run_master.bat / run_slaveN.bat
:: / deactivate.bat lewat perintah "call".

:: Path project (harus sama persis di semua PC)
set "PROJECT_DIR=C:\tubes-2"

:: Lokasi binary MS-MPI (folder yang berisi mpiexec.exe & smpd.exe)
:: Dipakai apa adanya kalau di-set; kalau folder tidak ada, run_*.bat fallback ke %MSMPI_BIN%.
set "MSMPI_BIN=C:\Program Files\Microsoft MPI\Bin"

:: ------------------------------------------------------------------
:: SLAVE 1  (diisi DI master; di slave boleh dibiarkan kosong)
:: S1_HOST : hostname slave (mis. desktop-s3pfjin). Pakai hostname, bukan IP,
::           supaya cocok dengan target cmdkey. Kalau pakai IP, isi juga S1_IP.
:: S1_USER : akun Windows di slave dalam bentuk HOST\user (mis. desktop-s3pfjin\HP)
:: S1_PASS : password akun itu
:: S1_SLOTS: jumlah process MPI di slave (mis. 2)
:: S1_IP   : (opsional) IP slave kalau master meng-Connect lewat IP
:: ------------------------------------------------------------------
set "S1_HOST="
set "S1_USER="
set "S1_PASS="
set "S1_SLOTS=2"
set "S1_IP="

:: SLAVE 2 (opsional, format sama seperti SLAVE 1)
set "S2_HOST="
set "S2_USER="
set "S2_PASS="
set "S2_SLOTS=2"
set "S2_IP="

:: Jumlah debug log smpd (0=diam, 3=verbose)
set "SMPD_DEBUG=0"

set "MPIEXEC_DISABLE_KERB=2"

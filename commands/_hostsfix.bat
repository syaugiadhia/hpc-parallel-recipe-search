@echo off
:: =====================================================================
:: HOSTS FIX: petakan hostname master + semua slave ke IPv4 langsung
:: di SETIAP PC (resolusi nama dua arah utk smpd; bantu 1726).
:: Logika ada di _hostsfix.ps1 (lebih andal daripada one-liner di .bat;
:: versi one-liner lama crash "Stream was not readable").
:: Idempotent (# tubes-mpi). deactivate.bat menghapus baris itu.
:: Dipanggil "call" oleh run_*.bat sesudah _netfix.bat.
:: =====================================================================

echo [hosts] Menulis pemetaan hostname -^> IPv4 (master + slave) ke hosts file...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_hostsfix.ps1"

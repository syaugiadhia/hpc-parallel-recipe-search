@echo off
net session >nul 2>&1
if %errorLevel% == 0 (
    echo Menjalankan dengan hak akses Administrator.
) else (
    echo Klik kanan file ini lalu pilih 'Run as Administrator'!
    pause
    exit
)

echo Mengembalikan settingan keamanan Windows (Registry)
reg delete HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v LocalAccountTokenFilterPolicy /f

echo Menyalakan kembali Firewall
netsh advfirewall set allprofiles state on

echo Menghapus Kredensial yang tersimpan
:: Isi nama host teman yang tadi didaftarkan
set host1=

:: Hapus versi standar
cmdkey /delete:%host1% >nul 2>&1

:: Hapus versi TERMSRV
cmdkey /delete:TERMSRV/%host1% >nul 2>&1

echo.
echo Setting Laptop udah dikembalikan
pause
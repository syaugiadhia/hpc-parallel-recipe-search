@echo off
net session >nul 2>&1
if %errorLevel% == 0 (
    echo Menjalankan dengan mode Administrator.
) else (
    echo Gunakan 'Run as Administrator'!
    pause
    exit
)

echo Menyiapkan Registry untuk akses Remote MPI
reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v LocalAccountTokenFilterPolicy /t REG_DWORD /d 1 /f

echo Menyesuaikan Setting Firewall
netsh advfirewall set allprofiles state off

echo .
echo Setting awal selesai
pause
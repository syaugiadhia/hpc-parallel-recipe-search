@echo off
:: Smoke test loopback MS-MPI (1 PC) untuk membuktikan jalur `mpiexec -hosts` jalan.
:: Tidak butuh PC kedua. Memakai smpd lokal (port 8677) -> persis mekanisme multi-node.
setlocal

call "%~dp0_config.bat"

if not exist "%MSMPI_BIN%\smpd.exe" (
    echo [error] smpd.exe tidak ada di "%MSMPI_BIN%". Install MS-MPI / perbaiki _config.bat.
    pause
    exit /b 1
)
set "EXE=%PROJECT_DIR%\build\alchemy_mpi.exe"
if not exist "%EXE%" (
    echo [error] %EXE% belum ada. Build dulu: cmake --build build
    pause
    exit /b 1
)

echo [1/4] Stop MsMpiLaunchSvc (bebaskan port 8677)...
net stop MsMpiLaunchSvc >nul 2>&1

echo [2/4] Start smpd daemon (window terpisah)...
start "SMPD-smoke" "%MSMPI_BIN%\smpd.exe" -d %SMPD_DEBUG%
:: beri waktu smpd listen
ping -n 3 127.0.0.1 >nul

echo [3/4] mpiexec -hosts 1 localhost 2 (loopback)...
mpiexec -hosts 1 localhost 2 "%EXE%" --data "%PROJECT_DIR%\data\recipes.json" --tiers "%PROJECT_DIR%\data\tiers.json" --algorithm bfs --mode multiple --render json --output "%PROJECT_DIR%\results\smoke_localhost" --target Brick --limit 5 --split-depth 1
echo     exit=%errorlevel%

echo [4/4] mpiexec -hosts 1 %COMPUTERNAME% 2 (remote-style ke diri sendiri lewat smpd)...
mpiexec -hosts 1 %COMPUTERNAME% 2 "%EXE%" --data "%PROJECT_DIR%\data\recipes.json" --tiers "%PROJECT_DIR%\data\tiers.json" --algorithm bfs --mode multiple --render json --output "%PROJECT_DIR%\results\smoke_hostname" --target Brick --limit 5 --split-depth 1
echo     exit=%errorlevel%

echo.
echo Selesai. Lihat baris "Rank hostnames" pada output di atas: harus memuat hostname,
echo "Processes: 2", dan "Recipes found" lebih dari 0. Tutup window SMPD-smoke saat selesai.
pause
endlocal

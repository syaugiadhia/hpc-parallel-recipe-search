# =====================================================================
# Jalankan SESI MPI sebagai AKUN CLUSTER (CLUSTER_USER) di PC ini:
#   1) restart smpd -d  sebagai CLUSTER_USER (window terpisah)
#   2) buka GUI (python alchemy_gui.py + GUI_ARGS) sebagai CLUSTER_USER
# Kenapa: MS-MPI butuh smpd+mpiexec berjalan sebagai akun yang sama di
# semua node, supaya slave bisa connect-back ke master (hindari error 1726
# "connect back to parent error 5"). Kalau user yang menjalankan SUDAH
# CLUSTER_USER, dijalankan langsung tanpa kredensial.
# Env dari _config.bat: MSMPI_BIN, PYTHON_EXE, PROJECT_DIR, SMPD_DEBUG,
#   CLUSTER_USER, CLUSTER_PASS, GUI_ARGS.
# =====================================================================
$ErrorActionPreference = 'Stop'

$u    = $env:CLUSTER_USER
$pass = $env:CLUSTER_PASS
$proj = $env:PROJECT_DIR
$smpd = Join-Path $env:MSMPI_BIN 'smpd.exe'
$dbg  = if ($env:SMPD_DEBUG) { $env:SMPD_DEBUG } else { '0' }
$guiArgs = @("$proj\gui\alchemy_gui.py") + (($env:GUI_ARGS -split '\s+') | Where-Object { $_ })

function Test-Py($exe) {
    if (-not $exe) { return $false }
    if (-not (Test-Path $exe)) { return $false }
    try { (& $exe -c "import customtkinter,PIL" 2>$null); return ($LASTEXITCODE -eq 0) } catch { return $false }
}

# --- pilih python yang punya dependency GUI ---
$py = $env:PYTHON_EXE
if (-not (Test-Py $py)) {
    Write-Host "[session] PYTHON_EXE tidak valid / tanpa customtkinter -> auto-detect..."
    $cands = @()
    try { $cands += (cmd /c "py -0p" 2>$null | ForEach-Object { ($_ -split '\s{2,}')[-1] }) } catch {}
    try { $cands += (where.exe python 2>$null) } catch {}
    $py = $cands | Where-Object { Test-Py $_ } | Select-Object -First 1
}
if (-not $py) { throw "Tidak menemukan python dengan customtkinter+pillow. Set PYTHON_EXE di _config.bat." }
# pakai pythonw bila ada (tanpa console window)
$pyw = Join-Path (Split-Path $py) 'pythonw.exe'
$guiExe = if (Test-Path $pyw) { $pyw } else { $py }
Write-Host "[session] python GUI = $guiExe"

# --- perlu kredensial? (kalau user sekarang sudah CLUSTER_USER, tidak perlu) ---
$me = ([Security.Principal.WindowsIdentity]::GetCurrent().Name).Split('\')[-1]
$useCred = ($me -ne $u)
$cred = $null
if ($useCred) {
    if (-not $u -or -not $pass) { throw "CLUSTER_USER/CLUSTER_PASS kosong di _config.bat." }
    $cred = New-Object System.Management.Automation.PSCredential($u, (ConvertTo-SecureString $pass -AsPlainText -Force))
    Write-Host "[session] menjalankan smpd + GUI sebagai '$u' (user sekarang '$me')."
} else {
    Write-Host "[session] user sekarang sudah '$u', jalan langsung tanpa kredensial."
}

# --- restart smpd sebagai CLUSTER_USER ---
Get-Process smpd -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800
if ($useCred) {
    Start-Process -FilePath $smpd -ArgumentList '-d', $dbg -Credential $cred -WorkingDirectory $proj
} else {
    Start-Process -FilePath $smpd -ArgumentList '-d', $dbg -WorkingDirectory $proj
}
Start-Sleep -Seconds 2
Write-Host "[session] smpd (re)started sebagai '$u' (JANGAN tutup window smpd)."

# --- buka GUI sebagai CLUSTER_USER ---
if ($useCred) {
    Start-Process -FilePath $guiExe -ArgumentList $guiArgs -Credential $cred -WorkingDirectory $proj
} else {
    Start-Process -FilePath $guiExe -ArgumentList $guiArgs -WorkingDirectory $proj
}
Write-Host "[session] GUI dibuka sebagai '$u': $guiExe $($guiArgs -join ' ')"

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

function Test-PyExists($exe) {
    return ($exe -and (Test-Path $exe))
}
function Test-Py($exe) {
    if (-not (Test-PyExists $exe)) { return $false }
    try { & $exe -c "import customtkinter,PIL" 2>$null; return ($LASTEXITCODE -eq 0) } catch { return $false }
}

# Kumpulkan SEMUA kandidat python di PC ini. Utamakan python sistem (Program Files /
# C:\PythonXX) supaya bisa diakses akun cluster 'hp' saat GUI dijalankan sebagai hp,
# bukan python di profil user lain.
function Find-AnyPython {
    $list = @()
    if ($env:PYTHON_EXE) { $list += $env:PYTHON_EXE }
    foreach ($pat in @(
            "C:\Program Files\Python3*\python.exe",
            "D:\Program Files\Python3*\python.exe",
            "C:\Program Files\Python\Python3*\python.exe",
            "D:\Program Files\Python\Python3*\python.exe",
            "C:\Python3*\python.exe", "D:\Python3*\python.exe",
            "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe")) {
        try { $list += (Get-ChildItem $pat -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }) } catch {}
    }
    try { $list += ((cmd /c "py -0p" 2>$null) | ForEach-Object { if ($_ -match '([A-Za-z]:\\.+\.exe)') { $matches[1].Trim() } }) } catch {}
    try { $list += (where.exe python 2>$null) } catch {}
    return ($list | Where-Object { (Test-PyExists $_) -and ($_ -notmatch 'WindowsApps') } | Select-Object -Unique)
}

# --- pilih python yang punya dependency GUI; kalau belum ada, AUTO-INSTALL ---
$cands = Find-AnyPython
Write-Host ("[session] kandidat python: " + ($cands -join '  |  '))
$py = $cands | Where-Object { Test-Py $_ } | Select-Object -First 1
if (-not $py) {
    $target = $cands | Select-Object -First 1   # utamakan python sistem (urutan di atas)
    if ($target) {
        Write-Host "[session] customtkinter/pillow belum ada -> auto-install ke: $target (butuh internet)..."
        & $target -m pip install --quiet --upgrade pip 2>$null
        & $target -m pip install --quiet customtkinter pillow
        if (Test-Py $target) { $py = $target; Write-Host "[session] auto-install sukses." }
        else { Write-Host "[session] auto-install gagal." }
    }
}
if (-not $py) {
    throw "Tidak ada python dengan customtkinter+pillow & auto-install gagal. Install Python (mis. dari python.org) lalu jalankan lagi, atau set PYTHON_EXE di _config.bat."
}
# pakai pythonw bila ada (tanpa console window)
$pyw = Join-Path (Split-Path $py) 'pythonw.exe'
$guiExe = if (Test-Path $pyw) { $pyw } else { $py }
Write-Host "[session] python GUI = $guiExe"

# --- siapa jalan sebagai siapa? ---
# smpd WAJIB jalan sebagai CLUSTER_USER (hp): saat master (hp) connect ke smpd slave,
#   user koneksi HARUS cocok dgn pemilik smpd, kalau tidak ditolak (access denied / 1726).
# GUI tidak perlu hp. Di SLAVE (SESSION_AS_LOGIN=1) GUI jalan sebagai USER LOGIN supaya
#   window-nya TAMPIL (GUI yang dijalankan sbg user lain tak terlihat di desktop user login).
$me = ([Security.Principal.WindowsIdentity]::GetCurrent().Name).Split('\')[-1]
$needHp   = ($me -ne $u)                                       # perlu kredensial utk jadi hp?
$smpdCred = $needHp                                            # smpd: SELALU hp
$guiCred  = $needHp -and ($env:SESSION_AS_LOGIN -ne '1')       # GUI: hp, kecuali slave
$cred = $null
if ($needHp) {
    if (-not $u -or -not $pass) { throw "CLUSTER_USER/CLUSTER_PASS kosong di _config.bat." }
    $cred = New-Object System.Management.Automation.PSCredential($u, (ConvertTo-SecureString $pass -AsPlainText -Force))
}
if ($env:SESSION_AS_LOGIN -eq '1') {
    Write-Host "[session] mode SLAVE: smpd sebagai '$u' (wajib utk auth), GUI sebagai user login '$me' (supaya terlihat)."
} elseif ($needHp) {
    Write-Host "[session] smpd + GUI sebagai '$u' (user login '$me')."
} else {
    Write-Host "[session] user login sudah '$u', jalan langsung."
}

# --- restart smpd (selalu sebagai hp bila user login berbeda) ---
Get-Process smpd -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800
if ($smpdCred) {
    Start-Process -FilePath $smpd -ArgumentList '-d', $dbg -Credential $cred -WorkingDirectory $proj
} else {
    Start-Process -FilePath $smpd -ArgumentList '-d', $dbg -WorkingDirectory $proj
}
Start-Sleep -Seconds 2
$smpdAs = if ($smpdCred) { $u } else { $me }
Write-Host "[session] smpd jalan sebagai '$smpdAs' (JANGAN tutup; di slave-beda-user window smpd bisa tak terlihat tapi tetap jalan)."

# --- buka GUI (sebagai user login di slave supaya terlihat) ---
if ($guiCred) {
    Start-Process -FilePath $guiExe -ArgumentList $guiArgs -Credential $cred -WorkingDirectory $proj
} else {
    Start-Process -FilePath $guiExe -ArgumentList $guiArgs -WorkingDirectory $proj
}
$guiAs = if ($guiCred) { $u } else { $me }
Write-Host "[session] GUI dibuka sebagai '$guiAs': $guiExe $($guiArgs -join ' ')"

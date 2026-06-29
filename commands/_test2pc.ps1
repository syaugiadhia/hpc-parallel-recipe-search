# =====================================================================
# Preflight 2-PC: jalankan "mpiexec -hosts N localhost 1 <slave> 1 ... hostname"
# SEBAGAI CLUSTER_USER (hp) supaya cocok dgn smpd. Kalau dijalankan dari user
# beda (mis. Administrator) sementara smpd = hp -> error 5 (access denied).
# IP slave diambil dari S1_IP / S2_IP (_config.bat). Dipanggil test_2pc.bat.
# =====================================================================
$ErrorActionPreference = 'Stop'
$u = $env:CLUSTER_USER; $p = $env:CLUSTER_PASS
$mpiexec = Join-Path $env:MSMPI_BIN 'mpiexec.exe'
$proj = $env:PROJECT_DIR

$ips = @()
foreach ($v in @($env:S1_IP, $env:S2_IP)) { if ($v) { $ips += $v } }
if ($ips.Count -eq 0) {
    throw "S1_IP/S2_IP kosong di _config.bat. Isi IP slave yang SEKARANG dulu."
}

$n = 1 + $ips.Count
$argList = @('-hosts', "$n", 'localhost', '1')
foreach ($ip in $ips) { $argList += @($ip, '1') }
$argList += 'hostname'
Write-Host ("[test] mpiexec " + ($argList -join ' ') + "    (dijalankan sebagai '$u')")

$me  = ([Security.Principal.WindowsIdentity]::GetCurrent().Name).Split('\')[-1]
$out = Join-Path $env:TEMP 'test2pc_out.txt'
$err = Join-Path $env:TEMP 'test2pc_err.txt'
if ($me -ne $u) {
    if (-not $u -or -not $p) { throw "CLUSTER_USER/CLUSTER_PASS kosong di _config.bat." }
    $cred = New-Object System.Management.Automation.PSCredential($u, (ConvertTo-SecureString $p -AsPlainText -Force))
    Start-Process -FilePath $mpiexec -ArgumentList $argList -Credential $cred -WorkingDirectory $proj -RedirectStandardOutput $out -RedirectStandardError $err -Wait
} else {
    Start-Process -FilePath $mpiexec -ArgumentList $argList -WorkingDirectory $proj -RedirectStandardOutput $out -RedirectStandardError $err -Wait
}
Start-Sleep -Milliseconds 300

$stdout = @(); if (Test-Path $out) { $stdout = @(Get-Content $out) }
$stderr = @(); if (Test-Path $err) { $stderr = @(Get-Content $err) }
Write-Host "----- OUTPUT -----"
$stdout | ForEach-Object { Write-Host "  $_" }
if ($stderr.Count) { Write-Host "----- STDERR -----"; $stderr | ForEach-Object { Write-Host "  $_" } }

$names = @($stdout | Where-Object { $_ -match '\S' })
$bad   = (($stderr -join ' ') -match 'error|Aborting')
Write-Host ""
if ($names.Count -ge $n -and -not $bad) {
    Write-Host ("[test] LULUS: $($names.Count) host menjawab -> " + ($names -join ', '))
} else {
    Write-Host "[test] GAGAL. Petunjuk:"
    Write-Host "  - 'error 5'        = mpiexec & smpd beda user (pastikan smpd master jalan sebagai '$u')."
    Write-Host "  - 'timed out/1726' = slave belum jalankan run_slaveN.bat / IP salah / firewall."
}

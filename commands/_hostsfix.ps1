# =====================================================================
# HOSTS FIX (dipanggil _hostsfix.bat). Tulis pemetaan hostname->IPv4
# LENGKAP cluster (master + semua slave) ke hosts file di SETIAP PC,
# supaya smpd master<->slave bisa resolve nama dua arah (bantu 1726).
# Baca file penuh dulu lalu tulis (hindari "Stream was not readable").
# Idempotent: baris ditandai "# tubes-mpi". deactivate.bat menghapusnya.
# Variabel diambil dari environment (di-set _config.bat): CLUSTER_PREFIX,
# MASTER_HOST/MASTER_IP, S1_HOST/S1_IP, S2_HOST/S2_IP.
# =====================================================================
$ErrorActionPreference = 'SilentlyContinue'
$h = Join-Path $env:WINDIR 'System32\drivers\etc\hosts'

# IP sendiri: dari CLUSTER_PREFIX kalau di-set, kalau tidak dari default-route.
$own = $null
if ($env:CLUSTER_PREFIX) {
    $own = (Get-NetIPAddress -AddressFamily IPv4 |
            Where-Object { $_.IPAddress -like ($env:CLUSTER_PREFIX + '*') } |
            Select-Object -First 1).IPAddress
}
if (-not $own) {
    $own = (Find-NetRoute -RemoteIPAddress '8.8.8.8' |
            Select-Object -ExpandProperty IPAddress |
            Where-Object { $_ -ne '127.0.0.1' -and $_ -notlike '169.254.*' } |
            Select-Object -First 1)
}

# Baca file penuh (read-then-write, jangan stream file yang sama).
$lines = @()
if (Test-Path $h) { $lines = @([System.IO.File]::ReadAllLines($h)) }
$kept = @($lines | Where-Object { $_ -notmatch '# tubes-mpi' })

# Bangun pemetaan cluster lengkap, de-dup.
$new  = New-Object System.Collections.Generic.List[string]
function Add-Map($ip, $name) {
    if ($ip -and $name) {
        $line = "$ip`t$name`t# tubes-mpi"
        if (-not $new.Contains($line)) { $new.Add($line) }
    }
}
Add-Map $own            $env:COMPUTERNAME
Add-Map $env:MASTER_IP  $env:MASTER_HOST
Add-Map $env:S1_IP      $env:S1_HOST
Add-Map $env:S2_IP      $env:S2_HOST

$all = @($kept) + @($new.ToArray())
[System.IO.File]::WriteAllLines($h, [string[]]$all)

Write-Host ("[hosts] own=" + $own)
$new | ForEach-Object { Write-Host ("  " + $_) }

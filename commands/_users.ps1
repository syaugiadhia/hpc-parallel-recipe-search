# =====================================================================
# Auto-buat AKUN CLUSTER (CLUSTER_USER/CLUSTER_PASS dari _config.bat) di
# PC ini. MS-MPI butuh akun yang sama (nama+password) di semua node supaya
# auth dua-arah smpd lolos. Akun dibuat admin + password never expires +
# Description 'tubes-mpi auto' (penanda supaya remove_users.bat aman: hanya
# menghapus akun yang IA buat, tidak menyentuh akun asli yang sudah ada).
# Kalau akun sudah ada -> dibiarkan (tidak diubah passwordnya).
# =====================================================================
$ErrorActionPreference = 'SilentlyContinue'
$u = $env:CLUSTER_USER
$p = $env:CLUSTER_PASS
if (-not $u) { Write-Host '[users] CLUSTER_USER kosong di _config.bat, dilewati.'; return }

$existing = Get-LocalUser -Name $u -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "[users] akun '$u' sudah ada (dibiarkan, password tidak diubah)."
    # Pastikan anggota Administrators (idempotent, aman).
    Add-LocalGroupMember -Group 'Administrators' -Member $u -ErrorAction SilentlyContinue
    return
}

try {
    $sec = ConvertTo-SecureString $p -AsPlainText -Force
    New-LocalUser -Name $u -Password $sec -PasswordNeverExpires -AccountNeverExpires `
        -Description 'tubes-mpi auto' -ErrorAction Stop | Out-Null
    Add-LocalGroupMember -Group 'Administrators' -Member $u -ErrorAction SilentlyContinue
    Write-Host "[users] akun '$u' DIBUAT (admin, tubes-mpi auto)."
} catch {
    # Fallback ke net user kalau policy password menolak New-LocalUser.
    Write-Host ("[users] New-LocalUser gagal: " + $_.Exception.Message + " -> coba net user")
    cmd /c "net user `"$u`" `"$p`" /add /expires:never" | Out-Null
    cmd /c "net localgroup Administrators `"$u`" /add" | Out-Null
    cmd /c "wmic useraccount where `"name='$u'`" set PasswordExpires=false" | Out-Null
    $chk = Get-LocalUser -Name $u -ErrorAction SilentlyContinue
    if ($chk) {
        $chk | Set-LocalUser -Description 'tubes-mpi auto' -ErrorAction SilentlyContinue
        Write-Host "[users] akun '$u' dibuat via net user."
    } else {
        Write-Host "[users] GAGAL membuat akun '$u'. Cek password policy / buat manual."
    }
}

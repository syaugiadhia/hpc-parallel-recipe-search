# =====================================================================
# Hapus HANYA akun cluster yang dibuat otomatis oleh _users.ps1 (ditandai
# Description 'tubes-mpi auto'). Akun ASLI yang sudah ada sebelumnya TIDAK
# disentuh (tidak bertanda). Sekaligus tutup proses akun itu (smpd/GUI) dan
# hapus folder profilnya bila sempat terbuat. Dipanggil remove_users.bat /
# reset_all.bat.
# =====================================================================
$ErrorActionPreference = 'SilentlyContinue'
$made = Get-LocalUser | Where-Object { $_.Description -eq 'tubes-mpi auto' }
if (-not $made) { Write-Host '[users] tidak ada akun bertanda tubes-mpi auto. Tidak ada yang dihapus.'; return }

# Tutup dulu proses cluster (smpd + GUI) supaya akun/profil bisa dihapus.
Get-CimInstance Win32_Process -Filter "Name='smpd.exe' OR Name='python.exe' OR Name='pythonw.exe'" |
    Where-Object { $_.Name -eq 'smpd.exe' -or $_.CommandLine -match 'alchemy_gui' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 500

foreach ($acct in $made) {
    $name = $acct.Name
    $sid  = $acct.SID.Value
    Remove-LocalGroupMember -Group 'Administrators' -Member $name -ErrorAction SilentlyContinue
    Remove-LocalUser -Name $name -ErrorAction SilentlyContinue
    # hapus profil (folder C:\Users\<name>) lewat Win32_UserProfile (by SID)
    Get-CimInstance Win32_UserProfile -ErrorAction SilentlyContinue |
        Where-Object { $_.SID -eq $sid } |
        ForEach-Object {
            $lp = $_.LocalPath
            Remove-CimInstance $_ -ErrorAction SilentlyContinue
            Write-Host ("[users] profil dihapus: " + $lp)
        }
    Write-Host ("[users] akun dihapus: " + $name)
}

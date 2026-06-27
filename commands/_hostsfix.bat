@echo off
:: =====================================================================
:: HOSTS FIX: petakan hostname master + slave ke IPv4 langsung.
:: Menghilangkan timeout name-resolution (penyebab run lambat / hang
:: setelah IPv6 & adapter di-disable) dan memastikan hostname lawan
:: resolve ke IPv4 yang benar (bantu masalah 1726 multi-PC).
:: IP sendiri di-AUTO-DETECT dari adapter default-route (atau dari
:: CLUSTER_PREFIX bila di-set di _config.bat). IP slave hanya ditulis
:: kalau S1_IP/S2_IP diisi (opsional).
:: Idempotent: baris ditandai "# tubes-mpi" & ditulis ulang tiap run.
:: deactivate.bat menghapus baris bertanda itu.
:: Dipanggil "call" oleh run_*.bat sesudah _netfix.bat.
:: =====================================================================

echo [hosts] Menulis pemetaan hostname -^> IPv4 ke hosts file...

powershell -NoProfile -Command "$h=\"$env:WINDIR\System32\drivers\etc\hosts\"; $prefix=$env:CLUSTER_PREFIX; $own=$null; if($prefix){ $own=(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like \"$prefix*\" } | Select-Object -First 1).IPAddress }; if(-not $own){ $own=(Find-NetRoute -RemoteIPAddress '8.8.8.8' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty IPAddress | Where-Object { $_ -ne '127.0.0.1' -and $_ -notlike '169.254.*' } | Select-Object -First 1) }; $keep=@(Get-Content $h -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch '# tubes-mpi' }); $new=@(); if($own){ $new+=\"$own`t$env:COMPUTERNAME`t# tubes-mpi\" }; if($env:S1_IP){ $new+=\"$env:S1_IP`t$env:S1_HOST`t# tubes-mpi\" }; if($env:S2_IP){ $new+=\"$env:S2_IP`t$env:S2_HOST`t# tubes-mpi\" }; Set-Content -Path $h ($keep + $new) -Encoding ASCII; Write-Host ('[hosts] own=' + $own); $new | ForEach-Object { Write-Host ('  ' + $_) }"

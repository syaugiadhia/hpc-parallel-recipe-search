@echo off
title SMPD
echo Setting Kredensial

=:: hostname laptop teman (Child/Master)
set host_teman=desktop-s3pfjin
:: Isi dengan username hasil 'whoami' (yang setelah tanda \)
set user_teman=desktop-s3pfjin\hp
:: Isi dengan password Windows laptop sebelah
set pass_teman=mrdx

echo [1/2] Mendaftarkan kredensial untuk %host_teman%
cmdkey /add:"%host_teman%" /user:"%user_teman%" /pass:"%pass_teman%"

echo Memulai SMPD Daemon
echo Window ini akan log semua aktivitas jaringan.
cd /d C:\HPC
smpd -d 3
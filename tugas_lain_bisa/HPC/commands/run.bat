mpiexec -hosts 2 localhost 1 desktop-s3pfjin 1 hostname

mpiexec -hosts 2 localhost 1 desktop-s3pfjin 1 -wdir C:\HPC\ mergesort_mpi.exe

mpiexec -hosts 2 localhost 2 desktop-s3pfjin 2 -wdir C:\HPC\ mergesort_mpi.exe

mpiexec -hosts 2 localhost 4 desktop-s3pfjin 4 -wdir C:\HPC\ mergesort_mpi.exe


pause
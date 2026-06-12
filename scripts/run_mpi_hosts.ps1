param(
    [string]$Hostfile = "hosts.txt",
    [int]$Np = 0,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AlchemyArgs
)

$ErrorActionPreference = "Stop"
$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
$Bin = Join-Path $RootDir "build\alchemy_mpi.exe"

if (-not (Test-Path $Bin)) {
    throw "Missing executable: $Bin. Build first with: cmake -S . -B build && cmake --build build"
}
if (-not (Get-Command mpiexec -ErrorAction SilentlyContinue)) {
    throw "mpiexec was not found in PATH. Install MS-MPI runtime/SDK and restart the shell."
}
if (-not (Test-Path $Hostfile)) {
    throw "Hostfile not found: $Hostfile"
}

$hostArgs = @()
$totalSlots = 0
Get-Content $Hostfile | ForEach-Object {
    $line = $_.Trim()
    if ($line.Length -eq 0 -or $line.StartsWith("#")) {
        return
    }
    $parts = $line -split "\s+"
    $hostName = $parts[0]
    $slots = 1
    for ($i = 1; $i -lt $parts.Count; $i++) {
        $part = $parts[$i]
        if ($part -match "^slots=(\d+)$") {
            $slots = [int]$Matches[1]
        }
    }
    if ($slots -lt 1) {
        throw "Invalid slot count for host '$hostName': $slots"
    }
    $hostArgs += $hostName
    $hostArgs += [string]$slots
    $totalSlots += $slots
}

if ($hostArgs.Count -eq 0) {
    throw "Hostfile has no usable hosts: $Hostfile"
}

$hostCount = [int]($hostArgs.Count / 2)
if ($Np -ne 0 -and $Np -ne $totalSlots) {
    Write-Warning "MS-MPI -hosts uses total hostfile slots ($totalSlots), ignoring -Np $Np."
}

$defaultArgs = @(
    "--data", (Join-Path $RootDir "data\recipes.json"),
    "--target", "Brick",
    "--algorithm", "bfs",
    "--mode", "multiple",
    "--limit", "10",
    "--trace-mode", "memo",
    "--visual-mode", "shared",
    "--split-depth", "2",
    "--output", (Join-Path $RootDir "results\brick_mpi_hosts_np$totalSlots")
)

New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "results") | Out-Null

& mpiexec -hosts $hostCount @hostArgs $Bin @defaultArgs @AlchemyArgs
exit $LASTEXITCODE

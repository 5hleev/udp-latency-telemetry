# tools/run_experiments.ps1 - воспроизводимый прогон всех серий эксперимента.
#
#   powershell -ExecutionPolicy Bypass -File tools\run_experiments.ps1 [-Count 100] [-IntervalMs 500]
#
# Схема: client -> netem_proxy (:27016) -> server (:27015). Для каждой серии
# netem_proxy запускается заново со своим профилем и seed. Результаты:
#   docs/latency_samples.csv          - журнал измерений (перезаписывается)
#   docs/logs/server.log              - журнал сервера за весь прогон
#   docs/logs/client_<серия>.log      - сводка клиента
#   docs/logs/netem_<серия>.log       - параметры эмулятора и фактические потери
#   docs/logs/environment.txt         - ОС, компилятор, коммит, дата

param(
    [int]$Count = 100,
    [int]$IntervalMs = 500,
    [int]$TimeoutMs = 1000,
    [int]$ServerPort = 27015,
    [int]$ProxyPort = 27016,
    [string]$Only = ""        # запустить одну серию, например -Only jitter
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$bin = Join-Path $root "build"
foreach ($exe in "server.exe", "client.exe", "netem_proxy.exe") {
    if (-not (Test-Path (Join-Path $bin $exe))) { throw "Нет $exe - сначала выполните build.bat" }
}

# Серии: up - искажения направления клиент->сервер, down - сервер->клиент.
# Задержка и потери вносятся в одну сторону, чтобы прибавка к RTT и доля потерь
# совпадали с параметрами серии. Seed фиксирован для каждой серии.
$series = @(
    @{ id = "baseline";  up = "";                            down = "";                                   seed = 1001; invalid = $true }
    @{ id = "delay_50";  up = "delay=50";                    down = "";                                   seed = 1002 }
    @{ id = "delay_100"; up = "delay=100";                   down = "";                                   seed = 1003 }
    @{ id = "jitter";    up = "delay=100,jitter=50";         down = "";                                   seed = 1004 }
    @{ id = "loss_5";    up = "loss=5";                      down = "";                                   seed = 1005 }
    @{ id = "combined";  up = "delay=100,jitter=50,loss=5";  down = "";                                   seed = 1006 }
    @{ id = "faults";    up = "";                            down = "dup=10,spike=8,spikems=1500,reseq=5,corrupt=5"; seed = 1007 }
)
if ($Only) { $series = $series | Where-Object { $_.id -eq $Only } }

$docs = Join-Path $root "docs"
$logs = Join-Path $docs "logs"
New-Item -ItemType Directory -Force $logs | Out-Null
$csv = Join-Path $docs "latency_samples.csv"
if (-not $Only -and (Test-Path $csv)) { Remove-Item $csv }

# Окружение - для docs/Experiment_Config.md
$os = Get-CimInstance Win32_OperatingSystem
$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
$commit = (git rev-parse --short HEAD 2>$null)
@(
    "date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
    "os: $($os.Caption) $($os.Version) build $($os.BuildNumber)"
    "cpu: $cpu"
    "commit: $commit"
    "count: $Count, interval_ms: $IntervalMs, timeout_ms: $TimeoutMs"
    "server: 127.0.0.1:$ServerPort, proxy: 127.0.0.1:$ProxyPort"
) | Set-Content -Encoding utf8 (Join-Path $logs "environment.txt")

$serverLog = Join-Path $logs "server.log"
if (-not $Only -and (Test-Path $serverLog)) { Remove-Item $serverLog }
$server = Start-Process -FilePath (Join-Path $bin "server.exe") -PassThru -WindowStyle Hidden `
    -ArgumentList "--port $ServerPort --log `"$serverLog`" --idle-exit 15"
Start-Sleep -Milliseconds 500

try {
    foreach ($s in $series) {
        $id = $s.id
        Write-Host "=== $id  (C->S: '$($s.up)'  S->C: '$($s.down)'  seed=$($s.seed))"
        $netemLog = Join-Path $logs "netem_$id.log"
        $clientLog = Join-Path $logs "client_$id.log"
        foreach ($f in $netemLog, $clientLog) { if (Test-Path $f) { Remove-Item $f } }

        $proxyArgs = "--listen $ProxyPort --server 127.0.0.1:$ServerPort --seed $($s.seed) --idle-exit 3 --log `"$netemLog`""
        if ($s.up)   { $proxyArgs += " --up `"$($s.up)`"" }
        if ($s.down) { $proxyArgs += " --down `"$($s.down)`"" }
        $proxy = Start-Process -FilePath (Join-Path $bin "netem_proxy.exe") -PassThru -WindowStyle Hidden -ArgumentList $proxyArgs
        Start-Sleep -Milliseconds 300

        $clientArgs = @("--experiment", $id, "--host", "127.0.0.1", "--port", $ProxyPort, "--count", $Count,
                        "--interval", $IntervalMs, "--timeout", $TimeoutMs, "--csv", $csv, "--log", $clientLog)
        if ($s.invalid) { $clientArgs += "--send-invalid" }
        & (Join-Path $bin "client.exe") @clientArgs
        if ($LASTEXITCODE -ne 0) { throw "client завершился с кодом $LASTEXITCODE" }

        $proxy.WaitForExit(10000) | Out-Null
        if (-not $proxy.HasExited) { Stop-Process -Id $proxy.Id -Force }
        Get-Content -Encoding utf8 $netemLog | Select-String "Итого" | ForEach-Object { Write-Host $_.Line }
    }
}
finally {
    $server.WaitForExit(30000) | Out-Null
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
}

& (Join-Path $bin "latency_report.exe") --csv $csv --summary (Join-Path $docs "latency_summary.csv")

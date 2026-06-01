$iris_cmake = Resolve-Path ".\cmake-build-release-manual\IRIS.exe"
$iris_zig = Resolve-Path ".\zig-out\bin\iris.exe"
$python = "python"
$luajit = "C:\lua\luajit.exe"

$benchmarks = @("01_loop_math", "02_fibonacci", "03_array_ops", "04_prime_sieve", "06_bubble_sort", "07_string_bench", "09_float_math", "10_nested_loops")

$report = "| Benchmark | Iris (CMake) | Iris (Zig) | Python | LuaJIT |`n"
$report += "|-----------|--------------|------------|--------|--------|`n"

foreach ($b in $benchmarks) {
    $row = "| $b | "
    
    # Iris CMake
    $out_cmake = & $iris_cmake "benchmarks\$b.iris" 2>&1 | Out-String
    if ($out_cmake -match "VM trval: ([\d.]+) ms") { $row += "$($Matches[1]) ms | " } else { $row += "ERR | " }

    # Iris Zig
    $out_zig = & $iris_zig "benchmarks\$b.iris" 2>&1 | Out-String
    if ($out_zig -match ": ([\d.]+) ms") { $row += "$($Matches[1]) ms | " } else { $row += "ERR | " }

    # Python
    $out_py = & $python "benchmarks\$b.py" 2>&1 | Out-String
    if ($out_py -match ": ([\d.]+) ms") { $row += "$($Matches[1]) ms | " } else { $row += "ERR | " }

    # LuaJIT
    $out_lua = & $luajit "benchmarks\$b.lua" 2>&1 | Out-String
    if ($out_lua -match ": ([\d.]+) ms") { $row += "$($Matches[1]) ms |" } else { $row += "ERR |" }

    $report += $row + "`n"
}
$report | Out-File BENCHMARKS.md -Encoding utf8
Write-Host $report

param(
    [string]$EzQuakeSource = "C:\Users\Negociador\Documents\ezquake-source",
    [switch]$Csv
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $EzQuakeSource "src\hud.h"))) {
    throw "ezQuake source not found at $EzQuakeSource"
}

$results = New-Object System.Collections.Generic.List[object]

$files = Get-ChildItem -Path (Join-Path $EzQuakeSource "src") -Filter "*.c" -File |
    Where-Object { $_.Name -like "hud*.c" -or $_.Name -in @("stats_grid.c", "r_rmain.c") }

foreach ($file in $files) {
    $lines = Get-Content -LiteralPath $file.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "HUD_Register\s*\(") {
            $block = New-Object System.Collections.Generic.List[string]
            $depth = 0
            $started = $false

            for ($j = $i; $j -lt $lines.Count; $j++) {
                $line = $lines[$j]
                $block.Add($line.Trim())

                foreach ($ch in $line.ToCharArray()) {
                    if ($ch -eq '(') {
                        $depth++
                        $started = $true
                    }
                    elseif ($ch -eq ')') {
                        $depth--
                    }
                }

                if ($started -and $depth -le 0) {
                    break
                }
            }

            $text = ($block -join " ")
            $name = ""
            if ($text -match 'HUD_Register\s*\(\s*"([^"]+)"') {
                $name = $Matches[1]
            }
            else {
                $name = "<dynamic-or-multiline>"
            }

            $results.Add([PSCustomObject]@{
                SourceFile = $file.Name
                SourceLine = $i + 1
                ElementName = $name
                RegisterCall = $text
            })
        }
    }
}

if ($Csv) {
    $results | ConvertTo-Csv -NoTypeInformation
}
else {
    $results
}

# Entry point central da Central de Testes AMD BC-250 (BC250-V3)
# Se invocado sem argumentos, inicia a GUI (para o usuário).
# Se invocado com argumentos (-All, -Test, -Json, etc.), executa a CLI.

param (
    [switch]$Gui,
    [switch]$All,
    [string]$Test = "",
    [int]$Phase = -1,
    [switch]$Json,
    [switch]$Summary,
    [switch]$Report,
    [switch]$Help
)

$Root = $PSScriptRoot

if ($Gui -or (-not $All -and $Test -eq "" -and $Phase -eq -1 -and -not $Json -and -not $Summary -and -not $Report -and -not $Help)) {
    & (Join-Path $Root "gui\CentralDeTestes_WPF.ps1")
} else {
    $cliArgs = @()
    if ($All) { $cliArgs += "-All" }
    if ($Test -ne "") { $cliArgs += @("-Test", $Test) }
    if ($Phase -ge 0) { $cliArgs += @("-Phase", "$Phase") }
    if ($Json) { $cliArgs += "-Json" }
    if ($Summary) { $cliArgs += "-Summary" }
    if ($Report) { $cliArgs += "-Report" }
    if ($Help) { $cliArgs += "-Help" }

    & (Join-Path $Root "cli\Runner_CLI.ps1") @cliArgs
}

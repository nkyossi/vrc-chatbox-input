# Runs the end-to-end test (tests/e2e_test.cpp). It types with SendInput; do
# not touch the keyboard or the mouse while it runs.
#
#   powershell -ExecutionPolicy Bypass -File tools\run-e2e.ps1 [-Ime]
#
# -Ime checks the window with Hiragana IME: its settings are saved, the romaji
# layout is used for the test, and the settings are restored afterwards.

param([switch] $Ime)

$ErrorActionPreference = 'Stop'
$build = Join-Path $env:LOCALAPPDATA 'vrc-chatbox-input-build'
$test = Join-Path $build 'e2e_test.exe'
$app = Join-Path $build 'vrc-chatbox-input.exe'

# The test starts the program itself; only one may run.
Get-Process -Name 'vrc-chatbox-input' -ErrorAction SilentlyContinue | Stop-Process -Force

if (-not $Ime) {
    & $test $app
    exit $LASTEXITCODE
}

$settings = Join-Path $env:APPDATA 'HiraganaIME\settings.json'
$saved = "$settings.e2e-backup"
Copy-Item $settings $saved -Force
try {
    $json = Get-Content $settings -Raw | ConvertFrom-Json
    $json.layout = 'roomazi'
    $json.keyboard = 'auto'
    if ($json.PSObject.Properties['key-swaps']) { $json.'key-swaps' = @() }
    ($json | ConvertTo-Json) | Set-Content -Encoding ascii $settings
    & $test $app --ime
    $code = $LASTEXITCODE
} finally {
    Start-Sleep -Milliseconds 500
    Copy-Item $saved $settings -Force
    Remove-Item $saved
}
exit $code

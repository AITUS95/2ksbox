# The everyday Windows scripts from PowerShell, with no MSYS2 shell to
# open first (docs/build-windows.md, "Building on Windows"). Each verb
# starts MSYS2's MINGW64 login shell in the checkout, with PowerShell's
# full environment (an MSYS2 bash started from Git Bash gets a stripped
# one, and rustc then finds no Visual Studio), and runs the bash script
# that does the work through scripts/win-ps1.sh (cargo on PATH),
# arguments passed through.
#
#   scripts\win.ps1 build [stages...]     scripts/build-windows.sh
#   scripts\win.ps1 run [launcher|player|qemu] [args...]
#                                         scripts/win-run.sh (the launcher by default)
#   scripts\win.ps1 test [checks...]      scripts/test.sh (all by default)
#   scripts\win.ps1 package [args...]     scripts/package-windows.sh
#   scripts\win.ps1 drivers [publish|fetch|key] [driver...]
#                                         scripts/windows-drivers.sh (publish by default)
#   scripts\win.ps1 sideload [args...]    scripts/win-sideload.ps1 (no MSYS2)
#   scripts\win.ps1 sh <script> [args...] any other script, in MINGW64
#   scripts\win.ps1 shell                 an interactive MINGW64 shell in the checkout
#
# MSYS2 is found at $env:MSYS2_ROOT, then Scoop's (apps\msys2\current),
# then C:\msys64. Arguments reach bash as words: PowerShell 5.1 does not
# quote embedded quotes for a native program, so keep them simple.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Usage {
  # the header's verb list: from the first verb to the next empty comment line
  $lines = Get-Content $PSCommandPath
  $first = [array]::FindIndex([string[]]$lines, [Predicate[string]]{ param($l) $l -like '#   scripts\win.ps1 *' })
  foreach ($l in ($lines | Select-Object -Skip $first)) {
    if ($l -eq '#') { break }
    $l -replace '^# ?', ''
  }
  exit 2
}

function Find-Msys2 {
  $scoop = if ($env:SCOOP) { $env:SCOOP } else { Join-Path $env:USERPROFILE 'scoop' }
  foreach ($dir in @($env:MSYS2_ROOT, (Join-Path $scoop 'apps\msys2\current'), 'C:\msys64')) {
    if ($dir -and (Test-Path (Join-Path $dir 'usr\bin\bash.exe'))) { return $dir }
  }
  throw 'win.ps1: no MSYS2 (set MSYS2_ROOT to its folder, the one with usr\bin\bash.exe)'
}

# bash -l in MINGW64, started in the checkout (CHERE_INVOKING keeps the
# login profile from cd'ing home); the caller's environment is restored
function Invoke-Mingw([string[]]$bashArgs) {
  $bash = Join-Path (Find-Msys2) 'usr\bin\bash.exe'
  $saved = @{ MSYSTEM = $env:MSYSTEM; CHERE_INVOKING = $env:CHERE_INVOKING }
  $env:MSYSTEM = 'MINGW64'
  $env:CHERE_INVOKING = '1'
  Push-Location $root
  try {
    & $bash -l scripts/win-ps1.sh @bashArgs
    $code = $LASTEXITCODE
  } finally {
    Pop-Location
    $env:MSYSTEM = $saved.MSYSTEM
    $env:CHERE_INVOKING = $saved.CHERE_INVOKING
  }
  exit $code
}

if ($args.Count -eq 0) { Usage }
$verb = $args[0]
$rest = @($args | Select-Object -Skip 1)

switch ($verb) {
  'build'   { Invoke-Mingw (@('scripts/build-windows.sh') + $rest) }
  'run'     {
    if ($rest.Count -eq 0) { $rest = @('launcher') }
    Invoke-Mingw (@('scripts/win-run.sh') + $rest)
  }
  'test'    {
    if ($rest.Count -eq 0) { $rest = @('all') }
    Invoke-Mingw (@('scripts/test.sh') + $rest)
  }
  'package' { Invoke-Mingw (@('scripts/package-windows.sh') + $rest) }
  'drivers' {
    if ($rest.Count -eq 0) { $rest = @('publish') }
    Invoke-Mingw (@('scripts/windows-drivers.sh') + $rest)
  }
  'sideload' {
    & (Join-Path $PSScriptRoot 'win-sideload.ps1') @rest
    exit $LASTEXITCODE
  }
  'sh'      {
    if ($rest.Count -eq 0) { Usage }
    Invoke-Mingw $rest
  }
  'shell'   { Invoke-Mingw @('-i') }
  default   { Usage }
}

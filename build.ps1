$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dist = Join-Path $root 'dist'
$assets = Join-Path $root 'assets'
$build = Join-Path $root 'build'

Remove-Item $dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $dist 'assets') | Out-Null
Copy-Item (Join-Path $assets '*.png') (Join-Path $dist 'assets')

$windres = (Get-Command windres.exe -ErrorAction Stop).Source
$gpp = (Get-Command g++.exe -ErrorAction Stop).Source
$resource = Join-Path $build 'app.res'

& $windres -I $build -i (Join-Path $build 'app.rc') -o $resource -O coff
if ($LASTEXITCODE -ne 0) {
    throw 'Resource compilation failed.'
}

$sources = @(Get-ChildItem (Join-Path $root 'src\*.cpp') | ForEach-Object { $_.FullName })
$compileArguments = @(
    '-std=c++17',
    '-O2',
    '-DUNICODE',
    '-D_UNICODE',
    '-municode',
    '-mwindows',
    '-static',
    '-static-libgcc',
    '-static-libstdc++',
    '-s'
) + $sources + @(
    $resource,
    '-o', (Join-Path $dist 'BoykisserAvatar.exe'),
    '-lgdiplus',
    '-lgdi32',
    '-luser32',
    '-lshell32',
    '-lwinmm',
    '-lole32',
    '-luuid'
)

& $gpp @compileArguments
if ($LASTEXITCODE -ne 0) {
    throw 'Build failed.'
}

Write-Host "Built $dist\BoykisserAvatar.exe"

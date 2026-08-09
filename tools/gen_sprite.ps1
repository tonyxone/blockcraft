# Converts hand-drawn item art into compiled-in source.
#
#   art\<name>.png  ->  src\sprites_generated.cpp
#
# Draw at 16x16 (TILE_PX) in Aseprite or anything else that exports PNG, save
# into art\, run this, rebuild. Transparent pixels stay transparent, so the
# inventory slot shows through around the item.
#
# Usage:  powershell -File tools\gen_sprite.ps1
#
# Everything ends up in the exe, so the game still needs no asset files at
# runtime — see src\sprites_generated.h.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Storage resolution of an atlas tile (ATLAS_TILE_PX in src\textures.h).
# Art may be drawn at this size or at the 16x16 block grid; 16x16 is doubled
# up losslessly, since every pixel becomes an exact 2x2 square.
$TILE = 32
$COARSE = 16
$root = Split-Path -Parent $PSScriptRoot
$artDir = Join-Path $root 'art'
$outFile = Join-Path $root 'src\sprites_generated.cpp'

if (-not (Test-Path $artDir)) {
    New-Item -ItemType Directory -Path $artDir | Out-Null
    Write-Output "Created $artDir - put your 16x16 PNGs there and run this again."
}

# Reads one PNG into straight-alpha RGBA bytes, row 0 = TOP of the image.
function Read-SpritePixels {
    param([string]$Path)
    $bmp = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $name = [System.IO.Path]::GetFileName($Path)
        if ($bmp.Width -ne $bmp.Height) {
            throw "$name is $($bmp.Width)x$($bmp.Height); it must be square"
        }
        if ($bmp.Width -ne $TILE -and $bmp.Width -ne $COARSE) {
            throw "$name is $($bmp.Width)x$($bmp.Width); it must be ${TILE}x${TILE} or ${COARSE}x${COARSE}"
        }
        # 16x16 art is nearest-neighbour doubled, which is exact: no blurring
        # and no detail invented.
        $step = $TILE / $bmp.Width
        $bytes = New-Object 'System.Byte[]' ($TILE * $TILE * 4)
        for ($y = 0; $y -lt $TILE; $y++) {
            for ($x = 0; $x -lt $TILE; $x++) {
                $c = $bmp.GetPixel([int][Math]::Floor($x / $step), [int][Math]::Floor($y / $step))
                $i = (($y * $TILE) + $x) * 4
                $bytes[$i]     = $c.R
                $bytes[$i + 1] = $c.G
                $bytes[$i + 2] = $c.B
                $bytes[$i + 3] = $c.A
            }
        }
        # comma keeps PowerShell from unrolling the array into the pipeline
        return ,$bytes
    } finally {
        $bmp.Dispose()
    }
}

# C identifiers can't hold the punctuation a filename can.
function ConvertTo-Identifier {
    param([string]$Name)
    return ($Name -replace '[^A-Za-z0-9_]', '_')
}

$pngs = @(Get-ChildItem -Path $artDir -Filter '*.png' -ErrorAction SilentlyContinue | Sort-Object Name)

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// GENERATED FILE - overwritten by tools\gen_sprite.ps1. Do not hand-edit.')
[void]$sb.AppendLine('//')
[void]$sb.AppendLine('// Source images: art\*.png, converted at build-authoring time so the game')
[void]$sb.AppendLine('// ships as a single exe with no asset files to load at runtime.')
[void]$sb.AppendLine('#include "sprites_generated.h"')
[void]$sb.AppendLine('#include <cstring>')
[void]$sb.AppendLine('')

foreach ($png in $pngs) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($png.Name)
    $ident = ConvertTo-Identifier $name
    $bytes = Read-SpritePixels -Path $png.FullName
    [void]$sb.AppendLine("// from art\$($png.Name)")
    [void]$sb.AppendLine("static const unsigned char SPRITE_$ident[] = {")
    # one image row per source line, so a diff shows which rows changed
    for ($y = 0; $y -lt $TILE; $y++) {
        $parts = New-Object System.Collections.Generic.List[string]
        for ($x = 0; $x -lt $TILE; $x++) {
            $i = (($y * $TILE) + $x) * 4
            [void]$parts.Add(('{0},{1},{2},{3}' -f $bytes[$i], $bytes[$i+1], $bytes[$i+2], $bytes[$i+3]))
        }
        # ', ' not ' ' — the separator between RGBA groups has to be a comma,
        # or the emitted array is not valid C++ at all
        [void]$sb.AppendLine('  ' + ($parts -join ', ') + ',')
    }
    [void]$sb.AppendLine('};')
    [void]$sb.AppendLine('')
}

[void]$sb.AppendLine('const GeneratedSprite GENERATED_SPRITES[] = {')
if ($pngs.Count -eq 0) {
    [void]$sb.AppendLine('  { nullptr, nullptr }, // placeholder: C++ has no zero-length arrays')
} else {
    foreach ($png in $pngs) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($png.Name)
        $ident = ConvertTo-Identifier $name
        [void]$sb.AppendLine("  { `"$name`", SPRITE_$ident },")
    }
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine("const int GENERATED_SPRITE_COUNT = $($pngs.Count);")
[void]$sb.AppendLine('')
[void]$sb.AppendLine('const GeneratedSprite* generatedSpriteNamed(const char* name) {')
[void]$sb.AppendLine('  if (!name) return nullptr;')
[void]$sb.AppendLine('  for (int i = 0; i < GENERATED_SPRITE_COUNT; i++) {')
[void]$sb.AppendLine('    if (GENERATED_SPRITES[i].name && std::strcmp(GENERATED_SPRITES[i].name, name) == 0) {')
[void]$sb.AppendLine('      return &GENERATED_SPRITES[i];')
[void]$sb.AppendLine('    }')
[void]$sb.AppendLine('  }')
[void]$sb.AppendLine('  return nullptr;')
[void]$sb.AppendLine('}')

Set-Content -Path $outFile -Value $sb.ToString() -Encoding utf8

if ($pngs.Count -eq 0) {
    Write-Output "No PNGs in $artDir - wrote the empty default; every tile keeps its procedural art."
} else {
    Write-Output "Converted $($pngs.Count) sprite(s) into src\sprites_generated.cpp:"
    foreach ($png in $pngs) { Write-Output "  $($png.Name)" }
    Write-Output "Rebuild to see them. Names must match spriteNameForTile() in src\textures.cpp."
}

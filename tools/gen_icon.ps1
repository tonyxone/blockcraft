# Generates icon\blockcraft.ico: a Minecraft-logo-style icon drawn from
# scratch — pixel grass/dirt block background (game palette) with chunky
# BLOCKCRAFT lettering. Run from anywhere; writes next to this script's
# parent folder. Also writes a preview PNG for inspection.
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$iconDir = Join-Path $root 'icon'
New-Item -ItemType Directory -Force $iconDir | Out-Null

$size = 256
$cell = 16
$bmp = New-Object System.Drawing.Bitmap($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$rand = New-Object System.Random(1337)

$grassCols = @('#5fa832', '#4c8f27', '#3f7a1f', '#7cc44a', '#6db13d', '#57a02c')
$dirtCols = @('#7a5230', '#8a5a34', '#5c3c20', '#93673f', '#6b4526', '#a0724a', '#7f5733')
$stoneCols = @('#8a8a8e', '#6a6a6e')

# ragged grass layer (2-4 cells deep per column) over dirt with rare stones
for ($col = 0; $col -lt 16; $col++) {
  $depth = 2 + $rand.Next(3)
  for ($row = 0; $row -lt 16; $row++) {
    if ($row -lt $depth) {
      $c = $grassCols[$rand.Next($grassCols.Count)]
    } elseif ($rand.NextDouble() -lt 0.06) {
      $c = $stoneCols[$rand.Next($stoneCols.Count)]
    } else {
      $c = $dirtCols[$rand.Next($dirtCols.Count)]
    }
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($c))
    $g.FillRectangle($brush, $col * $cell, $row * $cell, $cell, $cell)
    $brush.Dispose()
  }
}

# --- BLOCKCRAFT lettering: drawn small and aliased, then upscaled 2x with
# nearest-neighbor so the letters go blocky like the background ---
$tw = 126
$th = 36
$tb = New-Object System.Drawing.Bitmap($tw, $th)
$tg = [System.Drawing.Graphics]::FromImage($tb)
$tg.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixel

$text = 'BLOCKCRAFT'
$font = $null
foreach ($fs in 24..12) {
  $f = New-Object System.Drawing.Font('Impact', $fs, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
  $m = $tg.MeasureString($text, $f)
  if ($m.Width -le ($tw - 8)) { $font = $f; break }
  $f.Dispose()
}

$m = $tg.MeasureString($text, $font)
$tx = [Math]::Floor(($tw - $m.Width) / 2)
$ty = [Math]::Floor(($th - $m.Height) / 2)

$black = [System.Drawing.Brushes]::Black
$gray = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#C9C7C7'))
$darkgray = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#8E8C8C'))

# extrusion (drop toward bottom-right), then outline, then the stone-gray face
$tg.DrawString($text, $font, $black, $tx + 2, $ty + 2)
$tg.DrawString($text, $font, $darkgray, $tx + 1, $ty + 1)
foreach ($o in @(@(-1, 0), @(1, 0), @(0, -1), @(0, 1))) {
  $tg.DrawString($text, $font, $black, $tx + $o[0], $ty + $o[1])
}
$tg.DrawString($text, $font, $gray, $tx, $ty)
$gray.Dispose(); $darkgray.Dispose()

$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$bandW = [int]($tw * 2)
$bandH = [int]($th * 2)
$band = New-Object System.Drawing.Rectangle(2, 92, $bandW, $bandH)
$g.DrawImage($tb, $band)

$g.Dispose(); $tg.Dispose(); $tb.Dispose()

# preview for humans
$bmp.Save((Join-Path $iconDir 'blockcraft_preview.png'), [System.Drawing.Imaging.ImageFormat]::Png)

# 256px PNG entry (with lettering)
$ms = New-Object System.IO.MemoryStream
$bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
$png = $ms.ToArray()
$ms.Dispose(); $bmp.Dispose()

# Small sizes get a text-free grass-block face (lettering is unreadable
# below ~64px; the real game icon does the same).
function New-BlockFace([int]$px, [int]$grid) {
  $b = New-Object System.Drawing.Bitmap($px, $px)
  $bg = [System.Drawing.Graphics]::FromImage($b)
  $cellpx = [int]($px / $grid)
  $r = New-Object System.Random(1337)
  for ($col = 0; $col -lt $grid; $col++) {
    $depth = [Math]::Max(1, [int]($grid * 0.15)) + $r.Next(2)
    for ($row = 0; $row -lt $grid; $row++) {
      if ($row -lt $depth) {
        $c = $grassCols[$r.Next($grassCols.Count)]
      } elseif ($r.NextDouble() -lt 0.06) {
        $c = $stoneCols[$r.Next($stoneCols.Count)]
      } else {
        $c = $dirtCols[$r.Next($dirtCols.Count)]
      }
      $br = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml($c))
      $bg.FillRectangle($br, $col * $cellpx, $row * $cellpx, $cellpx, $cellpx)
      $br.Dispose()
    }
  }
  $bg.Dispose()
  return $b
}

# Classic BMP-in-ICO payload: BITMAPINFOHEADER (doubled height) + bottom-up
# BGRA pixels + an all-opaque 1bpp AND mask.
function Get-IcoBmpBytes($b) {
  $n = $b.Width
  $maskRow = [int]([Math]::Ceiling($n / 32.0) * 4)
  $xorSize = $n * $n * 4
  $andSize = $maskRow * $n
  $out = New-Object System.IO.MemoryStream
  $w = New-Object System.IO.BinaryWriter($out)
  $w.Write([uint32]40); $w.Write([int]$n); $w.Write([int]($n * 2))
  $w.Write([uint16]1); $w.Write([uint16]32)
  $w.Write([uint32]0); $w.Write([uint32]($xorSize + $andSize))
  $w.Write([int]0); $w.Write([int]0); $w.Write([uint32]0); $w.Write([uint32]0)
  for ($y = $n - 1; $y -ge 0; $y--) {
    for ($x = 0; $x -lt $n; $x++) {
      $p = $b.GetPixel($x, $y)
      $w.Write([byte]$p.B); $w.Write([byte]$p.G); $w.Write([byte]$p.R); $w.Write([byte]$p.A)
    }
  }
  $w.Write((New-Object byte[] $andSize))
  $bytes = $out.ToArray()
  $w.Dispose(); $out.Dispose()
  return , $bytes # comma keeps the byte[] from being unrolled
}

$entries = New-Object System.Collections.ArrayList
[void]$entries.Add(@{ size = 0; data = $png }) # 256 (width byte 0 means 256)
foreach ($sz in @(48, 32, 16)) {
  $fb = New-BlockFace $sz 8
  [void]$entries.Add(@{ size = $sz; data = (Get-IcoBmpBytes $fb) })
  $fb.Dispose()
}

$ico = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ico)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
  $data = [byte[]]$e.data
  $bw.Write([byte]$e.size); $bw.Write([byte]$e.size) # width, height
  $bw.Write([byte]0); $bw.Write([byte]0)
  $bw.Write([uint16]1); $bw.Write([uint16]32)
  $bw.Write([uint32]$data.Length); $bw.Write([uint32]$offset)
  $offset += $data.Length
}
foreach ($e in $entries) { $bw.Write([byte[]]$e.data) }
[System.IO.File]::WriteAllBytes((Join-Path $iconDir 'blockcraft.ico'), $ico.ToArray())
$bw.Dispose(); $ico.Dispose()

Write-Output "wrote $iconDir\blockcraft.ico"

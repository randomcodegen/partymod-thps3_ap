[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$FontPath,
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $PSScriptRoot "..\bridge\src\onani_hud_font.inc"
}

$privateFonts = [System.Drawing.Text.PrivateFontCollection]::new()
$privateFonts.AddFontFile([IO.Path]::GetFullPath($FontPath))
if ($privateFonts.Families.Count -ne 1) {
    throw "Expected one font family in $FontPath"
}

$font = [System.Drawing.Font]::new(
    $privateFonts.Families[0], 14,
    [System.Drawing.FontStyle]::Regular,
    [System.Drawing.GraphicsUnit]::Pixel)
$measureBitmap = [System.Drawing.Bitmap]::new(1, 1)
$measureGraphics = [System.Drawing.Graphics]::FromImage($measureBitmap)
$format = [System.Drawing.StringFormat]::GenericTypographic.Clone()
$height = [int][Math]::Ceiling($font.GetHeight($measureGraphics))
$spaceAdvance = [int][Math]::Ceiling(
    $measureGraphics.MeasureString(
        "A A", $font, [int]::MaxValue, $format).Width -
    $measureGraphics.MeasureString(
        "AA", $font, [int]::MaxValue, $format).Width)
$glyphs = @()

foreach ($codepoint in 32..126) {
    $character = [char]$codepoint
    $advance = if ($character -eq ' ') {
        $spaceAdvance
    } else {
        [Math]::Max(1, [int][Math]::Ceiling(
            $measureGraphics.MeasureString(
                [string]$character, $font, [int]::MaxValue, $format).Width))
    }
    $bitmap = [System.Drawing.Bitmap]::new(32, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.TextRenderingHint =
        [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
    $graphics.DrawString(
        [string]$character, $font, [System.Drawing.Brushes]::White,
        [System.Drawing.PointF]::new(0, 0), $format)

    $rows = foreach ($y in 0..($height - 1)) {
        [uint32]$mask = 0
        foreach ($x in 0..31) {
            if ($bitmap.GetPixel($x, $y).A -ge 128) {
                $mask = $mask -bor ([uint32]1 -shl $x)
            }
        }
        $mask
    }
    $graphics.Dispose()
    $bitmap.Dispose()
    $glyphs += [pscustomobject]@{ Advance = $advance; Rows = $rows }
}

if ($spaceAdvance -lt 2 -or $glyphs.Count -ne 95 -or
        ($glyphs[[int][char]'A' - 32].Rows |
        Where-Object { $_ -ne 0 }).Count -eq 0) {
    throw "Font rasterization self-check failed"
}

$fontHash = (Get-FileHash -LiteralPath $FontPath -Algorithm SHA256).Hash
$lines = [Collections.Generic.List[string]]::new()
$lines.Add("// Generated from Onani.ttf (SHA-256 $fontHash).")
$lines.Add("// Non-commercial use permitted by TarmSaft.")
$lines.Add("struct HudFontGlyph {")
$lines.Add("    std::uint8_t advance;")
$lines.Add("    std::array<std::uint32_t, $height> rows;")
$lines.Add("};")
$lines.Add("constexpr std::uint32_t kOnaniHudFontHeight = $height;")
$lines.Add("constexpr std::array<HudFontGlyph, 95> kOnaniHudGlyphs{{")
foreach ($glyph in $glyphs) {
    $rows = ($glyph.Rows | ForEach-Object { "0x{0:x8}u" -f $_ }) -join ", "
    $lines.Add("    {$($glyph.Advance), {$rows}},")
}
$lines.Add("}};")
$lines.Add("static_assert(kOnaniHudGlyphs[0].advance >= 2);")
$lines.Add("static_assert(kOnaniHudGlyphs['A' - 32].rows[5] != 0u);")

$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($resolvedOutput)) |
    Out-Null
[IO.File]::WriteAllLines(
    $resolvedOutput, $lines, [Text.UTF8Encoding]::new($false))
Write-Output $resolvedOutput

$format.Dispose()
$measureGraphics.Dispose()
$measureBitmap.Dispose()
$font.Dispose()
$privateFonts.Dispose()

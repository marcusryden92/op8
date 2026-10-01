# OF2: generates the night vision color correction lookup,
# materials\correction\of2_nightvision.raw (used by client\hl2\of2_nightvision.cpp).
#
# Run from anywhere:  powershell -ExecutionPolicy Bypass -File of2_nightvision_lut.ps1
# The game reads the file when a map loads, so reload the map to see a change.
#
# Format: 32x32x32 entries of 3 bytes (R G B), red index changing fastest, blue slowest.
# Each entry is the color that input color gets replaced with.

# --- Tuning -------------------------------------------------------------------
$Gamma     = 0.40   # amplification curve, out = in ^ Gamma. Lower = dark areas lifted more.
$Ceiling   = 0.92   # brightest level the picture reaches, leaves headroom for the HUD
$TintDark  = @(1.00, 1.00, 1.00)   # color of dim areas. Both white = grayscale picture;
$TintLight = @(1.00, 1.00, 1.00)   # color of bright areas. (green was 0.06 1 0.40 / 0.70 1 0.78)
# ------------------------------------------------------------------------------

$Size  = 32
$bytes = New-Object byte[] ($Size * $Size * $Size * 3)
$i = 0
for ($b = 0; $b -lt $Size; $b++) {
    for ($g = 0; $g -lt $Size; $g++) {
        for ($r = 0; $r -lt $Size; $r++) {
            $fr = $r / ($Size - 1); $fg = $g / ($Size - 1); $fb = $b / ($Size - 1)

            # How bright the pixel is. Treats the three channels alike on purpose:
            # a pure red or blue light should show up as clearly as a green one.
            $max = [Math]::Max($fr, [Math]::Max($fg, $fb))
            $v   = 0.6 * $max + 0.4 * ($fr + $fg + $fb) / 3.0

            $level = $Ceiling * [Math]::Pow($v, $Gamma)
            $wash  = $level * $level
            for ($c = 0; $c -lt 3; $c++) {
                $tint = $TintDark[$c] + ($TintLight[$c] - $TintDark[$c]) * $wash
                $bytes[$i++] = [byte][Math]::Round(255.0 * [Math]::Min(1.0, $level * $tint))
            }
        }
    }
}

$out = Join-Path $PSScriptRoot '..\..\materials\correction\of2_nightvision.raw'
$dir = Split-Path $out
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
[IO.File]::WriteAllBytes($out, $bytes)
Write-Host "Wrote $((Resolve-Path $out).Path) ($($bytes.Length) bytes)"

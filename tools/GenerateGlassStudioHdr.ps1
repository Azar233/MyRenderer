param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\assets\environments\glass_studio.hdr')
)

$ErrorActionPreference = 'Stop'
$width = 256
$height = 128
New-Item -ItemType Directory -Force -Path (Split-Path $OutputPath) | Out-Null

function Convert-ToRgbe([double]$red, [double]$green, [double]$blue) {
    $maximum = [Math]::Max($red, [Math]::Max($green, $blue))
    if ($maximum -lt 1.0e-32) { return [byte[]](0, 0, 0, 0) }
    $exponent = [Math]::Floor([Math]::Log($maximum, 2.0)) + 1.0
    $scale = 256.0 / [Math]::Pow(2.0, $exponent)
    return [byte[]](@(
        [Math]::Clamp([int]($red * $scale), 0, 255),
        [Math]::Clamp([int]($green * $scale), 0, 255),
        [Math]::Clamp([int]($blue * $scale), 0, 255),
        [Math]::Clamp([int]($exponent + 128.0), 0, 255)
    ))
}

$stream = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create)
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    $header = "#?RADIANCE`nFORMAT=32-bit_rle_rgbe`n`n-Y $height +X $width`n"
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes($header))
    for ($y = 0; $y -lt $height; ++$y) {
        $v = ($y + 0.5) / $height
        $channels = @(
            [byte[]]::new($width),
            [byte[]]::new($width),
            [byte[]]::new($width),
            [byte[]]::new($width)
        )
        for ($x = 0; $x -lt $width; ++$x) {
            $u = ($x + 0.5) / $width
            $horizon = [Math]::Max(0.0, 1.0 - [Math]::Abs($v - 0.5) * 2.0)
            $red = 0.025 + 0.16 * $horizon
            $green = 0.035 + 0.20 * $horizon
            $blue = 0.065 + 0.26 * $horizon

            $keyBox = [Math]::Abs($u - 0.18) -lt 0.045 -and [Math]::Abs($v - 0.34) -lt 0.16
            $rimBox = [Math]::Abs($u - 0.72) -lt 0.025 -and [Math]::Abs($v - 0.42) -lt 0.12
            $warmStrip = [Math]::Abs($u - 0.47) -lt 0.12 -and [Math]::Abs($v - 0.76) -lt 0.025
            if ($keyBox) {
                $red += 9.0; $green += 7.2; $blue += 5.3
            }
            if ($rimBox) {
                $red += 2.8; $green += 5.0; $blue += 8.5
            }
            if ($warmStrip) {
                $red += 3.2; $green += 1.5; $blue += 0.55
            }
            $rgbe = Convert-ToRgbe $red $green $blue
            for ($channel = 0; $channel -lt 4; ++$channel) {
                $channels[$channel][$x] = $rgbe[$channel]
            }
        }
        $writer.Write([byte[]](2, 2, (($width -shr 8) -band 255), ($width -band 255)))
        for ($channel = 0; $channel -lt 4; ++$channel) {
            for ($offset = 0; $offset -lt $width; $offset += 128) {
                $count = [Math]::Min(128, $width - $offset)
                $writer.Write([byte]$count)
                $writer.Write($channels[$channel], $offset, $count)
            }
        }
    }
} finally {
    $writer.Dispose()
    $stream.Dispose()
}

Write-Host "Generated original studio HDRI: $OutputPath"

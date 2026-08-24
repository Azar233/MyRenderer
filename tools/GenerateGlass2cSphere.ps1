param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets\models')
)

$ErrorActionPreference = 'Stop'
$longitudeSegments = 64
$latitudeSegments = 32
$positions = [System.Collections.Generic.List[Single]]::new()
$normals = [System.Collections.Generic.List[Single]]::new()
$indices = [System.Collections.Generic.List[UInt16]]::new()

function Add-Vertex([single]$x, [single]$y, [single]$z) {
    $positions.Add($x)
    $positions.Add($y)
    $positions.Add($z)
    $normals.Add($x)
    $normals.Add($y)
    $normals.Add($z)
}

Add-Vertex 0.0 1.0 0.0
for ($latitude = 1; $latitude -lt $latitudeSegments; ++$latitude) {
    $theta = [Math]::PI * $latitude / $latitudeSegments
    $sinTheta = [Math]::Sin($theta)
    $cosTheta = [Math]::Cos($theta)
    for ($longitude = 0; $longitude -lt $longitudeSegments; ++$longitude) {
        $phi = 2.0 * [Math]::PI * $longitude / $longitudeSegments
        Add-Vertex `
            ([single]($sinTheta * [Math]::Cos($phi))) `
            ([single]$cosTheta) `
            ([single]($sinTheta * [Math]::Sin($phi)))
    }
}
$southPole = [uint16]($positions.Count / 3)
Add-Vertex 0.0 -1.0 0.0

for ($longitude = 0; $longitude -lt $longitudeSegments; ++$longitude) {
    $current = [uint16](1 + $longitude)
    $next = [uint16](1 + (($longitude + 1) % $longitudeSegments))
    $indices.Add(0)
    $indices.Add($next)
    $indices.Add($current)
}

for ($ring = 0; $ring -lt $latitudeSegments - 2; ++$ring) {
    $ringStart = 1 + $ring * $longitudeSegments
    $nextRingStart = $ringStart + $longitudeSegments
    for ($longitude = 0; $longitude -lt $longitudeSegments; ++$longitude) {
        $nextLongitude = ($longitude + 1) % $longitudeSegments
        $a = [uint16]($ringStart + $longitude)
        $b = [uint16]($ringStart + $nextLongitude)
        $c = [uint16]($nextRingStart + $nextLongitude)
        $d = [uint16]($nextRingStart + $longitude)
        $indices.Add($a)
        $indices.Add($b)
        $indices.Add($c)
        $indices.Add($a)
        $indices.Add($c)
        $indices.Add($d)
    }
}

$lastRingStart = 1 + ($latitudeSegments - 2) * $longitudeSegments
for ($longitude = 0; $longitude -lt $longitudeSegments; ++$longitude) {
    $current = [uint16]($lastRingStart + $longitude)
    $next = [uint16]($lastRingStart + (($longitude + 1) % $longitudeSegments))
    $indices.Add($southPole)
    $indices.Add($current)
    $indices.Add($next)
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$binaryPath = Join-Path $OutputDirectory 'glass_volume_sphere.bin'
$stream = [System.IO.File]::Open($binaryPath, [System.IO.FileMode]::Create)
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    foreach ($value in $positions) { $writer.Write([single]$value) }
    foreach ($value in $normals) { $writer.Write([single]$value) }
    foreach ($value in $indices) { $writer.Write([uint16]$value) }
} finally {
    $writer.Dispose()
    $stream.Dispose()
}

$positionBytes = $positions.Count * 4
$normalBytes = $normals.Count * 4
$indexBytes = $indices.Count * 2
$vertexCount = $positions.Count / 3
$json = @"
{
  "asset": {"version": "2.0", "generator": "MyRenderer Glass-2C fixture generator"},
  "extensionsUsed": ["KHR_materials_transmission", "KHR_materials_ior", "KHR_materials_volume"],
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "SmoothClosedVolumeSphere", "mesh": 0}],
  "meshes": [{
    "name": "SmoothClosedVolumeSphere",
    "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]
  }],
  "materials": [{
    "name": "OliveVolumeGlass",
    "alphaMode": "OPAQUE",
    "doubleSided": false,
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
      "metallicFactor": 0.0,
      "roughnessFactor": 0.06
    },
    "extensions": {
      "KHR_materials_transmission": {"transmissionFactor": 1.0},
      "KHR_materials_ior": {"ior": 1.5},
      "KHR_materials_volume": {
        "thicknessFactor": 2.0,
        "attenuationDistance": 0.85,
        "attenuationColor": [0.68, 0.86, 0.22]
      }
    }
  }],
  "buffers": [{"byteLength": $($positionBytes + $normalBytes + $indexBytes), "uri": "glass_volume_sphere.bin"}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": $positionBytes, "target": 34962},
    {"buffer": 0, "byteOffset": $positionBytes, "byteLength": $normalBytes, "target": 34962},
    {"buffer": 0, "byteOffset": $($positionBytes + $normalBytes), "byteLength": $indexBytes, "target": 34963}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": $vertexCount, "type": "VEC3", "min": [-1, -1, -1], "max": [1, 1, 1]},
    {"bufferView": 1, "componentType": 5126, "count": $vertexCount, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": $($indices.Count), "type": "SCALAR"}
  ]
}
"@
$gltfPath = Join-Path $OutputDirectory 'glass_volume_sphere.gltf'
[System.IO.File]::WriteAllText(
    $gltfPath,
    $json,
    [System.Text.UTF8Encoding]::new($false)
)

Write-Host "Generated $gltfPath ($vertexCount vertices, $($indices.Count / 3) triangles)"

param(
    [Parameter(Mandatory = $true)]
    [string]$SbomRoot,
    [Parameter(Mandatory = $true)]
    [string]$DestinationPath
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $SbomRoot -PathType Container)) {
    throw "vcpkg SBOM root does not exist: $SbomRoot"
}
if (Test-Path -LiteralPath $DestinationPath) {
    throw "SBOM archive destination already exists: $DestinationPath"
}

$resolvedRoot = (Resolve-Path -LiteralPath $SbomRoot).Path.TrimEnd('\')
$rootPrefix = $resolvedRoot + '\'
$sbomFiles = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Filter '*.spdx.json')
if ($sbomFiles.Count -eq 0) {
    throw "No SPDX JSON files found under $resolvedRoot"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archivePath = [System.IO.Path]::GetFullPath($DestinationPath)
$archive = [System.IO.Compression.ZipFile]::Open(
    $archivePath,
    [System.IO.Compression.ZipArchiveMode]::Create
)
$expectedEntries = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase
)
try {
    foreach ($sbomFile in $sbomFiles) {
        $fullPath = [System.IO.Path]::GetFullPath($sbomFile.FullName)
        if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "SPDX file resolves outside the installed tree: $fullPath"
        }

        $entryName = $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
        if (-not $expectedEntries.Add($entryName)) {
            throw "Duplicate SPDX archive path: $entryName"
        }
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive,
            $fullPath,
            $entryName,
            [System.IO.Compression.CompressionLevel]::Optimal
        ) | Out-Null
    }
}
finally {
    $archive.Dispose()
}

$verificationArchive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $actualEntries = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase
    )
    foreach ($entry in $verificationArchive.Entries) {
        if (-not $entry.FullName.EndsWith('.spdx.json', [System.StringComparison]::OrdinalIgnoreCase) -or
            -not $actualEntries.Add($entry.FullName)) {
            throw "SBOM archive contains an unexpected or duplicate entry: $($entry.FullName)"
        }
    }
    if ($actualEntries.Count -ne $expectedEntries.Count) {
        throw "SBOM archive has $($actualEntries.Count) entries; expected $($expectedEntries.Count)"
    }
    foreach ($entryName in $expectedEntries) {
        if (-not $actualEntries.Contains($entryName)) {
            throw "SBOM archive is missing SPDX file: $entryName"
        }
    }
}
finally {
    $verificationArchive.Dispose()
}

Write-Host "Archived and verified $($sbomFiles.Count) SPDX files with relative paths: $archivePath"

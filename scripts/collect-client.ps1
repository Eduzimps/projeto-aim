# Copies only the on-disk BloodStrike.exe into small ZIPs for static analysis.
# Windows PowerShell 5.1+; does not read game memory or modify game files.
[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [string]$OutputDirectory,
    [ValidateRange(1, 30)][int]$PartSizeMB = 30
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $gameProcess = Get-Process -Name BloodStrike -ErrorAction Stop |
        Select-Object -First 1
    $ExecutablePath = $gameProcess.MainModule.FileName
    if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
        throw 'Nao foi possivel localizar BloodStrike.exe. Envie o erro para analise.'
    }
}

$sourceFile = Get-Item -LiteralPath $ExecutablePath -ErrorAction Stop
if ($sourceFile -isnot [IO.FileInfo] -or $sourceFile.Name -ine 'BloodStrike.exe') {
    throw 'O arquivo selecionado precisa ser BloodStrike.exe.'
}
if ($sourceFile.Length -lt 2) { throw 'Executavel vazio ou incompleto.' }

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = [Environment]::GetFolderPath('DesktopDirectory')
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
        throw 'Pasta de destino indisponivel. Use -OutputDirectory com uma pasta existente.'
    }
}
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    throw 'A pasta de destino precisa existir.'
}
$outputFolder = Get-Item -LiteralPath $OutputDirectory -ErrorAction Stop
if ($outputFolder -isnot [IO.DirectoryInfo]) {
    throw 'O destino precisa ser uma pasta do sistema de arquivos.'
}
$OutputDirectory = $outputFolder.FullName

$partLimit = [long]$PartSizeMB * 1MB
$totalLength = [long]$sourceFile.Length
$partCount = [int][Math]::Ceiling($totalLength / [double]$partLimit)
$sourceHash = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash
$folderName = 'BloodStrike-analise-' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$packageDirectory = Join-Path $OutputDirectory $folderName
New-Item -ItemType Directory -Path $packageDirectory -ErrorAction Stop | Out-Null

$buffer = New-Object byte[] (1MB)
$parts = @()
$copyHasher = [Security.Cryptography.SHA256]::Create()
$sourceStream = $null
try {
    $sourceStream = [IO.File]::Open($sourceFile.FullName, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    if ($sourceStream.ReadByte() -ne 0x4D -or $sourceStream.ReadByte() -ne 0x5A) {
        throw 'O arquivo nao possui assinatura MZ de executavel Windows.'
    }
    $sourceStream.Position = 0
    for ($partIndex = 1; $partIndex -le $partCount; $partIndex++) {
        $zipName = 'BloodStrike-{0:D2}-de-{1:D2}.zip' -f $partIndex, $partCount
        $zipPath = Join-Path $packageDirectory $zipName
        $entryName = 'BloodStrike.exe.part{0:D2}' -f $partIndex
        $partLength = [Math]::Min($partLimit, $totalLength - $sourceStream.Position)
        $remaining = $partLength
        $archiveStream = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
        try {
            $archive = [IO.Compression.ZipArchive]::new($archiveStream,
                [IO.Compression.ZipArchiveMode]::Create, $true)
            try {
                $entry = $archive.CreateEntry($entryName, [IO.Compression.CompressionLevel]::Fastest)
                $entryStream = $entry.Open()
                try {
                    while ($remaining -gt 0) {
                        $requested = [int][Math]::Min([long]$buffer.Length, $remaining)
                        $bytesRead = $sourceStream.Read($buffer, 0, $requested)
                        if ($bytesRead -le 0) { throw 'Leitura incompleta do executavel.' }
                        $entryStream.Write($buffer, 0, $bytesRead)
                        $null = $copyHasher.TransformBlock($buffer, 0, $bytesRead, $buffer, 0)
                        $remaining -= $bytesRead
                    }
                } finally { $entryStream.Dispose() }
            } finally { $archive.Dispose() }
        } finally { $archiveStream.Dispose() }
        $parts += [ordered]@{
            index = $partIndex
            file = $zipName
            entry = $entryName
            uncompressed_length = $partLength
            sha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
        }
        Write-Host ('Parte {0}/{1} criada.' -f $partIndex, $partCount)
    }
    $null = $copyHasher.TransformFinalBlock((New-Object byte[] 0), 0, 0)
    $copiedHash = ([BitConverter]::ToString($copyHasher.Hash)).Replace('-', '')
} finally {
    if ($null -ne $sourceStream) { $sourceStream.Dispose() }
    $copyHasher.Dispose()
}

$finalFile = Get-Item -LiteralPath $sourceFile.FullName
$finalHash = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash
if ($finalFile.Length -ne $totalLength -or $finalHash -ne $sourceHash -or $copiedHash -ne $sourceHash) {
    throw 'O executavel mudou durante a copia. Nao envie esta pasta; tente novamente apos a atualizacao terminar.'
}

$manifest = [ordered]@{
    format = 'zyon-client-parts-v1'
    file_name = 'BloodStrike.exe'
    length = $totalLength
    sha256 = $sourceHash
    parts = $parts
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (
    Join-Path $packageDirectory 'manifesto.json') -Encoding UTF8
Write-Host ''
Write-Host 'Concluido. Envie todos os ZIPs desta pasta e o arquivo manifesto.json:'
Write-Host $packageDirectory

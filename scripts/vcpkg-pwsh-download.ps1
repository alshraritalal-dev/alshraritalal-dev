param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Url,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$ExpectedSha512,

    [Parameter(Mandatory = $true, Position = 2)]
    [string]$Destination
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$destinationDirectory = Split-Path -Parent $Destination
if (-not [string]::IsNullOrWhiteSpace($destinationDirectory)) {
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
}

$temporaryFile = "$Destination.tmp"
if (Test-Path -LiteralPath $temporaryFile) {
    Remove-Item -LiteralPath $temporaryFile -Force
}

Invoke-WebRequest -Uri $Url -OutFile $temporaryFile -UseBasicParsing

if ($ExpectedSha512 -and $ExpectedSha512 -ne "SKIP") {
    $actualSha512 = (Get-FileHash -Algorithm SHA512 -LiteralPath $temporaryFile).Hash.ToLowerInvariant()
    if ($actualSha512 -ne $ExpectedSha512.ToLowerInvariant()) {
        Remove-Item -LiteralPath $temporaryFile -Force -ErrorAction SilentlyContinue
        throw "SHA512 mismatch for $Url. Expected $ExpectedSha512, got $actualSha512."
    }
}

Move-Item -LiteralPath $temporaryFile -Destination $Destination -Force

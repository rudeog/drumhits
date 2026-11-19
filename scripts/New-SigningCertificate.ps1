param(
    [Parameter(Mandatory = $true)]
    [string] $CertificatePath
)

$ErrorActionPreference = 'Stop'

$fullPath = [System.IO.Path]::GetFullPath($CertificatePath)
$parent = [System.IO.Path]::GetDirectoryName($fullPath)
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    throw "Certificate output directory does not exist: $parent"
}
if (Test-Path -LiteralPath $fullPath) {
    throw "Certificate output already exists: $fullPath"
}

$certificate = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject 'CN=DrumHits Local Code Signing' `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -KeyAlgorithm RSA `
    -KeyLength 3072 `
    -KeyExportPolicy NonExportable `
    -KeyUsage DigitalSignature `
    -HashAlgorithm SHA256 `
    -NotAfter (Get-Date).AddYears(3)

try {
    Export-Certificate -Cert $certificate -FilePath $fullPath -Type CERT -NoClobber | Out-Null
} catch {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" -DeleteKey
    throw
}

Write-Host "Public certificate: $fullPath"
Write-Host "Signing certificate thumbprint: $($certificate.Thumbprint)"
Write-Host "The non-exportable private key is in Cert:\CurrentUser\My on this machine."

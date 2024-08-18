$apiUrl = "https://api.github.com/repos/skeeto/w64devkit/releases/latest"

# Get the latest release information
$latestRelease = Invoke-RestMethod -Uri $apiUrl -Headers @{ "User-Agent" = "PowerShell Script" }

# Extract the download URL for the .exe file
$asset = $latestRelease.assets | Where-Object { $_.name -like "*.exe" } | Select-Object -First 1
$downloadUrl = $asset.browser_download_url

# Define the path where the file will be downloaded
$destinationPath = "$env:TEMP\$($asset.name)"

# Download the file
Invoke-WebRequest -Uri $downloadUrl -OutFile $destinationPath

# Run the installer in silent/automatic mode
Start-Process -FilePath $destinationPath -ArgumentList '-o"C:\" -y' -Wait

# Default installation destination
$binPath = "C:\w64devkit\bin"

# Add to user's path only if path is not already in the user's PATH
$currentPath = [Environment]::GetEnvironmentVariable("Path", [EnvironmentVariableTarget]::User)
if (-not ($currentPath.Split(';') -contains $binPath)) {
    $newPath = "$currentPath;$binPath"
    [Environment]::SetEnvironmentVariable("Path", $newPath, [EnvironmentVariableTarget]::User)
    Write-Host "$binPath added to PATH."
} else {
    Write-Host "$binPath is already in the PATH."
}

# Delete the downloaded installer file
Remove-Item -Path $destinationPath -Force

Write-Host "w64devkit installed and environment variable PATH updated."

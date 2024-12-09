param(
    [Parameter(Mandatory=$true)]
    [string]$UrlListPath,               # Path to a text file with one URL per line

    [Parameter(Mandatory=$true)]
    [string]$BucketName,                # Name of the R2 bucket

    [Parameter(Mandatory=$true)]
    [string]$AccessKey,                 # Your R2 access key

    [Parameter(Mandatory=$true)]
    [string]$SecretKey,                 # Your R2 secret key

    [Parameter(Mandatory=$true)]
    [string]$EndPointURL,               # Your R2 endpoint URL, e.g. https://<account_id>.r2.cloudflarestorage.com

    [Parameter(Mandatory=$false)]
    [int]$MaxConcurrentConnections = 16, # Number of concurrent connections for each file download

    [Parameter(Mandatory=$false)]
    [int]$SplitCount = 16,               # Number of segments to split the download into

    [Parameter(Mandatory=$false)]
    [string]$AwsRegion = "auto"          # Region; often "auto" works for R2, but can be set if needed
)

# Set AWS environment variables for this session
$Env:AWS_ACCESS_KEY_ID = $AccessKey
$Env:AWS_SECRET_ACCESS_KEY = $SecretKey
$Env:AWS_REGION = $AwsRegion  # If required, or leave as "auto"

# Read all URLs from file
$Urls = Get-Content $UrlListPath | Where-Object { $_ -and $_. Trim() -ne "" }

# Ensure aria2 is available
if (-not (Get-Command aria2c -ErrorAction SilentlyContinue)) {
    Write-Error "aria2c not found in PATH. Please install aria2."
    exit 1
}

# Ensure aws CLI is available
if (-not (Get-Command aws -ErrorAction SilentlyContinue)) {
    Write-Error "aws CLI not found in PATH. Please install AWS CLI."
    exit 1
}

function Process-Url {
    param(
        [string]$Url,
        [string]$BucketName,
        [string]$EndPointURL,
        [int]$MaxConcurrentConnections,
        [int]$SplitCount
    )

    # Extract the filename from the URL
    $FileName = Split-Path -Leaf $Url

    try {
        Write-Host "Downloading: $Url"

        # Use aria2c to download with multiple connections
        & aria2c `
            --max-connection-per-server=$MaxConcurrentConnections `
            --split=$SplitCount `
            --out=$FileName `
            --check-certificate=false `
            --header="Cookie: _c_t_c=1" `
            $Url

        if (!(Test-Path $FileName)) {
            Write-Host "Failed to download $Url"
            return
        }

        Write-Host "Uploading $FileName to R2 bucket: $BucketName"
        & aws s3 cp $FileName "s3://$BucketName/$FileName" --endpoint-url $EndPointURL
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Failed to upload $FileName to R2"
            return
        }

        Write-Host "Upload successful. Deleting local file: $FileName"
        Remove-Item $FileName -Force

        Write-Host "Completed processing of $FileName."

    } catch {
        Write-Host "Error processing $Url"
        Write-Host $_
    }
}

# Process each URL sequentially here. If you'd like to run multiple URLs in parallel,
# you could replace the foreach loop with a ForEach-Object -Parallel block.
foreach ($Url in $Urls) {
    Process-Url -Url $Url -BucketName $BucketName -EndPointURL $EndPointURL -MaxConcurrentConnections $MaxConcurrentConnections -SplitCount $SplitCount
}

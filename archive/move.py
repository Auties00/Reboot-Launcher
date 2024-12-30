import argparse
import os
import requests
import boto3

from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.parse import urlparse

def upload_url_to_s3(s3_client, bucket_name, url, object_key):
    response = requests.get(url, stream=True, verify=False, headers={"Cookie": "_c_t_c=1"})
    response.raise_for_status()
    s3_client.upload_fileobj(response.raw, bucket_name, object_key)
    return url, object_key

def derive_key_from_url(url, prefix=None):
    parsed = urlparse(url)
    filename = os.path.basename(parsed.path)
    if prefix:
        return f"{prefix}/{filename}"
    else:
        return filename

def main():
    parser = argparse.ArgumentParser(description="Upload multiple URLs from versions.txt to an S3 bucket concurrently.")
    parser.add_argument('--bucket', required=True, help="Name of the S3 bucket.")
    parser.add_argument('--concurrency', required=True, type=int, help="Number of concurrent uploads.")
    parser.add_argument('--versions-file', default='versions.txt', help="File containing one URL per line.")
    parser.add_argument('--access-key', required=True, help="AWS Access Key ID.")
    parser.add_argument('--secret-key', required=True, help="AWS Secret Access Key.")
    parser.add_argument('--endpoint-url', required=True, help="Custom endpoint URL for S3 or S3-compatible storage.")
    args = parser.parse_args()

    bucket_name = args.bucket
    concurrency = args.concurrency
    versions_file = args.versions_file
    access_key = args.access_key
    secret_key = args.secret_key
    endpoint_url = args.endpoint_url

    with open(versions_file, 'r') as f:
        urls = [line.strip() for line in f if line.strip()]

    print(f"Uploading {len(urls)} files...")
    s3_params = {}
    if access_key and secret_key:
        s3_params['aws_access_key_id'] = access_key
        s3_params['aws_secret_access_key'] = secret_key
    if endpoint_url:
        s3_params['endpoint_url'] = endpoint_url

    s3 = boto3.client('s3', **s3_params)

    futures = []
    with ThreadPoolExecutor(max_workers=concurrency) as executor:
        for url in urls:
            object_key = derive_key_from_url(url)
            futures.append(executor.submit(upload_url_to_s3, s3, bucket_name, url, object_key))
        for future in as_completed(futures):
            try:
                uploaded_url, uploaded_key = future.result()
                print(f"Uploaded: {uploaded_url}")
            except Exception as e:
                print(f"Error uploading: {e}")

if __name__ == "__main__":
    main()

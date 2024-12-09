# Builds Archive

Builds are stored on a Cloudflare R2 instance at `https://builds.rebootfn.org/versions.json`.
If you want to move them to another AWS-compatible object storage, run:
```
move.ps1
```
and provide the required parameters.
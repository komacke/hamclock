# HamClock Release Process

This document describes how to initiate, monitor, and troubleshoot HamClock releases, along with the expected GitHub secrets, tag naming conventions, and pipeline architecture.

---

## 1. Initiating a Release via Command Line

HamClock releases are managed by the GitHub Actions workflow [`.github/workflows/release.yml`](.github/workflows/release.yml). Instead of navigating to the GitHub web interface, you can trigger and monitor releases directly from your terminal using the GitHub CLI (`gh`).

### Prerequisites

Ensure you have the GitHub CLI installed and authenticated:

```bash
# Check if logged in and verify token scopes (needs repo access)
gh auth status

# If needed, log in:
gh auth login
```

### Triggering the Release Workflow

#### Non-Interactive Command

Trigger the workflow by specifying the workflow file and the desired `tag_name`:

```bash
# Beta release example (builds Docker by default):
gh workflow run release.yml -f tag_name=v4.32b00.0

# Stable release example:
gh workflow run release.yml -f tag_name=v4.32.0

# Skip GitHub Docker build (e.g. if building Docker image locally):
gh workflow run release.yml -f tag_name=v4.32b00.0 -f build_docker=false

# Optional: target a specific branch (defaults to main)
gh workflow run release.yml --ref main -f tag_name=v4.32.0
```

#### Interactive Command

To run interactively, run `gh workflow run` with no arguments. `gh` will prompt you to select the workflow, enter the `tag_name`, and confirm whether to build Docker (`build_docker` defaults to `Yes`):

```bash
gh workflow run
```

*(Note: Specifying `release.yml` directly as an argument, e.g. `gh workflow run release.yml`, tells `gh` to run non-interactively, so it dispatches immediately using default values rather than prompting).*

### Monitoring the Workflow

#### Live Progress Watch

To stream progress for the newly triggered run:

```bash
# Watch the latest workflow run interactively
gh run watch
```

#### Listing Workflow Runs

To view recent release runs and their statuses:

```bash
gh run list --workflow=release.yml -L 5
```

#### Viewing Logs and Failures

To inspect the logs of a specific run:

```bash
# View summary of a run
gh run view <RUN_ID>

# View full logs or failed steps
gh run view <RUN_ID> --log
gh run view <RUN_ID> --log-failed
```

#### Verifying the Created Release

Once the pipeline completes:

```bash
# View release details and uploaded assets
gh release view <TAG_NAME>

# Example:
gh release view v4.32b00.0
```

---

## 2. Tag Naming Convention

The release workflow parses and validates the `tag_name` input with the following regex:

```bash
HC_VER_PATTERN='^[0-9]+\.[0-9]+(b[0-9][0-9])?$'
```

The workflow strips the leading `v` / `V` and strips the trailing patch component (`.*`):
- `TAG`: The full Git tag name (e.g., `v4.32.0` or `v4.32b00.0`).
- `HC_TAG`: The version embedded into HamClock sources (`ESPHamClock/version.cpp`) and asset file names.

### Allowed Formats

| Release Type | Tag Pattern | Example Tag | Derived `HC_TAG` |
|---|---|---|---|
| **Stable** | `v<major>.<minor>.<patch>` | `v4.32.0` | `4.32` |
| **Beta** | `v<major>.<minor>b<beta_num>.<patch>` | `v4.32b00.0` | `4.32b00` |

> [!IMPORTANT]
> Always include the trailing `.0` (or patch number) in `tag_name`. If omitted (e.g. `v4.32`), the pipeline's regex validation will fail and immediately abort the run.

---

## 3. Required GitHub Secrets

The release workflow requires secrets for Android code signing and Docker Hub publishing.

### Current Secrets Reference

| Secret Name | Required By | Description |
|---|---|---|
| `ANDROID_KEYSTORE_BASE64` | Android Build | Base64-encoded release `.keystore` / `.jks` file used by Gradle to sign release APK and AAB. |
| `ANDROID_KEYSTORE_PASSWORD` | Android Build | Password unlocking the release keystore file. |
| `ANDROID_KEY_ALIAS` | Android Build | Key alias inside the keystore for the HamClock release key. |
| `ANDROID_KEY_PASSWORD` | Android Build | *(Optional)* Key password. If unset or blank, Gradle falls back to `ANDROID_KEYSTORE_PASSWORD`. |
| `DOCKERHUB_USERNAME` | Docker Job | Docker Hub account username (e.g., `komacke`). |
| `DOCKERHUB_TOKEN` | Docker Job | Docker Hub Personal Access Token (PAT) with write/push permissions. |

### Inspecting and Managing Secrets via CLI

```bash
# List repository secrets
gh secret list

# Set or update a secret
gh secret set DOCKERHUB_TOKEN

# Encode and upload an Android keystore:
base64 -w 0 path/to/release.keystore | gh secret set ANDROID_KEYSTORE_BASE64
```

---

## 4. Pipeline Jobs & Artifacts

The release workflow executes two parallel jobs: `release` and `docker`.

### Job 1: `release`

1. **Version Injection:**
   - Patches `ESPHamClock/version.cpp` with `HC_TAG`.
   - Creates `VERSION.txt` containing `TAG=$TAG`.
   - Patches `docker/manage-hc-docker.sh` with `HC_MANAGER_VERSION=$TAG`.
2. **Archives Packaging:**
   - `dist/HC-$TAG.tar.gz` and `dist/HC-$TAG.zip` (full repository archives).
   - `dist/ESPHamClock-V$HC_TAG.zip` (standalone HamClock source zip).
   - `dist/hamclock-contrib-V$HC_TAG.zip` (contrib zip).
3. **Android Build:**
   - Runs `./gradlew assembleRelease bundleRelease` with JDK 17.
   - Outputs release `.apk` (direct install) and `.aab` (Google Play Store bundle).
4. **GitHub Release Publication:**
   - Creates the GitHub Release via `gh release create`.
   - Generates release notes automatically from commit log (`--generate-notes`).
   - Attaches:
     - `docker/manage-hc-docker-$TAG.sh`
     - `debian/install-hc-rpi`
     - `dist/ESPHamClock-V$HC_TAG.zip`
     - `old-versions/ESPHamClock-V3.10.zip`
     - `old-versions/ohb.hamclock.app_ESPHamClock-V3.10.ino.bin`
     - `dist/hamclock-contrib-V$HC_TAG.zip`
     - `doc/HamClockUserGuide.pdf`
     - `dist/org.openhamclock*.apk`
     - `dist/org.openhamclock*.aab`

### Job 2: `docker` *(Optional - enabled by default)*

*Can be skipped by passing `-f build_docker=false` via CLI or unchecking the box in the GitHub web UI.*

1. Sets up Docker Buildx and QEMU.
2. Logs in to Docker Hub using `DOCKERHUB_USERNAME` and `DOCKERHUB_TOKEN`.
3. Runs `./docker/build-image.sh -m` to build and push multi-platform images (`linux/amd64`, `linux/arm64`) tagged as `komacke/hamclock:$TAG` (and `latest` for stable releases).

---

## 5. Institutional Knowledge & Release Checklist

*(Add pre-release and post-release procedures, notification lists, and operational steps here.)*

- [ ] Ensure `main` branch is clean and all CI tests (`tests.yml`, `compile-web.yml`, `compile-fb0.yml`) are passing.
- [ ] Update release notes / change logs if needed (`HC_RELEASE-stable.txt` / `HC_RELEASE-beta.txt`).
- [ ] Update documentation (`doc/HamClockUserGuide.pdf` or related files) if new features are introduced.
- [ ] Run `gh workflow run release.yml -f tag_name=<tag>`.
- [ ] Monitor build and verify assets on GitHub Release page (`gh release view <tag>`).
- [ ] Verify multi-arch images on Docker Hub.

---

## 6. Verified Commits & Tags (SSH Signing Keys)

If your local git environment is configured to sign commits or tags with SSH (`commit.gpgsign=true` / `tag.gpgsign=true`), GitHub requires that your public key be registered specifically as a **Signing Key** (rather than only an Authentication Key):

1. Check your public key:
   ```bash
   cat ~/.ssh/git-signing.pub
   ```
2. Go to **GitHub Settings -> [SSH and GPG keys](https://github.com/settings/keys)**.
3. Click **New SSH Key**.
4. In the **Key type** dropdown, select **Signing Key**.
5. Paste your public key and save.

*(Alternatively, run `gh auth refresh -h github.com -s admin:ssh_signing_key` and then `gh ssh-key add ~/.ssh/git-signing.pub --type signing`)*.

Once added as a signing key, GitHub will mark your releases, tags, and commits with the green **Verified** badge.


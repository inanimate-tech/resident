# Publishing Resident

## CI/CD (preferred)

Publishing is automated via GitHub Actions. Pushing a version tag triggers the `publish.yml` workflow, which runs pre-flight checks and publishes to both registries.

### Setup (one-time)

Add these secrets in your GitHub repo settings (**Settings > Secrets and variables > Actions**):

- `PLATFORMIO_AUTH_TOKEN` — generate with `pio account token`
- `IDF_COMPONENT_API_TOKEN` — generate at [components.espressif.com](https://components.espressif.com) under **Settings > Tokens**

### Publishing a release

1. Bump `version` in both `library.json` and `idf_component.yml` (must match, no `-dev` suffix)
2. Commit and push to `main`
3. Tag and push:
   ```bash
   git tag v0.3.0
   git push origin v0.3.0
   ```

The workflow will:
- Verify versions in both files match
- Verify no pre-release suffix (e.g. `-dev`)
- Verify the version is greater than what's currently published on each registry
- Check that both secrets are configured
- Publish to PlatformIO Registry and ESP Component Registry in parallel

### Pre-flight checks locally

You can run the same checks locally before tagging:

```bash
./tools/publish-preflight.py
```

## Manual publishing (backup)

### Prerequisites

Both registries upload source from your local directory — the GitHub repo does not need to be public.

#### PlatformIO

1. Create account: `pio account register` (or at [registry.platformio.org](https://registry.platformio.org))
2. Log in: `pio account login`

#### ESP Component Registry

1. Sign up at [components.espressif.com](https://components.espressif.com) (GitHub OAuth)
2. Go to **Settings > Tokens**, generate an API token
3. `export IDF_COMPONENT_API_TOKEN=<your-token>`
4. Install compote if needed: `get_idf && pip install idf-component-manager`

Custom namespaces (e.g. `inanimate` instead of your username) require manual approval — request at components.espressif.com before publishing.

### Before publishing

1. Bump `version` in both `library.json` and `idf_component.yml`
2. Verify packaged contents:

```bash
# PlatformIO — shows what files would be included
pio pkg pack

# ESP Component Registry
compote component pack --name resident
```

### PlatformIO Registry

```bash
pio pkg publish --owner inanimate
```

### ESP Component Registry

```bash
compote component upload --name resident --namespace inanimate
```

Dry run (validates without publishing):
```bash
compote component upload --name resident --namespace inanimate --dry-run
```

## Version rules

- **Versions are permanent** — once published, a version can never be reused (even if unpublished)
- **Bump for every release** — both registries reject duplicate versions
- **Use semver** — `0.1.0`, `0.2.0`, `1.0.0`, etc.
- **Use `-dev` suffix** during active development (e.g. `0.3.0-dev`) — the pre-flight check will reject it at publish time

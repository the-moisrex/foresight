# Release Process

This document describes how to create a new release of Foresight.

## Overview

Foresight uses a semi-automated release process:

1. **Version bump**: Update the `VERSION` file
2. **Tag**: Create a git tag
3. **CI/CD**: GitHub Actions automatically builds, tests, and creates a release
4. **Changelog**: Generated automatically from commit messages

## Prerequisites

- Git
- GPG key for signing tags
- GitHub repository with Actions enabled

## Release Steps

### 1. Update Version

Edit the `VERSION` file in the root directory:

```bash
echo "2.1.0" > VERSION
```

### 2. Update CHANGELOG (Optional)

If you want to manually edit the changelog before release:

```bash
# Install git-cliff (if not already installed)
# Ubuntu/Debian:
sudo apt-get install git-cliff

# Or via cargo:
cargo install git-cliff

# Generate changelog for review
git cliff --tag v2.1.0 -o CHANGELOG-v2.1.0.md

# Review and edit the generated file
# Then use it in the release
```

### 3. Commit Changes

```bash
git add VERSION CHANGELOG.md
git commit -m "chore: prepare release v2.1.0"
```

### 4. Create and Push Tag

```bash
# Create annotated tag
git tag -s v2.1.0 -m "Release v2.1.0"

# Push changes and tag
git push origin main
git push origin v2.1.0
```

### 5. Automated Release

Once the tag is pushed, GitHub Actions will automatically:

1. Build binaries for GCC and Clang
2. Run tests
3. Generate changelog using git-cliff
4. Create a GitHub Release with:
   - Release notes from changelog
   - Pre-built binaries for both compilers
   - Source archives

## Version Scheme

We follow [Semantic Versioning](https://semver.org/):

- **MAJOR** (X.0.0): Incompatible API changes
- **MINOR** (0.X.0): New functionality in a backwards compatible manner
- **PATCH** (0.0.X): Backwards compatible bug fixes

### Pre-release Versions

For pre-release versions, use suffixes:

- Alpha: `2.1.0-alpha.1`
- Beta: `2.1.0-beta.1`
- Release Candidate: `2.1.0-rc.1`

Update the VERSION file accordingly:

```bash
echo "2.1.0-beta.1" > VERSION
```

## Local Testing

### Test Version Embedding

```bash
cmake -B build -G Ninja
cmake --build build
./build/foresight --version
# Should output: foresight 2.0.0
```

### Test Changelog Generation

```bash
# Install git-cliff
cargo install git-cliff

# Generate changelog
git cliff -o CHANGELOG.md

# Preview changelog
git cliff --unreleased
```

## Troubleshooting

### Build Fails in CI

- Check that all dependencies are available
- Verify CMake version requirement (3.23+)
- Ensure GCC 14+ or Clang 16+ is available

### Tag Already Exists

```bash
# Delete local tag
git tag -d v2.1.0

# Delete remote tag (use with caution!)
git push --delete origin v2.1.0
```

### Changelog Not Generated

- Ensure git-cliff is installed in the CI environment
- Check that the tag format matches `v*`

## Files Involved

- `VERSION`: Contains the current version number
- `CMakeLists.txt`: Reads VERSION and exposes it to the build
- `main/main.cxx`: Implements `--version` flag
- `.github/workflows/release.yml`: Automated release workflow
- `cliff.toml`: Configuration for changelog generation
- `CHANGELOG.md`: Project changelog

## GPG Signing

All release tags are GPG-signed. To set up GPG signing:

```bash
# Generate GPG key (if needed)
gpg --gen-key

# Configure git to use your key
git config --global user.signingkey YOUR_KEY_ID

# Verify tag signature
git verify-tag v2.1.0
```

## Rollback

If a release has issues:

1. **Do not delete the tag** - this can break dependencies
2. Create a new patch release with fixes
3. Mark the problematic release as "Pre-release" on GitHub
4. Add a note to the release describing the issue

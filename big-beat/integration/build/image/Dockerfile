# Build runtime for build_from_source.py: Debian bookworm (pinned digest, as BUILD_SETUP.md) + python3.
# The Jieli toolchain is mounted read-only from Orange Base; this image only supplies the Linux/amd64
# userland it runs on. Equivalence is proven by reproducing installed boot42 byte for byte.
FROM debian@sha256:7c7b2c966bc9ee8cedfeef67e0e279108992c77681fa595db4a9d65c06ccc587
RUN apt-get update && apt-get install -y --no-install-recommends python3 && rm -rf /var/lib/apt/lists/*

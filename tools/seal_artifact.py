#!/usr/bin/env python3
"""Seals a rebuild artifact for Tikpal's review machine (.github/workflows/rebuild.yml): this repository is public, and
so are its Actions artifacts, so what the Rebuild uploads (the source list, the build with its ELF and map, the build's
log) goes up sealed with the review machine's public key (review/rebuild-seal.pub) and only that machine opens it
(yaykit/scripts/review/sealed.py in Tikpal's repository). The key is not a secret; the machine's private one never
leaves it.

  seal_artifact.py <public key file> <in> <out>

The sealed file: b"TKSEAL1\\n", an ephemeral X25519 public key (32 bytes), a nonce (12), then the input sealed with
ChaCha20-Poly1305 under HKDF-SHA256(the X25519 shared secret, info b"tikpal-rebuild-seal-v1"), the magic and both public
keys as its associated data. Needs `cryptography` (ESP-IDF's Python environment has it, for esptool).
"""
import base64
import os
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

MAGIC = b"TKSEAL1\n"
INFO = b"tikpal-rebuild-seal-v1"
RAW = serialization.Encoding.Raw, serialization.PublicFormat.Raw


def public_key(path):
    """The review machine's key: one line, the 32 raw bytes in base64."""
    text = open(path, encoding="ascii").read().strip()
    raw = base64.b64decode(text, validate=True)
    if len(raw) != 32:
        raise SystemExit(f"{path} is not a review machine's public key (32 bytes in base64)")
    return raw


def seal(recipient, data):
    ephemeral = X25519PrivateKey.generate()
    shared = ephemeral.exchange(X25519PublicKey.from_public_bytes(recipient))
    ours = ephemeral.public_key().public_bytes(*RAW)
    key = HKDF(algorithm=hashes.SHA256(), length=32, salt=None, info=INFO).derive(shared)
    nonce = os.urandom(12)
    return MAGIC + ours + nonce + ChaCha20Poly1305(key).encrypt(nonce, data, MAGIC + ours + recipient)


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    key, source, target = argv
    with open(source, "rb") as fh:
        data = fh.read()
    with open(target, "wb") as fh:
        fh.write(seal(public_key(key), data))


if __name__ == "__main__":
    main(sys.argv[1:])

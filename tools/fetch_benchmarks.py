#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, urllib.request
from pathlib import Path


def sha256(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''): h.update(b)
    return h.hexdigest()


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--manifest',type=Path,required=True); ap.add_argument('--dest',type=Path,required=True); a=ap.parse_args()
    m=json.loads(a.manifest.read_text()); a.dest.mkdir(parents=True,exist_ok=True)
    for item in m['instances']:
        if not item.get('url') or not item.get('sha256'):
            raise SystemExit(f"manifest entry lacks url/sha256: {item.get('name')}")
        out=a.dest/item['path']; out.parent.mkdir(parents=True,exist_ok=True)
        if not out.exists(): urllib.request.urlretrieve(item['url'],out)
        got=sha256(out)
        if got != item['sha256']: raise SystemExit(f"SHA-256 mismatch {item['name']}: {got}")
        print(f"OK {item['name']} {got}")
if __name__=='__main__': main()

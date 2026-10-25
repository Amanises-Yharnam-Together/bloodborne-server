"""Shader packs: a shader cache we built, shipped in shader_packs/ so a new player's first start
does not compile every shader while playing.

The port installs a pack at start when its own cache would start empty (none yet, another build or
another graphics card) and the pack's profile.bin is the one this build writes for this GPU
(PipelineCache::WarmUp, Storage::DataBase::InstallPack). A cache only fits one GPU profile, so
there is one pack per kind of card (NVIDIA RTX, AMD, ...). Packs are ours only: a cache holds
native code the game runs, so a cache from anyone else is never installed this way.

    py -3.12 scripts/shader_pack.py make <user>/cache/CUSA03173 shader_packs/nvidia.zip
    py -3.12 scripts/shader_pack.py info shader_packs/nvidia.zip
    py -3.12 scripts/shader_pack.py current C:/bb/shader-packs shader_packs  (the release's copy)
"""
import argparse
import hashlib
import re
import struct
import sys
import zipfile
from pathlib import Path

# The cache's own blob names, as CacheCheck::IsPackEntryName accepts them.
ENTRY = re.compile(r'(profile\.bin|[gc]_0x[0-9a-f]{16}\.key|0x[0-9a-f]{16}\.meta|0x[0-9a-f]{16}_[0-9]{1,4}\.spv)')
# profile.bin starts with "CPBB" and the shader binary, meta and pipeline key versions.
PROFILE_MAGIC = 0x42425043


def is_entry(name):
    return ENTRY.fullmatch(name) is not None


def profile_versions(blob):
    """(binary, meta, key) versions of a profile.bin, or None when it is not one of ours."""
    if len(blob) < 16:
        return None
    magic, binary, meta, key = struct.unpack_from('<4I', blob)
    return (binary, meta, key) if magic == PROFILE_MAGIC else None


def make(cache_dir, out):
    """Writes the pack of a cache folder; returns (entries, sha256). The driver's own cache
    (vk_pipeline_cache.vkc, one driver's binary) and half-written .tmp files stay out."""
    cache_dir, out = Path(cache_dir), Path(out)
    profile = cache_dir / 'profile.bin'
    if not profile.is_file() or profile_versions(profile.read_bytes()) is None:
        raise SystemExit(f'{cache_dir} has no profile.bin of this port: start the game once')
    names = sorted(p.name for p in cache_dir.iterdir() if p.is_file() and is_entry(p.name))
    if not any(name.endswith('.key') for name in names):
        raise SystemExit(f'{cache_dir} holds no pipelines yet: play first')
    out.parent.mkdir(parents=True, exist_ok=True)
    temp = out.with_name(out.name + '.tmp')
    with zipfile.ZipFile(temp, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as pack:
        # profile.bin first: the port reads it before anything else.
        for name in ['profile.bin'] + [n for n in names if n != 'profile.bin']:
            pack.write(cache_dir / name, name)
    temp.replace(out)
    return len(names), hashlib.sha256(out.read_bytes()).hexdigest()


def info(pack_path):
    """What a pack holds: counts per kind, versions, and whether every entry is a cache blob."""
    with zipfile.ZipFile(pack_path) as pack:
        names = pack.namelist()
        blob = pack.read('profile.bin') if 'profile.bin' in names else b''
    counts = {'key': 0, 'meta': 0, 'spv': 0}
    for name in names:
        suffix = name.rsplit('.', 1)[-1]
        if is_entry(name) and suffix in counts:
            counts[suffix] += 1
    return {
        'versions': profile_versions(blob),
        'pipelines': counts['key'],
        'shaders': counts['meta'],
        'binaries': counts['spv'],
        'foreign': [name for name in names if not is_entry(name)],
        'sha256': hashlib.sha256(Path(pack_path).read_bytes()).hexdigest(),
    }


SERIALIZATION = Path(__file__).resolve().parent.parent / 'gpu' / 'shadps4' / 'video_core' / 'renderer_vulkan' / 'vk_pipeline_serialization.cpp'


def source_versions(source=SERIALIZATION):
    """(binary, meta, key) versions this source tree writes into profile.bin."""
    text = Path(source).read_text(encoding='utf-8')
    found = [re.search(rf'{name}\s*=\s*(\d+)u', text) for name in ('ShaderBinaryVersion', 'ShaderMetaVersion', 'PipelineKeyVersion')]
    if not all(found):
        raise SystemExit(f'cache versions not found in {source}')
    return tuple(int(match.group(1)) for match in found)


def copy_current(packs_dir, dest, versions=None):
    """Copies the packs of this build's cache versions into dest (the port's shader_packs) and
    returns (copied, stale) file names. A pack of other versions would never be installed: the
    port rebuilds such a cache, so it only makes the setup bigger."""
    versions = versions or source_versions()
    dest = Path(dest)
    if dest.exists():
        for old in dest.glob('*.zip'):
            old.unlink()
    copied, stale = [], []
    for pack in sorted(Path(packs_dir).glob('*.zip')):
        details = info(pack)
        if details['versions'] != versions or details['foreign'] or not details['pipelines']:
            stale.append(pack.name)
            continue
        dest.mkdir(parents=True, exist_ok=True)
        (dest / pack.name).write_bytes(pack.read_bytes())
        copied.append(pack.name)
    return copied, stale


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    make_cmd = sub.add_parser('make', help='pack a cache folder')
    make_cmd.add_argument('cache_dir')
    make_cmd.add_argument('out')
    info_cmd = sub.add_parser('info', help='describe a pack')
    info_cmd.add_argument('pack')
    current_cmd = sub.add_parser('current', help="copy the packs of this build's cache versions")
    current_cmd.add_argument('packs_dir')
    current_cmd.add_argument('dest')
    args = parser.parse_args(argv)
    if args.command == 'current':
        copied, stale = copy_current(args.packs_dir, args.dest)
        print(f"shader packs: {', '.join(copied) or 'none'}" +
              (f"; left out, made by another build (make them again): {', '.join(stale)}" if stale else ''))
        return 0
    if args.command == 'make':
        entries, digest = make(args.cache_dir, args.out)
        print(f'{args.out}: {entries} entries, {Path(args.out).stat().st_size // 1024} KiB, sha256 {digest}')
        return 0
    details = info(args.pack)
    print(f"versions (binary, meta, key): {details['versions']}")
    print(f"{details['pipelines']} pipelines, {details['shaders']} shaders, {details['binaries']} binaries")
    print(f"sha256 {details['sha256']}")
    if details['foreign']:
        print(f"not cache blobs (the port leaves them out): {', '.join(details['foreign'])}")
    return 1 if details['versions'] is None or details['foreign'] else 0


if __name__ == '__main__':
    sys.exit(main())

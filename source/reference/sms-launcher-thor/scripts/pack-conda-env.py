"""Pack relocation data while retaining normalized symlinks from the installed tree."""
import os
import sys
import conda_pack
from conda_pack.formats import TarArchive
from io import BytesIO
import tarfile


def add_bytes(self, source, data, target):
    info = self.archive.gettarinfo(source, target)
    # Rewritten data must have a regular header, even if the source inode was
    # already archived. A hardlink with a nonzero payload corrupts GNU tar.
    if info.islnk():
        info.type = tarfile.REGTYPE
        info.linkname = ''
    info.size = len(data)
    self.archive.addfile(info, BytesIO(data))


TarArchive._add_bytes = add_bytes

prefix, output = sys.argv[1:]
environment = conda_pack.CondaEnv.from_prefix(prefix)
for record in environment.files:
    installed = os.path.join(prefix, record.target)
    # conda-pack normally copies managed symlinks from its original package cache.
    # Use our flattened links so extraction never has to follow a symlink chain.
    if os.path.islink(installed):
        record.source = installed
environment.pack(output=output, arcroot="env", force=True)

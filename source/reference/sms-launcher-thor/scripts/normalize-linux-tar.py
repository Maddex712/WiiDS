"""Repair conda-pack's hardlink headers while preserving every payload byte.

Older conda-pack archives can contain data after a hardlink header. GNU tar
skips that data; the launcher extractor consumes it. Materialize these entries
as regular files, keeping their content and relocation records intact.
"""
import gzip
import pathlib
import sys
import tarfile


class CompatibleInfo(tarfile.TarInfo):
    def _proc_builtin(self, archive):
        if self.islnk() and self.size:
            self.type = tarfile.REGTYPE
            self.linkname = ''
            self.pax_headers.pop('linkpath', None)
        return super()._proc_builtin(archive)


def normalize(source, destination):
    with tarfile.open(source, 'r|gz', tarinfo=CompatibleInfo) as reader:
        with open(destination, 'wb') as raw:
            with gzip.GzipFile(filename='', mode='wb', fileobj=raw, compresslevel=4, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.PAX_FORMAT) as writer:
                    for info in reader:
                        writer.addfile(info, reader.extractfile(info) if info.isfile() else None)


if __name__ == '__main__':
    normalize(*sys.argv[1:])

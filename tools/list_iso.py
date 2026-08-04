#!/usr/bin/env python3
"""List files in the extracted ISO9660 image with sizes. Requires pycdlib."""
import sys
import pycdlib

def main(iso_path):
    iso = pycdlib.PyCdlib()
    iso.open(iso_path)
    for child in iso.list_children(iso_path='/'):
        if child is None or child.is_dot() or child.is_dotdot():
            continue
        name = child.file_identifier().decode('ascii', errors='replace')
        size = child.get_data_length()
        print(f"{size:>10}  {name}")
    iso.close()

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "track1.iso")

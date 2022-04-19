#!/usr/bin/env python3
# $$PHOTON_UNPUBLISHED_FILE$$

import os
import subprocess

tools_dir = os.path.dirname(os.path.realpath(__file__))
root_dir = os.path.dirname(tools_dir)

tag = '$$PHOTON_UNPUBLISHED_FILE$$'

def check_unpublished(file):
    try:
        with open(file, "r") as f:
            lines = f.readlines()
    except UnicodeDecodeError:
        # Ignore binary file
        return False
    if len(lines) == 1 and tag in lines[0]:
        return True
    if len(lines) >= 2 and (tag in lines[0] or tag in lines[1]):
        return True
    return False

for file in subprocess.getoutput('find %s -type f' % root_dir).split('\n'):
    if not file:
        continue
    file = file.strip()
    if file.startswith('%s/build/' % root_dir) or file.startswith('%s/.git/' % root_dir):
        continue
    if check_unpublished(file):
        print('Purge %s' % file)
        os.remove(file)

print('\nUnpublished files purged. Now you can re-commit the git repo.')
#!/usr/bin/env python3
# $$PHOTON_UNPUBLISHED_FILE$$

import os
import sys
import subprocess

if len(sys.argv) != 2:
    print('Usage: ./publish.py <your/PhotonLibOS/dir>')
    exit(1)

tools_dir = os.path.dirname(os.path.realpath(__file__))
root_dir = os.path.dirname(tools_dir)
dst_repo_dir = sys.argv[1]

if not os.path.exists(os.path.join(dst_repo_dir, '.git/')):
    print('Dst repo not exist')
    exit(1)

# Step 1: purge unpublished files and commit to a temporary change

os.chdir(root_dir)

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

subprocess.run(['git', 'commit', '-m', 'any_message', '.'], check=True)

# Step 2: archive tarball
subprocess.run(['git', 'archive', '-o', 'photon-publish.tar.gz', 'HEAD'], check=True)

# Step 3: overwrite all code
os.chdir(dst_repo_dir)
subprocess.run('rm -rf *', shell=True, check=True)

tarball = os.path.join(root_dir, 'photon-publish.tar.gz')
subprocess.run(['tar', 'xf', tarball, '-C', '.'], check=True)

# Step 4: remove tarball
subprocess.run(['rm', '-rf', tarball], check=True)

# Step 5: reset temporary change
os.chdir(root_dir)
subprocess.run(['git', 'reset', '--hard', 'HEAD~1'], check=True)

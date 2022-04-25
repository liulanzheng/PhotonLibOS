#!/bin/bash
# $$PHOTON_UNPUBLISHED_FILE$$

set -x

VERSION=$3
if [ -z "$VERSION" ]; then
    echo "need specify version number !"
    echo "such as: ./t-storage-photon-build.sh 1 2 1.0.0"
    exit 1;
fi

RELEASE=$4
if [ -z "$RELEASE" ]; then
    RELEASE="$(date +%Y%m%d%H%M%S).$(git log --pretty=format:%h -1)"
fi

CUR_DIR=$(cd "$(dirname "$0")" && pwd)
cd $CUR_DIR
ROOT=$CUR_DIR/../

cd $ROOT
git submodule update --init --recursive
./build.sh -c -b release -d || exit 1

TOP_DIR=/tmp/photon_rpm_build/
rm -rf $TOP_DIR
mkdir -p $TOP_DIR

cd $CUR_DIR
rpmbuild -bb --define "_topdir $TOP_DIR" --define "_rpm_version ${VERSION}" --define "_rpm_release ${RELEASE}" ./t-storage-photon.spec

ls -lha $TOP_DIR/RPMS/x86_64/

RPM_DIR=$TOP_DIR/RPMS/x86_64/
for rpm in `find $RPM_DIR -name "*${RELEASE}*.rpm"`; do
    mv $rpm .
done

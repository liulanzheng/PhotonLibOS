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

uname -a | grep x86_64
if [ $? -eq 0 ]; then
    ARCH=x86_64
else
    ARCH=aarch64
fi
# ./build.sh -c -b release -d || exit 1

uname -a | grep al8
if [ $? -eq 0 ]; then
    # alios8
    if [ $ARCH == "x86_64" ]; then
        # x86
        sudo rpm -Uvh http://yum.tbsite.net/taobao/7/x86_64/current/mag/mag-1.0.25-20230315134520.alios7.x86_64.rpm
    else
        # aarch64
        sudo rpm -Uvh http://yum.tbsite.net/taobao/7/aarch64/current/mag/mag-1.0.25-20230315134520.alios7.aarch64.rpm
    fi
else
    # alios7
    sudo yum install -b current mag -y
fi

mag update --images -i /home/admin/.ssh/id_rsa
mkdir -p build
mkdir -p package
mag build --system=alios7u-gcc9.2.1 -I Magfiles -j 64 -o build/photon.tar.gz -i /home/admin/.ssh/id_rsa --upload
pushd build
tar xvpf photon.tar.gz
popd
cp build/lib64/libphoton.* package/

TOP_DIR=/tmp/photon_rpm_build/
rm -rf $TOP_DIR
mkdir -p $TOP_DIR

cd $CUR_DIR
rpmbuild -bb --define "_topdir $TOP_DIR" --define "_rpm_version ${VERSION}" --define "_rpm_release ${RELEASE}" ./t-storage-photon.spec

ls -lha $TOP_DIR/RPMS/${ARCH}/

RPM_DIR=$TOP_DIR/RPMS/${ARCH}/
for rpm in $(find $RPM_DIR -name "*${RELEASE}*.rpm"); do
    mv $rpm .
done

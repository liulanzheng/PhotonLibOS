##############################################################
# http://baike.corp.taobao.com/index.php/%E6%B7%98%E5%AE%9Drpm%E6%89%93%E5%8C%85%E8%A7%84%E8%8C%83 # 
# http://www.rpm.org/max-rpm/ch-rpm-inside.html              #
##############################################################
Name: t-storage-photon
Version: 1.0.0
Release: %{_rpm_release}%{?dist}
Summary: Photon library
Group: alibaba/library
License: Commercial
AutoReqProv: none
Requires: glibc >= 2.17
%define _prefix /usr
%define debug_package %{nil}
%define __strip /bin/true

%description
This package includes photon library, or libphoton.

%debug_package

%prep

%install
BASE=$OLDPWD/..
ls -lha $BASE
ls -lha $BASE/package
mkdir -p ${RPM_BUILD_ROOT}/usr/%{_lib}
mkdir -p ${RPM_BUILD_ROOT}/usr/include/photon
cp -P $BASE/package/* ${RPM_BUILD_ROOT}/usr/%{_lib}/
cp -rL $BASE/include/photon/* ${RPM_BUILD_ROOT}/usr/include/photon/

%files
%defattr(-,root,root)
%{_prefix}/lib64/*
%{_prefix}/include/*

%post

%changelog

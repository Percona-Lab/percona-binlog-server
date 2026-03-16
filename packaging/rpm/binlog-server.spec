%global percona_binlog_server_version @@PBS_RELEASE@@
%global rpm_release @@RPM_RELEASE@@
%global BUILD_PRESET_DEBUG debug_gcc14
%global BUILD_PRESET_RELEASE release_gcc14
%global BOOST_VERSION 1.88.0
%global AWS_VERSION 1.11.570
%global release %{rpm_release}%{?dist}
%global optflags %(echo %{optflags} | sed 's/-specs=[^ ]*annobin[^ ]*//g')

Name:           percona-binlog-server
Version:        %{percona_binlog_server_version}
Release:        %{release}
Summary:        Percona Binary Log Server

License:        GPLv2
URL:            https://github.com/Percona-Lab/percona-binlog-server
Source0:        %{name}-%{version}.tar.gz

%description
Percona Binary Log Server is a command-line utility that acts as an enhanced version of mysqlbinlog in --read-from-remote-server mode. It serves as a replication client and can stream binary log events from a remote Oracle MySQL Server / Percona Server for MySQL both to a local filesystem and to a cloud storage (currently AWS S3). The tool is capable of automatically reconnecting to the remote server and resuming operations from the point where it was previously stopped.

%prep
%setup -q

%build
# Build Debug version
mkdir -p debug
(
%if 0%{?amzn}
  echo "Running Amazon Linux–specific command"
  sed -i 's:gcc-14:gcc14-gcc:' ../percona-binlog-server-%{version}/extra/cmake_presets/boost/CMakePresets.json
  sed -i 's:g++-14:gcc14-g++:' ../percona-binlog-server-%{version}/extra/cmake_presets/boost/CMakePresets.json
  export CFLAGS="${CFLAGS//-Werror*/}"
  export CXXFLAGS="${CXXFLAGS//-Werror*/}"
  export LDFLAGS="${LDFLAGS//-Werror*/}"
  export CFLAGS="${CFLAGS//-specs*annobin*/}"
  export CXXFLAGS="${CXXFLAGS//-specs*annobin*/}"
  export LDFLAGS="${LDFLAGS//-specs*annobin*/}"
  export CFLAGS="${CFLAGS//-specs*redhat-hardened-cc1*/}"
  export CXXFLAGS="${CXXFLAGS//-specs*redhat-hardened-cc1*/}"
  export LDFLAGS="${LDFLAGS//-specs*redhat-hardened-ld*/}"
%endif
  sed -i 's:/boost-install-:/percona-binlog-server-%{version}/debug/boost-install-:' ../percona-binlog-server-%{version}/CMakePresets.json
  sed -i 's:/aws-sdk-cpp-install-:/percona-binlog-server-%{version}/debug/aws-sdk-cpp-install-:' ../percona-binlog-server-%{version}/CMakePresets.json
  cd debug
  # Build Boost Libraries
  git clone --recurse-submodules -b boost-%{BOOST_VERSION} --jobs=8 https://github.com/boostorg/boost.git boost
  cd boost
  git switch -c required_release
  cd ..
  cp -v ../../percona-binlog-server-%{version}/extra/cmake_presets/boost/CMakePresets.json boost

  cmake ./boost \
    --preset %{BUILD_PRESET_DEBUG}
  cmake --build ./boost-build-%{BUILD_PRESET_DEBUG} --parallel
  cmake --install ./boost-build-%{BUILD_PRESET_DEBUG}

  # Build AWS SDK for C++ Libraries
  git clone --recurse-submodules --jobs=8 https://github.com/aws/aws-sdk-cpp.git aws-sdk-cpp
  cd aws-sdk-cpp
  git checkout --recurse-submodules -b required_release %{AWS_VERSION}
  cd ..
  cp -v ../../percona-binlog-server-%{version}/extra/cmake_presets/aws-sdk-cpp/CMakePresets.json aws-sdk-cpp
  cmake ./aws-sdk-cpp --preset %{BUILD_PRESET_DEBUG}
  cmake --build ./aws-sdk-cpp-build-%{BUILD_PRESET_DEBUG} --parallel
  cmake --install ./aws-sdk-cpp-build-%{BUILD_PRESET_DEBUG}

  # Build Percona Binlog Server
  cd ../..
  cmake ./percona-binlog-server-%{version} --preset %{BUILD_PRESET_DEBUG}
  cmake --build ./percona-binlog-server-%{version}-build-%{BUILD_PRESET_DEBUG} --parallel
)

# Build Release version
mkdir -p release
(
%if 0%{?amzn}
  echo "Running Amazon Linux–specific command"
  export CFLAGS="${CFLAGS//-Werror*/}"
  export CXXFLAGS="${CXXFLAGS//-Werror*/}"
  export LDFLAGS="${LDFLAGS//-Werror*/}"
  export CFLAGS="${CFLAGS//-specs*annobin*/}"
  export CXXFLAGS="${CXXFLAGS//-specs*annobin*/}"
  export LDFLAGS="${LDFLAGS//-specs*annobin*/}"
  export CFLAGS="${CFLAGS//-specs*redhat-hardened-cc1*/}"
  export CXXFLAGS="${CXXFLAGS//-specs*redhat-hardened-cc1*/}"
  export LDFLAGS="${LDFLAGS//-specs*redhat-hardened-ld*/}"
%endif
  sed -i 's:debug/boost-install-:release/boost-install-:' ../percona-binlog-server-%{version}/CMakePresets.json
  sed -i 's:debug/aws-sdk-cpp-install-:release/aws-sdk-cpp-install-:' ../percona-binlog-server-%{version}/CMakePresets.json
  cd release
  # Build Boost Libraries
  git clone --recurse-submodules -b boost-%{BOOST_VERSION} --jobs=8 https://github.com/boostorg/boost.git boost
  cd boost
  git switch -c required_release
  cd ..
  cp -v ../../percona-binlog-server-%{version}/extra/cmake_presets/boost/CMakePresets.json boost

  cmake ./boost \
    --preset %{BUILD_PRESET_RELEASE}
  cmake --build ./boost-build-%{BUILD_PRESET_RELEASE} --parallel
  cmake --install ./boost-build-%{BUILD_PRESET_RELEASE}

  # Build AWS SDK for C++ Libraries
  git clone --recurse-submodules --jobs=8 https://github.com/aws/aws-sdk-cpp.git aws-sdk-cpp
  cd aws-sdk-cpp
  git checkout --recurse-submodules -b required_release %{AWS_VERSION}
  cd ..
  cp -v ../../percona-binlog-server-%{version}/extra/cmake_presets/aws-sdk-cpp/CMakePresets.json aws-sdk-cpp
  cmake ./aws-sdk-cpp --preset %{BUILD_PRESET_RELEASE}
  cmake --build ./aws-sdk-cpp-build-%{BUILD_PRESET_RELEASE} --parallel
  cmake --install ./aws-sdk-cpp-build-%{BUILD_PRESET_RELEASE}

  # Build Percona Binlog Server
  cd ../..
  cmake ./percona-binlog-server-%{version} --preset %{BUILD_PRESET_RELEASE}
  cmake --build ./percona-binlog-server-%{version}-build-%{BUILD_PRESET_RELEASE} --parallel
)

%install
install -d %{buildroot}/usr/bin
install -m 755 ../percona-binlog-server-%{version}-build-%{BUILD_PRESET_DEBUG}/binlog_server %{buildroot}/usr/bin/binlog_server-debug
install -m 755 ../percona-binlog-server-%{version}-build-%{BUILD_PRESET_RELEASE}/binlog_server %{buildroot}/usr/bin/binlog_server
install -m 0755 -d %{buildroot}/%{_sysconfdir}
install -D -m 0644  main_config.json %{buildroot}/%{_sysconfdir}/percona-binlog-server/main_config.json


%files
%license LICENSE
%doc README.md
%config(noreplace) %attr(0640,root,root) /%{_sysconfdir}/percona-binlog-server/main_config.json
/usr/bin/binlog_server-debug
/usr/bin/binlog_server


%changelog
* Mon Mar 16 2026 Yura Sorokin <yura.sorokin@percona.com> - 0.2.0-1
- Percona Binlog Server with GTID replication support.

* Fri Jan 16 2026 Vadim Yalovets <vadim.yalovets@percona.com> - 0.1.0-2
- PKG-1208 Prepare packages for Percona Binlog Server.

* Mon Aug 26 2024 Surabhi Bhat <surabhi.bhat@percona.com> - 0.1.0-1
- Initial package with separate builds for Debug and RelWithDebInfo versions.

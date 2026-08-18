%global percona_binlog_server_version @@PBS_RELEASE@@
%global rpm_release @@RPM_RELEASE@@
%global build_preset_debug debug_gcc14
%global build_preset_release release_gcc14
%global boost_version 1.90.0
%global aws_sdk_version 1.11.774
%global release %{rpm_release}%{?dist}
%global optflags %(echo %{optflags} | sed 's/-specs=[^ ]*annobin[^ ]*//g')

%global boost_src %{_builddir}/boost-%{boost_version}
%global aws_src %{_builddir}/aws-sdk-cpp-%{aws_sdk_version}
%global app_src %{_builddir}/%{name}-%{version}
%global boost_install %{_builddir}/boost-%{boost_version}-install-%{build_preset_release}
%global aws_install %{_builddir}/aws-sdk-cpp-%{aws_sdk_version}-install-%{build_preset_release}

Name:           percona-binlog-server
Version:        %{percona_binlog_server_version}
Release:        %{release}
Summary:        Percona Binary Log Server

License:        GPLv2
URL:            https://github.com/Percona-Lab/percona-binlog-server
Source0:        %{name}-%{version}.tar.gz
Source1:        boost-%{boost_version}.tar.gz
Source2:        aws-sdk-cpp-%{aws_sdk_version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  make
BuildRequires:  libcurl-devel
BuildRequires:  zlib-devel
BuildRequires:  percona-server-devel
%if 0%{?amzn}
BuildRequires:  gcc14
BuildRequires:  gcc14-c++
%else
BuildRequires:  openssl-devel
%if 0%{?rhel} && 0%{?rhel} < 10
BuildRequires:  gcc-toolset-14-gcc
BuildRequires:  gcc-toolset-14-gcc-c++
BuildRequires:  gcc-toolset-14-binutils
%else
BuildRequires:  gcc
BuildRequires:  gcc-c++
%endif
%endif

%description
Percona Binary Log Server is a command-line utility that acts as an enhanced version of mysqlbinlog in --read-from-remote-server mode. It serves as a replication client and can stream binary log events from a remote Oracle MySQL Server / Percona Server for MySQL both to a local filesystem and to a cloud storage (currently AWS S3). The tool is capable of automatically reconnecting to the remote server and resuming operations from the point where it was previously stopped.

%prep
%setup -q
tar -xf %{SOURCE1} -C %{_builddir}
tar -xf %{SOURCE2} -C %{_builddir}
cp -v extra/cmake_presets/boost/CMakePresets.json %{boost_src}/
cp -v extra/cmake_presets/aws-sdk-cpp/CMakePresets.json %{aws_src}/

%build
%if 0%{?amzn}
echo "Running Amazon Linux-specific command"
export CFLAGS="${CFLAGS//-Werror*/}"
export CXXFLAGS="${CXXFLAGS//-Werror*/}"
export LDFLAGS="${LDFLAGS//-Werror*/}"
export CFLAGS="${CFLAGS//-specs*annobin*/}"
export CXXFLAGS="${CXXFLAGS//-specs*annobin*/}"
export LDFLAGS="${LDFLAGS//-specs*annobin*/}"
export CFLAGS="${CFLAGS//-specs*redhat-hardened-cc1*/}"
export CXXFLAGS="${CXXFLAGS//-specs*redhat-hardened-cc1*/}"
export LDFLAGS="${LDFLAGS//-specs*redhat-hardened-ld*/}"
compiler_args="-DCMAKE_C_COMPILER=gcc14-gcc -DCMAKE_CXX_COMPILER=gcc14-g++"
%else
%if 0%{?rhel} && 0%{?rhel} < 10
. /opt/rh/gcc-toolset-14/enable
%endif
compiler_args="-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++"
%endif

build_jobs=%{?_smp_build_ncpus}%{!?_smp_build_ncpus:$(nproc)}

build_dependency() {
  local src="$1"
  local build_dir="$2"
  cmake "${src}" --preset %{build_preset_release} ${compiler_args}
  cmake --build "${build_dir}" --parallel "${build_jobs}"
  cmake --install "${build_dir}"
}

build_dependency %{boost_src} \
  %{_builddir}/boost-%{boost_version}-build-%{build_preset_release}
build_dependency %{aws_src} \
  %{_builddir}/aws-sdk-cpp-%{aws_sdk_version}-build-%{build_preset_release}

for preset in %{build_preset_debug} %{build_preset_release}; do
  testing=ON
  if [ "${preset}" = "%{build_preset_debug}" ]; then
    testing=OFF
  fi
  cmake %{app_src} --preset "${preset}" ${compiler_args} \
    -DCMAKE_PREFIX_PATH="%{aws_install};%{boost_install}" \
    -DBUILD_TESTING="${testing}"
  cmake --build %{_builddir}/%{name}-%{version}-build-"${preset}" \
    --parallel "${build_jobs}"
done

%check
cd %{_builddir}/%{name}-%{version}-build-%{build_preset_release}
./binlog_server version
ctest --output-on-failure

%install
install -Dpm 0755 \
  %{_builddir}/%{name}-%{version}-build-%{build_preset_release}/binlog_server \
  %{buildroot}%{_bindir}/binlog_server
install -Dpm 0755 \
  %{_builddir}/%{name}-%{version}-build-%{build_preset_debug}/binlog_server \
  %{buildroot}%{_bindir}/binlog_server-debug
install -Dpm 0640 main_config.json \
  %{buildroot}%{_sysconfdir}/%{name}/main_config.json

%files
%license LICENSE
%doc README.md
%dir %{_sysconfdir}/%{name}
%config(noreplace) %attr(0640,root,root) %{_sysconfdir}/%{name}/main_config.json
%{_bindir}/binlog_server
%{_bindir}/binlog_server-debug

%changelog
* Thu Jun 11 2026 Yura Sorokin <yura.sorokin@percona.com> - 0.3.1-1
- PBS-27 binlog_server list command fails with ERROR if executed while fetch/pull is in progress.

* Thu Jun 04 2026 Yura Sorokin <yura.sorokin@percona.com> - 0.3.0-1
- PS-10625 Update boost libraries to 1.90.0.
- PS-10934 Update AWS SDK C++ libraries to version 1.11.774.
- PS-11001 Binlog clean up (purge_binlogs command).

* Wed Jun 03 2026 Yura Sorokin <yura.sorokin@percona.com> - 0.2.4-1
- PS-11137 binlog-server fails to reconnect in position-based replication mode when network timeout interrupts a transaction.
- PS-11136 non-GTID transactions cause one storage flush per transaction, bypassing size/interval checkpointing.
- PS-11080 Spoiled logical clock information in rewritten binlog.
- PS-11205 When fs_buffer_directory is not set the temporary buffer directory is created in the current binary path rather than in /tmp.

* Fri Apr 24 2026 Yura Sorokin <yura.sorokin@percona.com> - 0.2.3-1
- PS-11033 Crash when S3 bucket accumulates large number of objects; recovery requires manual intervention.

* Tue Apr 21 2026 Yura Sorokin <yura.sorokin@percona.com> - 0.2.2-1
- PS-11054 Cannot replicate because the source purged required binary logs.

* Fri Apr 10 2026 Yura Sorokin <yura.sorokin@percona.com> - 0.2.1-1
- PS-10910 Bucket name is missing in search_* outputs.
- PS-11002 Changing storage prefix fails.
- PS-10911 Unexpected binlog position in artificial rotate event.

* Mon Mar 16 2026 Yura Sorokin <yura.sorokin@percona.com> - 0.2.0-1
- Percona Binlog Server with GTID replication support.

* Fri Jan 16 2026 Vadim Yalovets <vadim.yalovets@percona.com> - 0.1.0-2
- PKG-1208 Prepare packages for Percona Binlog Server.

* Mon Aug 26 2024 Surabhi Bhat <surabhi.bhat@percona.com> - 0.1.0-1
- Initial package with separate builds for Debug and RelWithDebInfo versions.

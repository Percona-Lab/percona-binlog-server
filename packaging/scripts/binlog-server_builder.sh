#!/bin/sh

set -eu

PRODUCT=percona-binlog-server
PROPERTIES=percona-binlog-server.properties
BOOST_UPSTREAM=https://github.com/boostorg/boost.git
AWS_SDK_UPSTREAM=https://github.com/aws/aws-sdk-cpp.git

CURDIR=$(pwd)
WORKDIR=
REPO=https://github.com/Percona-Lab/percona-binlog-server
BRANCH=main
VERSION=1.0.0
RPM_RELEASE=1
DEB_RELEASE=1
INSTALL=0
SOURCE=0
SRPM=0
SDEB=0
RPM=0
DEB=0

OS=
OS_NAME=
RHEL=
ARCH=

msg() {
    echo "$*"
}

die() {
    echo "$*" >&2
    exit 1
}

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]
    The following options may be given :
        --builddir=DIR      Absolute path to the dir where all actions will be performed
        --get_sources=1     Source will be downloaded from github
        --build_src_rpm=1   If it is set - src rpm will be built
        --build_src_deb=1   If it is set - source deb package will be built
        --build_rpm=1       If it is set - rpm will be built
        --build_deb=1       If it is set - deb will be built
        --install_deps=1    Install build dependencies (root privileges are required)
        --branch=NAME       Branch for build
        --repo=URL          Repo for build
        --version=X.Y.Z     Version to build
        --rpm_release=N     RPM release ( default = 1 )
        --deb_release=N     DEB release ( default = 1 )
        --help              Show this help

Example $0 --builddir=/tmp/percona-binlog-server --get_sources=1 --build_src_rpm=1 --build_rpm=1
EOF
    exit 1
}

parse_arguments() {
    for arg; do
        val=${arg#*=}
        case "${arg}" in
        --builddir=*) WORKDIR="${val}" ;;
        --build_src_rpm=*) SRPM="${val}" ;;
        --build_src_deb=*) SDEB="${val}" ;;
        --build_rpm=*) RPM="${val}" ;;
        --build_deb=*) DEB="${val}" ;;
        --get_sources=*) SOURCE="${val}" ;;
        --install_deps=*) INSTALL="${val}" ;;
        --branch=*) BRANCH="${val}" ;;
        --repo=*) REPO="${val}" ;;
        --version=*) VERSION="${val}" ;;
        --rpm_release=*) RPM_RELEASE="${val}" ;;
        --deb_release=*) DEB_RELEASE="${val}" ;;
        --help) usage ;;
        *) die "Unknown option: ${arg}" ;;
        esac
    done
}

require_workdir() {
    [ -n "${WORKDIR}" ] || die "No build directory given, use --builddir=DIR"
    [ "${WORKDIR}" != "${CURDIR}" ] || die "Current directory cannot be used for building!"
    [ -d "${WORKDIR}" ] || die "${WORKDIR} is not a directory."
}

find_artifact() {
    artifact_dir=$1
    artifact_pattern=$2
    for base in "${WORKDIR}" "${CURDIR}"; do
        found=$(find "${base}/${artifact_dir}" -maxdepth 1 -name "${artifact_pattern}" 2>/dev/null | sort | tail -n1)
        if [ -n "${found}" ]; then
            echo "${found}"
            return 0
        fi
    done
    return 1
}

fetch_artifact() {
    artifact_dir=$1
    artifact_pattern=$2
    artifact_hint=$3
    artifact_path=$(find_artifact "${artifact_dir}" "${artifact_pattern}") ||
        die "There is no ${artifact_pattern} in ${artifact_dir}. ${artifact_hint}"
    cp "${artifact_path}" "${WORKDIR}/"
    basename "${artifact_path}"
}

publish() {
    publish_dir=$1
    shift
    mkdir -p "${WORKDIR}/${publish_dir}" "${CURDIR}/${publish_dir}"
    cp "$@" "${WORKDIR}/${publish_dir}/"
    cp "$@" "${CURDIR}/${publish_dir}/"
    ls -la "${WORKDIR}/${publish_dir}"
}

append_property() {
    echo "$1" >>"${WORKDIR}/${PROPERTIES}"
}

detect_system() {
    if [ -f /etc/redhat-release ]; then
        RHEL=$(rpm --eval %rhel)
        OS_NAME="el${RHEL}"
        OS=rpm
    elif [ -f /etc/amazon-linux-release ]; then
        RHEL=$(rpm --eval %amzn)
        OS_NAME="amzn${RHEL}"
        OS=rpm
    else
        OS_NAME=$(. /etc/os-release && echo "${VERSION_CODENAME}")
        OS=deb
    fi
    ARCH=$(uname -m | sed -e 's:i686:i386:g')
}

require_os() {
    [ "${OS}" = "$1" ] || die "$2 cannot be built on this system"
}

install_dependencies() {
    if [ "${INSTALL}" = 0 ]; then
        msg "Dependencies will not be installed"
        return 0
    fi
    [ "$(id -u)" -eq 0 ] || die "Dependencies can only be installed as root"

    if [ "${OS}" = rpm ]; then
        enable_repos_rpm
        install_toolchain_rpm
        install_build_deps_rpm
    else
        enable_repos_deb
        install_build_deps_deb
        install_toolchain_deb
    fi
}

enable_repos_rpm() {
    yum clean all
    if [ "${RHEL}" != 2023 ]; then
        if [ "${RHEL}" = 10 ]; then
            dnf -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-10.noarch.rpm
            /usr/bin/crb enable
        else
            yum -y install epel-release
        fi
    fi
    yum -y install https://repo.percona.com/yum/percona-release-latest.noarch.rpm
    if [ "${RHEL}" = 8 ]; then
        dnf module disable mysql -y
    fi
    percona-release enable ps-84-lts release
}

register_gcc14_rpm() {
    alternatives --install /usr/bin/gcc-14 gcc-14 "$1" 140
    alternatives --install /usr/bin/g++-14 g++-14 "$2" 140
}

install_toolchain_rpm() {
    yum -y install gcc gcc-c++ libatomic
    if [ "${RHEL}" = 2023 ]; then
        yum -y install gcc14 gcc14-c++
        register_gcc14_rpm /usr/bin/gcc14-gcc /usr/bin/gcc14-g++
    elif [ "${RHEL}" = 8 ] || [ "${RHEL}" = 9 ]; then
        yum -y install gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils
        register_gcc14_rpm /opt/rh/gcc-toolset-14/root/usr/bin/gcc \
            /opt/rh/gcc-toolset-14/root/usr/bin/g++
    else
        register_gcc14_rpm /usr/bin/gcc /usr/bin/g++
    fi
}

install_build_deps_rpm() {
    yum -y install git wget rpm-build make rpmdevtools cmake libcurl-devel zlib-devel
    if [ "${RHEL}" != 10 ]; then
        yum -y install rpmlint || true
    fi
    if [ "${RHEL}" != 2023 ]; then
        yum -y install curl openssl-devel
    fi
    yum -y install percona-server-devel
}

enable_repos_deb() {
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get -y install --no-install-recommends \
        ca-certificates curl wget gnupg2 gpgv lsb-release apt-transport-https
    percona_release_deb="percona-release_latest.$(lsb_release -sc)_all.deb"
    wget "https://repo.percona.com/apt/${percona_release_deb}"
    dpkg -i "${percona_release_deb}"
    rm -f "${percona_release_deb}"
    percona-release enable pdps-84-lts release
    apt-get update
}

register_gcc_deb() {
    gcc_version=$1
    update-alternatives --install /usr/bin/gcc gcc "/usr/bin/gcc-${gcc_version}" 100 \
        --slave /usr/bin/g++ g++ "/usr/bin/g++-${gcc_version}"
    update-alternatives --install /usr/bin/cc cc "/usr/bin/gcc-${gcc_version}" 100
}

install_toolchain_deb() {
    case "${OS_NAME}" in
    bullseye)
        install_cmake_bullseye
        apt-get -y install gcc-10 g++-10
        register_gcc_deb 10
        ;;
    jammy)
        apt-get -y install software-properties-common
        add-apt-repository ppa:ubuntu-toolchain-r/test -y
        apt-get -y install gcc-13 g++-13
        register_gcc_deb 13
        ;;
    bookworm)
        echo "deb http://deb.debian.org/debian testing main" >/etc/apt/sources.list.d/testing.list
        apt-get update
        apt-get -y install gcc-13 g++-13
        register_gcc_deb 13
        ;;
    *)
        apt-get -y install gcc-14 g++-14
        register_gcc_deb 14
        ;;
    esac
}

install_cmake_bullseye() {
    cmake_version=3.21.7
    cmake_dir="cmake-${cmake_version}-linux-x86_64"
    apt-get -y install ca-certificates xz-utils
    wget "https://github.com/Kitware/CMake/releases/download/v${cmake_version}/${cmake_dir}.tar.gz"
    tar -xf "${cmake_dir}.tar.gz"
    rm -f "${cmake_dir}.tar.gz"
    mv "${cmake_dir}" "/opt/cmake-${cmake_version}"
    rm -f /usr/bin/cmake
    update-alternatives --install /usr/bin/cmake cmake "/opt/cmake-${cmake_version}/bin/cmake" 210
}

install_build_deps_deb() {
    apt-get -y install cmake build-essential devscripts debconf debhelper fakeroot git \
        zlib1g-dev libcurl4-openssl-dev libssl-dev libperconaserverclient24-dev
    if [ "${OS_NAME}" = trixie ]; then
        apt-get -y install libzstd-dev
    fi
}

fetch_product_sources() {
    if [ "${SOURCE}" = 0 ]; then
        msg "Sources will not be downloaded"
        return 0
    fi
    cd "${WORKDIR}"

    {
        echo "PRODUCT=${PRODUCT}"
        echo "BUILD_NUMBER=${BUILD_NUMBER:-}"
        echo "BUILD_ID=${BUILD_ID:-}"
        echo "VERSION=${VERSION}"
        echo "BRANCH=${BRANCH}"
        echo "RPM_RELEASE=${RPM_RELEASE}"
        echo "DEB_RELEASE=${DEB_RELEASE}"
    } >"${WORKDIR}/${PROPERTIES}"

    rm -rf "${PRODUCT}" "${PRODUCT}-${VERSION}"
    git clone "${REPO}" "${PRODUCT}" ||
        die "There were some issues during repo cloning from github. Please retry one more time"
    cd "${PRODUCT}"
    if [ -n "${BRANCH}" ]; then
        git reset --hard
        git clean -xdf
        git checkout "${BRANCH}"
    fi

    sed -i "s:@@PBS_RELEASE@@:${VERSION}:g" packaging/rpm/binlog-server.spec
    sed -i "s:@@RPM_RELEASE@@:${RPM_RELEASE}:g" packaging/rpm/binlog-server.spec

    revision=$(git rev-parse --short HEAD)
    {
        echo "VERSION=${VERSION}"
        echo "REVISION=${revision}"
        echo "GITCOMMIT=$(git rev-parse HEAD)"
        echo "GITBRANCH=$(git rev-parse --abbrev-ref HEAD)"
    } >VERSION

    cd "${WORKDIR}"
    append_property "REVISION=${revision}"
    append_property "UPLOAD=UPLOAD/experimental/BUILDS/${PRODUCT}/${PRODUCT}-${VERSION}/${BRANCH}/${revision}/${BUILD_ID:-}"

    mv "${PRODUCT}" "${PRODUCT}-${VERSION}"
    tar --owner=0 --group=0 -czf "${PRODUCT}-${VERSION}.tar.gz" "${PRODUCT}-${VERSION}"
    publish source_tarball "${PRODUCT}-${VERSION}.tar.gz"

    fetch_dependency_sources "${WORKDIR}/${PRODUCT}-${VERSION}/packaging/rpm/binlog-server.spec"
    cd "${CURDIR}"
}

fetch_dependency_sources() {
    spec_file=$1

    boost_version=$(awk '/^%global boost_version/ {print $3; exit}' "${spec_file}")
    aws_version=$(awk '/^%global aws_sdk_version/ {print $3; exit}' "${spec_file}")
    [ -n "${boost_version}" ] && [ -n "${aws_version}" ] ||
        die "Cannot determine Boost / AWS SDK versions from ${spec_file}"

    boost_dir="boost-${boost_version}"
    aws_dir="aws-sdk-cpp-${aws_version}"

    cd "${WORKDIR}"
    rm -rf "${boost_dir}" "${aws_dir}"

    git clone --depth 1 --recurse-submodules --shallow-submodules \
        -b "boost-${boost_version}" "${BOOST_UPSTREAM}" "${boost_dir}"
    git clone --depth 1 --recurse-submodules --shallow-submodules \
        -b "${aws_version}" "${AWS_SDK_UPSTREAM}" "${aws_dir}"

    find "${boost_dir}" "${aws_dir}" -name .git -exec rm -rf {} + 2>/dev/null || true
    prune_boost "${boost_dir}"
    prune_aws_sdk "${aws_dir}"

    tar --owner=0 --group=0 -czf "${boost_dir}.tar.gz" "${boost_dir}"
    tar --owner=0 --group=0 -czf "${aws_dir}.tar.gz" "${aws_dir}"
    rm -rf "${boost_dir}" "${aws_dir}"

    publish source_tarball "${boost_dir}.tar.gz" "${aws_dir}.tar.gz"
}

prune_boost() {
    boost_tree=$1
    find "${boost_tree}/libs" -mindepth 2 -maxdepth 3 -type d \
        \( -name test -o -name doc -o -name example -o -name examples \) \
        -exec rm -rf {} + 2>/dev/null || true
    rm -rf "${boost_tree}/doc"
}

prune_aws_sdk() {
    aws_tree=$1
    find "${aws_tree}/generated/src" -mindepth 1 -maxdepth 1 -type d \
        ! -name 'aws-cpp-sdk-s3-crt' -exec rm -rf {} + 2>/dev/null || true
    rm -rf "${aws_tree}/tests" "${aws_tree}/code-generation" \
        "${aws_tree}/tools/code-generation" "${aws_tree}/docs"
    find "${aws_tree}/crt" -mindepth 2 -maxdepth 5 -type d \
        \( -name tests -o -name test -o -name docs \) \
        -exec rm -rf {} + 2>/dev/null || true
}

fetch_source_tarballs() {
    product_tarball=$(fetch_artifact source_tarball "${PRODUCT}-*.tar.gz" \
        "You can create it using key --get_sources=1")
    boost_tarball=$(fetch_artifact source_tarball 'boost-*.tar.gz' \
        "You can create it using key --get_sources=1")
    aws_tarball=$(fetch_artifact source_tarball 'aws-sdk-cpp-*.tar.gz' \
        "You can create it using key --get_sources=1")
}

build_srpm() {
    if [ "${SRPM}" = 0 ]; then
        msg "SRC RPM will not be created"
        return 0
    fi
    require_os rpm "Source RPM"

    cd "${WORKDIR}"
    fetch_source_tarballs
    rm -rf rpmbuild packaging VERSION
    mkdir -vp rpmbuild/SOURCES rpmbuild/SPECS rpmbuild/BUILD rpmbuild/SRPMS rpmbuild/RPMS

    tar xzf "${product_tarball}" --wildcards '*/packaging' --strip=1
    tar xzf "${product_tarball}" --wildcards '*/VERSION' --strip=1
    . ./VERSION

    sed -e "s:@@PBS_RELEASE@@:${VERSION}:g" \
        -e "s:@@RPM_RELEASE@@:${RPM_RELEASE}:g" \
        packaging/rpm/binlog-server.spec >rpmbuild/SPECS/binlog-server.spec

    mv -f "${product_tarball}" "${boost_tarball}" "${aws_tarball}" rpmbuild/SOURCES/
    rpmbuild -bs \
        --define "_topdir ${WORKDIR}/rpmbuild" \
        --define "version ${VERSION}" \
        --define "dist .generic" \
        rpmbuild/SPECS/binlog-server.spec

    publish srpm rpmbuild/SRPMS/*.src.rpm
}

build_rpm() {
    if [ "${RPM}" = 0 ]; then
        msg "RPM will not be created"
        return 0
    fi
    require_os rpm "RPM"

    cd "${WORKDIR}"
    src_rpm=$(fetch_artifact srpm "${PRODUCT}-*.src.rpm" \
        "You can create it using key --build_src_rpm=1")

    rm -rf rpmbuild
    mkdir -vp rpmbuild/SOURCES rpmbuild/SPECS rpmbuild/BUILD rpmbuild/SRPMS rpmbuild/RPMS
    cp "${src_rpm}" rpmbuild/SRPMS/

    append_property "RHEL=${RHEL}"
    append_property "ARCH=${ARCH}"

    rpmbuild \
        --define "_topdir ${WORKDIR}/rpmbuild" \
        --define "dist .${OS_NAME}" \
        --rebuild "rpmbuild/SRPMS/${src_rpm}"

    publish rpm rpmbuild/RPMS/*/*.rpm
}

build_sdeb() {
    if [ "${SDEB}" = 0 ]; then
        msg "Source DEB will not be created"
        return 0
    fi
    require_os deb "Source DEB"

    cd "${WORKDIR}"
    rm -rf "${PRODUCT}"-*
    rm -f ./*.dsc ./*.orig.tar.gz ./*.orig-*.tar.gz ./*.debian.tar.xz ./*.changes
    fetch_source_tarballs

    VERSION=$(echo "${product_tarball}" | sed -e "s:^${PRODUCT}-::" -e 's:\.tar\.gz$::')
    [ -n "${VERSION}" ] || die "Cannot determine version from ${product_tarball}"

    mv "${product_tarball}" "${PRODUCT}_${VERSION}.orig.tar.gz"
    cp "${boost_tarball}" "${PRODUCT}_${VERSION}.orig-boost.tar.gz"
    cp "${aws_tarball}" "${PRODUCT}_${VERSION}.orig-aws-sdk-cpp.tar.gz"

    tar xzf "${PRODUCT}_${VERSION}.orig.tar.gz"
    cd "${PRODUCT}-${VERSION}"
    for component in boost aws-sdk-cpp; do
        mkdir -p "${component}"
        tar xzf "../${PRODUCT}_${VERSION}.orig-${component}.tar.gz" \
            -C "${component}" --strip-components=1
    done

    cp -a packaging/debian .
    export DEBEMAIL=support@percona.com
    export DEBFULLNAME=Percona
    dch -D unstable --force-distribution -v "${VERSION}-${DEB_RELEASE}" \
        "Update to new upstream release Percona Binlog Server ${VERSION}-${DEB_RELEASE}"
    dpkg-buildpackage -S

    cd "${WORKDIR}"
    publish source_deb ./*.dsc ./*.orig.tar.gz ./*.orig-*.tar.gz \
        ./*.debian.tar.xz ./*_source.changes
}

build_deb() {
    if [ "${DEB}" = 0 ]; then
        msg "DEB will not be created"
        return 0
    fi
    require_os deb "DEB"

    cd "${WORKDIR}"
    for pattern in '*.dsc' '*.orig.tar.gz' '*.orig-boost.tar.gz' \
        '*.orig-aws-sdk-cpp.tar.gz' '*.debian.tar.xz' '*_source.changes'; do
        fetch_artifact source_deb "${PRODUCT}${pattern}" \
            "You can create it using key --build_src_deb=1" >/dev/null
    done

    dsc=$(find . -maxdepth 1 -name '*.dsc' | sort | tail -n1)
    VERSION=$(awk '/^Version:/ {sub(/-[^-]*$/, "", $2); print $2; exit}' "${dsc}")
    [ -n "${VERSION}" ] || die "Cannot determine version from ${dsc}"

    append_property "DEBIAN=${OS_NAME}"
    append_property "ARCH=${ARCH}"

    rm -rf "${PRODUCT}-${VERSION}"
    dpkg-source -x "${dsc}"
    cd "${PRODUCT}-${VERSION}"
    dch -m -D "${OS_NAME}" --force-distribution \
        -v "${VERSION}-${DEB_RELEASE}.${OS_NAME}" 'Update distribution'
    dpkg-buildpackage -rfakeroot -us -uc -b

    cd "${WORKDIR}"
    publish deb ./*.*deb
}

main() {
    parse_arguments "$@"
    require_workdir
    detect_system
    install_dependencies
    fetch_product_sources
    build_srpm
    build_sdeb
    build_rpm
    build_deb
}

main "$@"

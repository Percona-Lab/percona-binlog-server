#!/bin/sh

shell_quote_string() {
    echo "$1" | sed -e 's,\([^a-zA-Z0-9/_.=-]\),\\\1,g'
}

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]
    The following options may be given :
        --builddir=DIR      Absolute path to the dir where all actions will be performed
        --get_sources       Source will be downloaded from github
        --build_src_rpm     If it is set - src rpm will be built
        --build_src_deb     If it is set - source deb package will be built
        --build_rpm         If it is set - rpm will be built
        --build_deb         If it is set - deb will be built
        --install_deps      Install build dependencies(root privilages are required)
        --branch            Branch for build
        --repo              Repo for build
        --version           Version to build
        --rpm_release       RPM version( default = 1)
        --deb_release       DEB version( default = 1)
        --help) usage ;;
Example $0 --builddir=/tmp/percona-binlog-server --get_sources=1 --build_src_rpm=1 --build_rpm=1
EOF
    exit 1
}

append_arg_to_args() {
    args="$args "$(shell_quote_string "$1")
}

parse_arguments() {
    pick_args=
    if test "$1" = PICK-ARGS-FROM-ARGV; then
        pick_args=1
        shift
    fi

    for arg; do
        val=$(echo "$arg" | sed -e 's;^--[^=]*=;;')
        case "$arg" in
        --builddir=*) WORKDIR="$val" ;;
        --build_src_rpm=*) SRPM="$val" ;;
        --build_src_deb=*) SDEB="$val" ;;
        --build_rpm=*) RPM="$val" ;;
        --build_deb=*) DEB="$val" ;;
        --get_sources=*) SOURCE="$val" ;;
        --branch=*) BRANCH="$val" ;;
        --repo=*) REPO="$val" ;;
        --version=*) VERSION="$val" ;;
        --install_deps=*) INSTALL="$val" ;;
        --rpm_release=*) RPM_RELEASE="$val" ;;
        --deb_release=*) DEB_RELEASE="$val" ;;
        --help) usage ;;
        *)
            if test -n "$pick_args"; then
                append_arg_to_args "$arg"
            fi
            ;;
        esac
    done
}

check_workdir() {
    if [ "x$WORKDIR" = "x$CURDIR" ]; then
        echo >&2 "Current directory cannot be used for building!"
        exit 1
    else
        if ! test -d "$WORKDIR"; then
            echo >&2 "$WORKDIR is not a directory."
            exit 1
        fi
    fi
    return
}

get_sources() {
    cd "${WORKDIR}"
    if [ "${SOURCE}" = 0 ]; then
        echo "Sources will not be downloaded"
        return 0
    fi
    PRODUCT=percona-binlog-server
    PRODUCT_FULL=${PRODUCT}-${VERSION}
    echo "PRODUCT=${PRODUCT}" >percona-binlog-server.properties
    echo "BUILD_NUMBER=${BUILD_NUMBER}" >>percona-binlog-server.properties
    echo "BUILD_ID=${BUILD_ID}" >>percona-binlog-server.properties
    echo "VERSION=${VERSION}" >>percona-binlog-server.properties
    echo "BRANCH=${BRANCH}" >>percona-binlog-server.properties
    echo "RPM_RELEASE=${RPM_RELEASE}" >>percona-binlog-server.properties
    echo "DEB_RELEASE=${DEB_RELEASE}" >>percona-binlog-server.properties
    git clone "$REPO" ${PRODUCT}
    retval=$?
    if [ $retval != 0 ]; then
        echo "There were some issues during repo cloning from github. Please retry one more time"
        exit 1
    fi
    cd percona-binlog-server
    if [ ! -z "$BRANCH" ]; then
        git reset --hard
        git clean -xdf
        git checkout "$BRANCH"
    fi
    sed -i "s:@@PBS_RELEASE@@:${VERSION}:g" packaging/rpm/binlog-server.spec
    sed -i "s:@@RPM_RELEASE@@:${RPM_RELEASE}:g" packaging/rpm/binlog-server.spec
    REVISION=$(git rev-parse --short HEAD)
    GITCOMMIT=$(git rev-parse HEAD 2>/dev/null)
    GITBRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
    echo "VERSION=${VERSION}" >VERSION
    echo "REVISION=${REVISION}" >>VERSION
    echo "GITCOMMIT=${GITCOMMIT}" >>VERSION
    echo "GITBRANCH=${GITBRANCH}" >>VERSION
    echo "REVISION=${REVISION}" >>${WORKDIR}/percona-binlog-server.properties
    rm -fr  rpm
    cd ${WORKDIR}

    mv percona-binlog-server ${PRODUCT}-${VERSION}
    tar --owner=0 --group=0 -czf ${PRODUCT}-${VERSION}.tar.gz ${PRODUCT}-${VERSION}
    echo "UPLOAD=UPLOAD/experimental/BUILDS/${PRODUCT}/${PRODUCT}-${VERSION}/${BRANCH}/${REVISION}/${BUILD_ID}" >>percona-binlog-server.properties
    mkdir $WORKDIR/source_tarball
    mkdir $CURDIR/source_tarball
    cp ${PRODUCT}-${VERSION}.tar.gz $WORKDIR/source_tarball
    cp ${PRODUCT}-${VERSION}.tar.gz $CURDIR/source_tarball
    cd $CURDIR
    rm -rf percona-binlog-server
    return
}

get_system() {
    if [ -f /etc/redhat-release ]; then
        GLIBC_VER_TMP="$(rpm glibc -qa --qf %{VERSION})"
        RHEL=$(rpm --eval %rhel)
        ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
        OS_NAME="el$RHEL"
        OS="rpm"
    elif [ -f /etc/amazon-linux-release ]; then
        GLIBC_VER_TMP="$(rpm glibc -qa --qf %{VERSION})"
        RHEL=$(rpm --eval %amzn)
        ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
        OS_NAME="amzn$RHEL"
        OS="rpm"
    else
        GLIBC_VER_TMP="$(dpkg-query -W -f='${Version}' libc6 | awk -F'-' '{print $1}')"
        ARCH=$(uname -m)
        OS_NAME="$( . /etc/os-release && echo $VERSION_CODENAME)"
        OS="deb"
    fi
    export GLIBC_VER=".glibc${GLIBC_VER_TMP}"
    return
}

install_gcc() {
    local GCC_VERSION="$1"
    if [[ -z "$GCC_VERSION" ]]; then
        local GCC_VERSION="14.3.0"
    fi
    pushd /tmp
    wget https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.gz
    tar -zxvf gcc-${GCC_VERSION}.tar.gz
    cd gcc-${GCC_VERSION}
    ./contrib/download_prerequisites
    mkdir build && cd build
    ../configure --prefix=/opt/gcc-${GCC_VERSION::2} --disable-multilib
    make -j$(nproc)
    make install
    export PATH=/opt/gcc-${GCC_VERSION::2}/bin:$PATH
    export LD_LIBRARY_PATH=/opt/gcc-${GCC_VERSION::2}/lib64:$LD_LIBRARY_PATH
    gcc --version
    alternatives --install /usr/bin/gcc-${GCC_VERSION::2} gcc-${GCC_VERSION::2} /opt/gcc-${GCC_VERSION::2}/bin/gcc 140
    alternatives --install /usr/bin/g++-${GCC_VERSION::2} g++-${GCC_VERSION::2} /opt/gcc-${GCC_VERSION::2}/bin/g++ 140
    popd
}

install_deps() {
    if [ $INSTALL = 0 ]; then
        echo "Dependencies will not be installed"
        return
    fi
    if [ ! $(id -u) -eq 0 ]; then
        echo "It is not possible to install dependencies. Please run as root"
        exit 1
    fi
    CURPLACE=$(pwd)

    if [ "x$OS" = "xrpm" ]; then
        yum clean all
        if [ "x${RHEL}" != "x2023" ]; then
            if [ "x${RHEL}" = "x10" ]; then
                dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-10.noarch.rpm
            else
                yum -y install epel-release
            fi
        fi
        yum -y install git wget
        yum -y install rpm-build make rpmlint rpmdevtools cmake3 libcurl-devel zlib-devel
        yum -y install gcc gcc-c++
        yum -y install libatomic
        if [ "x${RHEL}" != "x2023" ]; then
            yum -y install curl openssl-devel
            if [ "x$RHEL" = "x8" -o "x$RHEL" = "x9" ]; then
                yum -y install gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils
                source /opt/rh/gcc-toolset-14/enable
                alternatives --install /usr/bin/gcc-14 gcc-14 /opt/rh/gcc-toolset-14/root/usr/bin/gcc 140
                alternatives --install /usr/bin/g++-14 g++-14 /opt/rh/gcc-toolset-14/root/usr/bin/g++ 140
            else
                alternatives --install /usr/bin/gcc-14 gcc-14 /usr/bin/gcc 140
                alternatives --install /usr/bin/g++-14 g++-14 /usr/bin/g++ 140
            fi
        else
            yum -y install gcc14 gcc14-c++
            alternatives --install /usr/bin/gcc-14 gcc-14 /usr/bin/gcc14-gcc 140
            alternatives --install /usr/bin/g++-14 g++-14 /usr/bin/gcc14-g++ 140
            #yum -y install tar gzip bzip2 diffutils
            #install_gcc 14.3.0
        fi
        yum -y install https://repo.percona.com/yum/percona-release-latest.noarch.rpm
        if [ "x$RHEL" = "x8" ]; then
            dnf module disable mysql -y
        fi
        percona-release enable ps-84-lts release
        yum -y install percona-server-devel
    else
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get -y install curl lsb-release gnupg2 wget apt-transport-https
        wget https://repo.percona.com/apt/percona-release_latest.$(lsb_release -sc)_all.deb && dpkg -i percona-release_latest.$(lsb_release -sc)_all.deb
        export DEBIAN_FRONTEND="noninteractive"
        export DIST="$(lsb_release -sc)"
        percona-release enable pdps-84-lts release
        apt-get install -y cmake autotools-dev autoconf automake build-essential devscripts debconf debhelper fakeroot
        if [ "${OS_NAME}" == "bullseye" ]; then
            apt-get install -y ca-certificates xz-utils
            wget https://github.com/Kitware/CMake/releases/download/v3.21.7/cmake-3.21.7-linux-x86_64.tar.gz
            tar -xf cmake-3.21.7-linux-x86_64.tar.gz
            mv cmake-3.21.7-linux-x86_64 /opt/cmake-3.21.7
            export PATH=/opt/cmake-3.21.7/bin:$PATH
            cmake --version
            rm -f /usr/bin/cmake
            update-alternatives --install /usr/bin/cmake cmake /opt/cmake-3.21.7/bin/cmake 210
            update-alternatives --config cmake
            apt-get -y install gcc g++
            update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 --slave /usr/bin/g++ g++ /usr/bin/g++-10
            update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-10 100
        fi
        if [ "${OS_NAME}" == "jammy" ]; then
            apt-get -y install software-properties-common
            add-apt-repository ppa:ubuntu-toolchain-r/test -y
            apt-get -y install gcc-13 g++-13
            update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 --slave /usr/bin/g++ g++ /usr/bin/g++-13
            update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-13 100
        elif [ "${OS_NAME}" = "bookworm" ]; then
            apt remove -y lintian
            echo "deb http://deb.debian.org/debian testing main" > /etc/apt/sources.list.d/testing.list
            apt update
            apt-get -y install gcc-13 g++-13
            update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 --slave /usr/bin/g++ g++ /usr/bin/g++-13
            update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-13 100
        else
            apt-get -y install gcc-14 g++-14
            update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 --slave /usr/bin/g++ g++ /usr/bin/g++-14
            update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 100
        fi
        if [ "${OS_NAME}" = "trixie" ]; then
            apt install -y libzstd-dev
        fi
        apt install -y zlib1g-dev libcurl4-openssl-dev libssl-dev
        apt install -y libmysqlclient-dev
    fi
    return
}

get_tar() {
    TARBALL=$1
    TARFILE=$(basename $(find $WORKDIR/$TARBALL -name 'percona-binlog-server*.tar.gz' | sort | tail -n1))
    if [ -z $TARFILE ]; then
        TARFILE=$(basename $(find $CURDIR/$TARBALL -name 'percona-binlog-server*.tar.gz' | sort | tail -n1))
        if [ -z $TARFILE ]; then
            echo "There is no $TARBALL for build"
            exit 1
        else
            cp $CURDIR/$TARBALL/$TARFILE $WORKDIR/$TARFILE
        fi
    else
        cp $WORKDIR/$TARBALL/$TARFILE $WORKDIR/$TARFILE
    fi
    return
}

build_srpm() {
    if [ $SRPM = 0 ]; then
        echo "SRC RPM will not be created"
        return
    fi
    if [ "x$OS" = "xdeb" ]; then
        echo "It is not possible to build src rpm here"
        exit 1
    fi
    cd $WORKDIR
    get_tar "source_tarball"
    rm -fr rpmbuild
    ls | grep -v tar.gz | xargs rm -rf
    TARFILE=$(find . -name 'percona-binlog-server*.tar.gz' | sort | tail -n1)
    SRC_DIR=${TARFILE%.tar.gz}
    mkdir -vp rpmbuild/{SOURCES,SPECS,BUILD,SRPMS,RPMS}
    tar vxzf ${WORKDIR}/${TARFILE} --wildcards '*/packaging' --strip=1
    tar vxzf ${WORKDIR}/${TARFILE} --wildcards '*/VERSION' --strip=1
    source VERSION
    #
    sed -e "s:@@PBS_RELEASE@@:${VERSION}:g" \
        -e "s:@RPM_@RELEASE@@:${RPM_RELEASE}:g" \
        -e "s:@@REVISION@@:${REVISION}:g" \
        packaging/rpm/binlog-server.spec >rpmbuild/SPECS/binlog-server.spec
    mv -fv ${TARFILE} ${WORKDIR}/rpmbuild/SOURCES
    rpmbuild -bs --define "_topdir ${WORKDIR}/rpmbuild" --define "version ${VERSION}" --define "dist .generic" rpmbuild/SPECS/binlog-server.spec
    mkdir -p ${WORKDIR}/srpm
    mkdir -p ${CURDIR}/srpm
    cp rpmbuild/SRPMS/*.src.rpm ${CURDIR}/srpm
    cp rpmbuild/SRPMS/*.src.rpm ${WORKDIR}/srpm
    ls -la ${WORKDIR}/srpm
    ls -la ${CURDIR}/srpm
    return
}

build_rpm() {
    if [ $RPM = 0 ]; then
        echo "RPM will not be created"
        return
    fi
    if [ "x$OS" = "xdeb" ]; then
        echo "It is not possible to build rpm here"
        exit 1
    fi
    ls -la $WORKDIR
    ls -la $CURDIR
    ls -la $WORKDIR/srpm
    ls -la $CURDIR/srpm
    SRC_RPM=$(basename $(find $WORKDIR/srpm -name 'percona-binlog-server*.src.rpm' | sort | tail -n1))
    if [ -z $SRC_RPM ]; then
        SRC_RPM=$(basename $(find $CURDIR/srpm -name 'percona-binlog-server*.src.rpm' | sort | tail -n1))
        if [ -z $SRC_RPM ]; then
            echo "There is no src rpm for build"
            echo "You can create it using key --build_src_rpm=1"
            exit 1
        else
            cp $CURDIR/srpm/$SRC_RPM $WORKDIR
        fi
    else
        cp $WORKDIR/srpm/$SRC_RPM $WORKDIR
    fi
    cd $WORKDIR
    rm -fr rpmbuild
    mkdir -vp rpmbuild/{SOURCES,SPECS,BUILD,SRPMS,RPMS}
    cp $SRC_RPM rpmbuild/SRPMS/

    RHEL=$(rpm --eval %rhel)
    ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')

    echo "RHEL=${RHEL}" >>percona-binlog-server.properties
    echo "ARCH=${ARCH}" >>percona-binlog-server.properties
    #fi
    rpmbuild --define "_topdir ${WORKDIR}/rpmbuild" --define "dist .$OS_NAME" --rebuild rpmbuild/SRPMS/$SRC_RPM

    return_code=$?
    if [ $return_code != 0 ]; then
        exit $return_code
    fi
    mkdir -p ${WORKDIR}/rpm
    mkdir -p ${CURDIR}/rpm
    cp rpmbuild/RPMS/*/*.rpm ${WORKDIR}/rpm
    cp rpmbuild/RPMS/*/*.rpm ${CURDIR}/rpm
}

get_deb_sources(){
    param=$1
    echo $param
    FILE=$(basename $(find $WORKDIR/source_deb -iname "percona-binlog-server*.$param" | sort | tail -n1))
    if [ -z $FILE ]
    then
        FILE=$(basename $(find $CURDIR/source_deb -iname "percona-binlog-server*.$param" | sort | tail -n1))
        if [ -z $FILE ]
        then
            echo "There is no sources for build"
            exit 1
        else
            cp $CURDIR/source_deb/$FILE $WORKDIR/
        fi
    else
        cp $WORKDIR/source_deb/$FILE $WORKDIR/
    fi
    return
}

build_sdeb(){
    if [ $SDEB = 0 ]
    then
        echo "source deb package will not be created"
        return;
    fi
    if [ "x$OS" = "xrpm" ]
    then
        echo "It is not possible to build source deb here"
        exit 1
    fi
    rm -rf percona-*
    get_tar "source_tarball"
    rm -f *.dsc *.orig.tar.gz *.debian.tar.gz *.changes
    #

    TARFILE=$(basename $(find . -name 'percona-binlog-server-*.tar.gz' | sort | tail -n1))

    NAME=$(echo ${TARFILE}| awk -F '-' '{print $1"-"$2"-"$3}')
    VERSION=$(echo ${TARFILE}| awk -F '-' '{print $4}' | awk -F . '{print $1"."$2"."$3}')
    SHORTVER=$(echo ${VERSION} | awk -F '.' '{print $1"."$2}')
    #TMPREL=$(echo ${TARFILE}| awk -F '-' '{print $4}')
    #RELEASE=${TMPREL%.tar.gz}
    #
    NEWTAR=percona-binlog-server_${VERSION}.orig.tar.gz
    mv ${TARFILE} ${NEWTAR}

    tar xzf ${NEWTAR}
    cd percona-binlog-server-${VERSION}
    cp -av packaging/debian .
    sed -i "s:export DEB_VERSION=.*:export DEB_VERSION=${VERSION}:g" debian/rules
    export DEBEMAIL="support@percona.com"
    export DEBFULLNAME="Percona"
    dch -D unstable --force-distribution -v "${VERSION}-${DEB_RELEASE}" "Update to new upstream release Percona Binlog Server ${VERSION}-${DEB_RELEASE}"
    #copyright-update -d debian/copyright
    dpkg-buildpackage -S

    cd ${WORKDIR}
    ls -la

    mkdir -p $WORKDIR/source_deb
    mkdir -p $CURDIR/source_deb

    cp *.tar.xz* $WORKDIR/source_deb
    cp *_source.changes $WORKDIR/source_deb
    cp *.dsc $WORKDIR/source_deb
    cp *.orig.tar.gz $WORKDIR/source_deb

    cp *.tar.xz* $CURDIR/source_deb
    cp *_source.changes $CURDIR/source_deb
    cp *.dsc $CURDIR/source_deb
    cp *.orig.tar.gz $CURDIR/source_deb
}

build_deb(){
    if [ $DEB = 0 ]
    then
        echo "source deb package will not be created"
        return;
    fi
    if [ "x$OS" = "xrmp" ]
    then
        echo "It is not possible to build source deb here"
        exit 1
    fi
    for file in 'dsc' 'orig.tar.gz' 'changes' 'debian.tar.xz'
    do
        get_deb_sources $file
    done
    cd $WORKDIR
    #TARFILE=$(basename $(find . -name 'percona-binlog-server*.tar.gz' | sort | tail -n1))
    #tar zxf ${TARFILE}
    #rm -fv *.deb
    #
    export DEBIAN=$(lsb_release -sc)
    export ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
    #
    echo "DEBIAN=${DEBIAN}" >> percona-binlog-server.properties
    echo "ARCH=${ARCH}" >> percona-binlog-server.properties
    #
    DSC=$(basename $(find . -name '*.dsc' | sort | tail -n1))
    #
    dpkg-source -x ${DSC}
    #
    cd ${PRODUCT}-${VERSION}
    dch -m -D "${DEBIAN}" --force-distribution -v "${VERSION}-${RELEASE}.${DEBIAN}" 'Update distribution'
    dpkg-buildpackage -rfakeroot -us -uc -b
    mkdir -p $CURDIR/deb
    mkdir -p $WORKDIR/deb
    cp $WORKDIR/*.*deb $WORKDIR/deb
    cp $WORKDIR/*.*deb $CURDIR/deb
}

CURDIR=$(pwd)
VERSION_FILE=$CURDIR/percona-binlog-server.properties
args=
WORKDIR=
SRPM=0
SDEB=0
RPM=0
DEB=0
SOURCE=0
TARBALL=0
OS_NAME=
ARCH=
OS=
INSTALL=0
VERSION="1.0.0"
RELEASE="1"
REVISION=0
BRANCH="main"
REPO="https://github.com/Percona-Lab/percona-binlog-server"
PRODUCT=percona-binlog-server
parse_arguments PICK-ARGS-FROM-ARGV "$@"

check_workdir
get_system
install_deps
get_sources
build_srpm
build_sdeb
build_rpm
build_deb

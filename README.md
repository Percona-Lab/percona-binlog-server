# Percona Binary Log Server

`binlog_server` is a command line utility that can be considered as an enhanced version of [mysqlbinlog](https://dev.mysql.com/doc/refman/8.0/en/mysqlbinlog.html) in [--read-from-remote-server](https://dev.mysql.com/doc/refman/8.0/en/mysqlbinlog.html#option_mysqlbinlog_read-from-remote-server) mode which serves as a replication client and can stream binary log events from a remote [Oracle MySQL Server](https://www.mysql.com/) / [Percona Server for MySQL](https://www.percona.com/mysql/software/percona-server-for-mysql) both to a local filesystem and to a cloud storage (currently to `AWS S3`). It is capable of automatically reconnecting to the remote server and resume operation from the point when it was previously stopped / terminated.

It is written in portable c++ following the c++20 standard best practices.

## Installation

### Prebuilt Binaries

Currently prebuilt binaries are not available.

### Building from source

#### Dependencies

- [CMake](https://cmake.org/) 3.20.0+
- [Clang](https://clang.llvm.org/) (`clang-15` / `clang-16` / `clang-17`) or [GCC](https://gcc.gnu.org/) (`gcc-12` / `gcc-13` / `gcc-14`)
- [Boost libraries](https://www.boost.org/) 1.84.0 (git version, not the source tarball)
- [MySQL client library](https://dev.mysql.com/doc/c-api/8.0/en/) 8.0.x (`libmysqlclient`)
- [CURL library](https://curl.se/libcurl/) (`libcurl`) 8.6.0+
- [AWS SDK for C++](https://aws.amazon.com/sdk-for-cpp/) 1.11.286

#### Instructions

##### Creating a build workspace

```bash
mkdir ws
```
Every next step will assume that we are currently inside the `ws` directory unless explicitly stated otherwise.

##### Defining environment variables affecting build configurations

Define `BUILD_CONFIGURATION` depending on whether you want to build `Debug` or `Release` version of the library.

```bash
export BUILD_CONFIGURATION=Debug
```
or
```bash
export BUILD_CONFIGURATION=RelWithDebInfo
```
Define `BUILD_CC` and `BUILD_CXX` depending on the compiler you wish to use.
```bash
export BUILD_CC=gcc
export BUILD_CXX=g++
```
Instead of `gcc` you may use `gcc-12` / `gcc-13` / `gcc-14` or `clang-15` / `clang-16` / `clang-17`.
Instead of `g++` you may use `g++-12` / `g++-13` / `g++-14` or `clang++-15` / `clang++-16` / `clang++-17`.
Make sure that versions of the `CC` and `CXX` compilers match.

##### Boost Libraries

###### Getting Boost Libraries source

```bash
git clone --recurse-submodules -b boost-1.84.0 --jobs=8 https://github.com/boostorg/boost.git
cd boost
git switch -c required_release
```

###### Configuring Boost Libraries

```bash
cmake \
  -B ./boost-build-${BUILD_CONFIGURATION} \
  -S ./boost \
  -DCMAKE_INSTALL_PREFIX=./boost-install-${BUILD_CONFIGURATION} \
  -DCMAKE_BUILD_TYPE=${BUILD_CONFIGURATION} \
  -DCMAKE_C_COMPILER=${BUILD_CC} \
  -DCMAKE_CXX_COMPILER=${BUILD_CXX} \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF
```

###### Building Boost Libraries

```bash
cmake --build ./boost-build-${BUILD_CONFIGURATION} --config ${BUILD_CONFIGURATION} --parallel
```

###### Installing Boost Libraries

```bash
cmake --install ./boost-build-${BUILD_CONFIGURATION} --config ${BUILD_CONFIGURATION}
```

##### AWS SDK CPP Libraries

###### Getting AWS SDK CPP Libraries source

```bash
git clone --recurse-submodules --jobs=8 https://github.com/aws/aws-sdk-cpp
cd aws-sdk-cpp
git switch -c required_release
```

###### Configuring AWS SDK CPP Libraries

```bash
cmake \
  -B ./aws-sdk-cpp-build-${BUILD_CONFIGURATION} \
  -S ./aws-sdk-cpp \
  -DCMAKE_INSTALL_PREFIX=./aws-sdk-cpp-install-${BUILD_CONFIGURATION} \
  -DCMAKE_BUILD_TYPE=${BUILD_CONFIGURATION} \
  -DCMAKE_C_COMPILER=${BUILD_CC} \
  -DCMAKE_CXX_COMPILER=${BUILD_CXX} \
  -DCPP_STANDARD=20 \
  -DENABLE_UNITY_BUILD=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DFORCE_SHARED_CRT=OFF \
  -DENABLE_TESTING=OFF \
  -DAUTORUN_UNIT_TESTS=OFF \
  -DBUILD_ONLY=s3-crt
```

###### Building AWS SDK CPP Libraries

```bash
cmake --build ./aws-sdk-cpp-build-${BUILD_CONFIGURATION} --config ${BUILD_CONFIGURATION} --parallel
```

###### Installing AWS SDK CPP Libraries

```bash
cmake --install ./aws-sdk-cpp-build-${BUILD_CONFIGURATION} --config ${BUILD_CONFIGURATION}
```
##### Main Application

###### Getting Main Application source

```bash
git clone https://github.com/Percona-Lab/percona-binlog-server.git
```

###### Configuring Main Application

```bash
cmake  \
  -B ./percona-binlog-server-build-${BUILD_CONFIGURATION} \
  -S ./percona-binlog-server \
  -DCMAKE_BUILD_TYPE=${BUILD_CONFIGURATION} \
  -DCMAKE_C_COMPILER=${BUILD_CC} \
  -DCMAKE_CXX_COMPILER=${BUILD_CXX} \
  -DCPP_STANDARD=20 \
  -DCMAKE_PREFIX_PATH=${PWD}/aws-sdk-cpp-install-${BUILD_CONFIGURATION} \
  -DBoost_ROOT=${PWD}/boost-install-${BUILD_CONFIGURATION}
```

###### Building Main Application

```bash
cmake --build ./percona-binlog-server-build-${BUILD_CONFIGURATION} --config ${BUILD_CONFIGURATION} --parallel
```

###### Main Application binary

The result binary can be found under the following path `ws/percona-binlog-server-build-${BUILD_CONFIGURATION}/binlog_server`

## Usage

### Command line arguments

Please run
```bash
./binlog_server <operation_mode> <json_config_file>`
```
where
`<operation_mode>` can be either `fetch` or `pull`
and
`<json_config_file>` is a path to a JSON configuration file (described below).

### Operation modes

Percona Binary Log Server utility can operate in two modes:
- 'fetch'
- 'pull'

#### 'fetch' operation mode

In this mode the utility tries to connect to a remote MySQL server, switch connection to replication mode and read events from all available binary logs already stored on the server. After reading the very last event, the utility gracefully disconnects and exits.
Any error (network issues, server down, out of space, etc) encountered in this mode results in immediate termination of the program making sure that storage is left in consistent state.

#### 'pull' operation mode

In this mode the utility continuously tries to connect to a remote MySQL server / switch to replication mode and read binary log events. After reading the very last one, the utility does not close the connection immediately but instead waits for `<connection.read_timeout>` seconds for the server to generate more events. If this period of time elapses, the utility closes the MySQL connection and enters the `idle` mode. In this mode it just waits for `<replication.idle_time>` seconds in disconnected state. After that another reconnection attempt is made and everything starts from the beginning.
Any network-related error (network issues, server down, etc) encountered in this mode does not result in immediate termination of the program. Instead, another reconnection attempt is made. More serious errors (like out of space, etc.) cause program termination.

### JSON Configuration file

The Percona Binary Log Server configuration file has the following format.
```json
{
  "logger": {
    "level": "debug",
    "file": "binsrv.log"
  },
  "connection": {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "rpl_user",
    "password": "rpl_password",
    "connect_timeout": 20,
    "read_timeout": 60,
    "write_timeout": 60
  },
  "replication": {
    "server_id": 42,
    "idle_time": 10
  },
  "storage": {
    "uri": "file:///home/user/vault"
  }
}
```

#### \<logger\> section
- `<logger.level>` sets the minimum severity of the log messages that user want to appear in the log output, can be one of the `trace` / `debug` / `info` / `warning` / `error` / `fatal`  (explained below).
- `<logger.file>` can be either a path to a file on a local filesytem to which all log messages will be written or an empty string `""` meaning that all the output will be made to console (`STDOUT`).

##### Logger message severity levels

Each message written to the log has the `severity` level associated with it.
Currently we use the following mapping:
- `fatal` - currently not used,
- `error` - used for printing messages coming from caught exceptions,
- `warning` - currently not used,
- `info` - primary log severity level used mostly to indicate progress (configuration file read, storage created, connection established, etc.),
- `debug` - used to print function names from caught exceptions and to print the data from parsed binary log events,
- `trace` - used to print source file name / line number / position from caught exceptions and to print raw data (hex dumps) of binary log events.

#### \<connection\> section
- `<connection.host>` - MySQL server host name (e.g. `127.0.0.1`, `192.168.0.100`, `dbsrv.mydomain.com`, etc.). Please do not use `localhost` here as it will be interpreted differently by the `libmysqlclient` and will instruct the library to use Unix socket file for connection instead of TCP protocol - use `127.0.0.1` instead (see [this page](https://dev.mysql.com/doc/c-api/8.0/en/mysql-real-connect.html) for more details).
- `<connection.port>` - MySQL server port (e.g. `3306` - the default MySQL server port).
- `<connection.user>` - the name of the MySQL user that has [REPLICATION SLAVE](https://dev.mysql.com/doc/refman/8.0/en/replication-howto-repuser.html) privilege.
- `<connection.password>` - the password for this MySQL user
- `<connection.connect_timeout>` - the number of seconds the MySQL client library will wait to establish a connection with a remote host.
- `<connection.read_timeout>` - the number of seconds the MySQL client library will wait to read data from a remote server (this parameter may affect the responsiveness of the program to graceful termination - see below).
- `<connection.write_timeout>` - the number of seconds the MySQL client library will wait to write data to a remote server.

#### \<replication\> section
- `<replication.server_id>` - specifies the server ID that the utility will be using when connecting to a remote MySQL server (similar to [--connection-server-id](https://dev.mysql.com/doc/refman/8.0/en/mysqlbinlog.html#option_mysqlbinlog_connection-server-id) `mysqlbinlog` command line option).
- `<replication.idle_time>` - the number of seconds the utility will spend in disconnected mode between reconnection attempts.

#### \<storage\> section
- `<storage.uri>` - specifies the location (either local or remote) where the received binary logs should be stored

##### Storage URI format

The type of the storage where the received binary logs should be stored (currently, on a local filesystem of on AWS S3) is determined by the protocol of the URI.
- `file://...` - for local filesystem.
- `s3://...` - for AWS S3.

###### Local filesystem storage URIs

In case of local filesystem, the URIs must have the following format.
`file://<local_fs_path>`, where `<local_fs_path>` is an absolute path on a local filesystem to a directory where downloaded binary log files must be stored. Relative paths are not supported. For instance, `file:///home/user/vault`.

Please notice 3 forward slashes `/` (2 from the protocol part `file://` and 1 from the absolute path).

###### AWS S3 storage URIs
In case of AWS S3, the URIs must have the following format.
`s3://[<access_key_id>:<secret_access_key>@]<bucket_name>[.<region>]/<path>`, where:
- `<access_key_id>` - the AWS key ID (the `<access_key_id>` / `<secret_access_key>` pair is optional),
- `<secret_access_key>`- the AWS secret access key (the `<access_key_id>` / `<secret_access_key>` pair is optional),
- `<bucket_name>` - the name of AWS S3 bucket in which the data must be stored,
- `<region>` - the name of the AWS region (e.g. `us-east-1`) where this bucket was created (optional, if omitted, it will be auto-detected),
- `<path>` - a virtual path (key prefix) inside the bucket under which all the binary log files will be stored.

For example:
- `s3://binsrv-bucket/vault` - no AWS credentials specified, `binsrv-bucket` bucket must be publicly write-accessible, the region will be auto-detected, `/vault` will be the virtual directory.
- `s3://binsrv-bucket.us-east-1/vault` - no AWS credentials specified, `binsrv-bucket` bucket must be publicly write-accessible, the bucket must be created in the `us-east-1` region, `/vault` will be the virtual directory.
- `s3://key_id:secret@binsrv-bucket.us-east-1/vault` - `key_id` will be used as `AWS_ACCESS_KEY_ID`, `secret` will be used as `AWS_SECRET_ACCESS_KEY`, `binsrv-bucket` will be the name of the bucket, the bucket must be created in the `us-east-1` region, the bucket may have restricted write access, `/vault` will be the virtual directory.


### Resuming previous operation

Running the utility for the second time (in any mode) results in resuming streaming from the position at which the previous run finished.

### Graceful termination

The user can request the utility operating in either `fetch` or `pull` mode to be gracefully terminated leaving storage in consistent state. For this, the utility sets custom handlers for the the following POSIX signals.
- `SIGINT` - for processing `^C` in console.
- `SIGTERM` - for processing `kill <pid>`.

Because of the synchronous nature of the binlog API from the MySQL client library, there still may be a delay between receiving the signal and reacting to it. Worst case scenario, user will have to wait for `<connection.read_timeout>` seconds (the value from the configuration) + 1 (the granularity of sleep intervals in the `idle` mode) seconds.

Please note that killing the program with `kill -9 <pid>` does not guarantee to flush all the internal file buffers / upload temporary data to a cloud storage and may result in losing some progress.

## Licensing

Percona is dedicated to **keeping open source open**. Whenever possible, we strive to include permissive licensing for both our software and documentation. For this project, we are using version 2 of the GNU General Public License (GPLv2).

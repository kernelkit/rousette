# An almost-RESTCONF server

![License](https://img.shields.io/github/license/cesnet/rousette)
[![Gerrit](https://img.shields.io/badge/patches-via%20Gerrit-blue)](https://gerrit.cesnet.cz/q/project:CzechLight/rousette)
[![Zuul CI](https://img.shields.io/badge/zuul-checked-blue)](https://zuul.gerrit.cesnet.cz/t/public/buildsets?project=CzechLight/rousette)


A [RESTCONF](https://datatracker.ietf.org/doc/html/rfc8040.html) server built on top of [sysrepo](https://www.sysrepo.org/)

## Features

- [RESTCONF](https://datatracker.ietf.org/doc/html/rfc8040.html) server except:
    - TLS certificate authentication. See [Access control model](#access-control-model) below.
    - Datastore resource responses do not contain the [`Last-Modified`](https://datatracker.ietf.org/doc/html/rfc8040.html#section-3.4.1.1) and [`ETag`](https://datatracker.ietf.org/doc/html/rfc8040.html#section-3.4.1.2) headers for [edit collision prevention](https://datatracker.ietf.org/doc/html/rfc8040.html#section-3.4.1)
    - Data resource responses do not contain the [`Last-Modified`](https://datatracker.ietf.org/doc/html/rfc8040.html#section-3.5.1) and [`ETag`](https://datatracker.ietf.org/doc/html/rfc8040.html#section-3.5.2) headers.
    - The [`fields`](https://datatracker.ietf.org/doc/html/rfc8040.html#section-4.8.3) query parameter.
- [NMDA](https://datatracker.ietf.org/doc/html/rfc8527.html) support
- [YANG Patch](https://datatracker.ietf.org/doc/html/rfc8072) support


## Usage

Since this service only talks cleartext HTTP/2, it's recommended to run it behind a reverse proxy.

### Required YANG models

Rousette requires the following YANG models to be present in sysrepo:

- `ietf-yang-library@2019-01-04` (installed by sysrepo)
- `ietf-netconf` (installed by sysrepo)
- [`ietf-restconf@2017-01-26`](yang/ietf-restconf@2017-01-26.yang)
- [`ietf-restconf-monitoring@2017-01-26`](yang/ietf-restconf-monitoring@2017-01-26.yang)
- [`ietf-yang-patch@2017-02-22`](yang/ietf-yang-patch@2017-02-22.yang)

### Access control model

Rousette implements [RFC 8341 (NACM)](https://datatracker.ietf.org/doc/html/rfc8341.html)
The access rights for users (and groups) are configurable via `ietf-netconf-acm` YANG model.

The reverse proxy must pass the `authorization` header as-is and delegate authentication/authorization to the RESTCONF server.
The server currently supports two authentication/authorization methods:

- a systemwide PAM setup through the *Basic* HTTP authentication, i.e., via the `authorization` header, which is checked against the system's PAM configuration
- a special anonymous access.

When the request does not contain the `authorization` header, and anonymous access is enabled (see below), the server will perform extra safety checks.
When certain conditions are met, the anonymous access will be mapped to a NACM account named in the `ANONYMOUS_USER` CMake option.
Such user must be in group `ANONYMOUS_USER_GROUP` (CMake option) and there must be some specific access rights set up in `ietf-netconf-acm` model (these are currently very opinionated for our use-case):

1. The first entry of `rule-list` list must be configured for `ANONYMOUS_USER_GROUP`.
2. All the rules except the last one in this rule-list entry must enable only "read" access operation.
3. The last rule in the first rule-set must be a wildcard rule that disables all operations over all modules.

The anonymous user access is disabled whenever these rules are not met.

### YANG schema retrieval

As an extension to the RESTCONF protocol, all YANG modules which are available through sysrepo can be fetched via the `/yang/` endpoint.
This access is controlled through NACM as-if the access was made against the `location` leaf-list within the [`ietf-yang-library`](https://datatracker.ietf.org/doc/html/rfc8525#section-3).
In practical terms, this means that the NACM access rules for the following XPaths also control schema retrieval:

- `/ietf-yang-library:yang-library/module-set[name='complete']/module/location`
- `/ietf-yang-library:yang-library/module-set[name='complete']/import-only-module/location`
- `/ietf-yang-library:yang-library/module-set[name='complete']/module/submodule/location`
- `/ietf-yang-library:yang-library/module-set[name='complete']/import-only-module/submodule/location`

## Dependencies

- [nghttp2-asio](https://github.com/nghttp2/nghttp2-asio) - asynchronous C++ library for HTTP/2
- [sysrepo-cpp](https://github.com/sysrepo/sysrepo-cpp) - object-oriented bindings of the [*sysrepo*](https://github.com/sysrepo/sysrepo) library
- [libyang-cpp](https://github.com/CESNET/libyang-cpp) - C++ bindings for *libyang*
- [PAM](http://www.linux-pam.org/) - for authentication
- [spdlog](https://github.com/gabime/spdlog) - Very fast, header-only/compiled, C++ logging library
- [docopt-cpp](https://github.com/docopt/docopt.cpp) - command-line argument parser
- Boost's system and thread
- C++20 compiler (e.g., GCC 10.x+, clang 10+)
- CMake 3.19+
- optionally systemd - the shared library for logging to `sd-journal`
- optionally for built-in tests, [Doctest](https://github.com/onqtam/doctest/) as a C++ unit test framework
- optionally for built-in tests, [trompeloeil](https://github.com/rollbear/trompeloeil) for mock objects in C++
- optionally for built-in tests, [`pam_matrix` and `pam_wrapper`](https://cwrap.org/pam_wrapper.html) for PAM mocking

## Building

The standard way of building *rousette* looks like this:
```
mkdir build
cd build
cmake ..
make
make install
```

## Contributing

The development is being done on Gerrit [here](https://gerrit.cesnet.cz/q/project:CzechLight/rousette).
Instructions on how to submit patches can be found [here](https://gerrit.cesnet.cz/Documentation/intro-gerrit-walkthrough-github.html).
GitHub Pull Requests are not used.

#!/bin/sh
set -o xtrace   # Write all commands first to stderr
set -o errexit  # Exit the script with error if any of the commands fail

# Supported/used environment variables:
#       CFLAGS                  Additional compiler flags
#       MARCH                   Machine Architecture. Defaults to lowercase uname -m
#       RELEASE                 Use the fully qualified release archive
#       DEBUG                   Use debug configure flags
#       VALGRIND                Run the test suite through valgrind
#       CC                      Which compiler to use
#       ANALYZE                 Run the build through clangs scan-build
#       COVERAGE                Produce code coverage reports
#       LIBBSON                 Build against bundled or external libbson
#       EXTRA_CONFIGURE_FLAGS   Extra configure flags to use

RELEASE=${RELEASE:-no}
DEBUG=${DEBUG:-no}
VALGRIND=${VALGRIND:-no}
ANALYZE=${ANALYZE:-no}
COVERAGE=${COVERAGE:-no}
SASL=${SASL:-no}
SSL=${SSL:-no}

echo "CFLAGS: $CFLAGS"
echo "MARCH: $MARCH"
echo "RELEASE: $RELEASE"
echo "DEBUG: $DEBUG"
echo "VALGRIND: $VALGRIND"
echo "CC: $CC"
echo "ANALYZE: $ANALYZE"
echo "COVERAGE: $COVERAGE"


# Get the kernel name, lowercased
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
echo "OS: $OS"

# Automatically retrieve the machine architecture, lowercase, unless provided
# as an environment variable (e.g. to force 32bit)
[ -z "$MARCH" ] && MARCH=$(uname -m | tr '[:upper:]' '[:lower:]')

INSTALL_DIR=$(pwd)/install-dir

# Default configure flags for debug builds and release builds
DEBUG_FLAGS="\
   --enable-html-docs=no \
   --enable-man-pages=no \
   --enable-yelp=no \
   --enable-examples=no \
   --enable-optimizations=no \
   --enable-extra-align=no \
   --enable-maintainer-flags \
   --enable-debug \
   --disable-silent-rules \
   --disable-automatic-init-and-cleanup \
   --prefix=$INSTALL_DIR \
"

RELEASE_FLAGS="\
   --enable-man-pages=no \
   --enable-html-docs=no \
   --enable-extra-align=no \
   --enable-optimizations \
   --disable-automatic-init-and-cleanup \
   --prefix=$INSTALL_DIR \
"
if [ "$LIBBSON" = "external" ]; then
   RELEASE_FLAGS="$RELEASE_FLAGS --with-libbson=system"
   DEBUG_FLAGS="$DEBUG_FLAGS --with-libbson=system"
else
   RELEASE_FLAGS="$RELEASE_FLAGS --with-libbson=bundled"
   DEBUG_FLAGS="$DEBUG_FLAGS --with-libbson=bundled"
fi

# By default we build from git clone, which requires autotools
# This gets overwritten if we detect we should use the release archive
CONFIGURE_SCRIPT="./autogen.sh"


# --strip-components is an GNU tar extension. Check if the platform
# (e.g. Solaris) has GNU tar installed as `gtar`, otherwise we assume to be on
# platform that supports it
# command -v returns success error code if found and prints the path to it
if command -v gtar 2>/dev/null; then
   TAR=gtar
else
   TAR=tar
fi

# Available on our Ubuntu 16.04 images
[ "$ANALYZE" = "yes" ] && SCAN_BUILD="scan-build -o scan --status-bugs"

[ "$DEBUG" = "yes" ] && CONFIGURE_FLAGS=$DEBUG_FLAGS || CONFIGURE_FLAGS=$RELEASE_FLAGS

CONFIGURE_FLAGS="$CONFIGURE_FLAGS --enable-sasl=${SASL}"
CONFIGURE_FLAGS="$CONFIGURE_FLAGS --enable-ssl=${SSL}"
[ "$COVERAGE" = "yes" ] && CONFIGURE_FLAGS="$CONFIGURE_FLAGS --enable-coverage"

[ "$VALGRIND" = "yes" ] && TARGET="valgrind" || TARGET="test"

if [ "$RELEASE" = "yes" ]; then
   # Overwrite the git checkout with the packaged archive
   $TAR xf ../mongoc.tar.gz -C . --strip-components=1
   CONFIGURE_SCRIPT="./configure"
fi

#if ldconfig -N -v 2>/dev/null | grep -q libSegFault.so; then
   #export SEGFAULT_SIGNALS="all"
   #export LD_PRELOAD="libSegFault.so"
#fi

# AddressSanitizer configuration
ASAN_OPTIONS="detect_leaks=1"
# LeakSanitizer configuration
LSAN_OPTIONS="log_pointers=true"

case "$MARCH" in
   i386)
      CFLAGS="$CFLAGS -m32 -march=i386"
   ;;
   s390x)
      CFLAGS="$CFLAGS -march=z196 -mtune=zEC12"
   ;;
   x86_64)
      CFLAGS="$CFLAGS -m64 -march=x86-64"
   ;;
   ppc64le)
      CFLAGS="$CFLAGS -mcpu=power8 -mtune=power8 -mcmodel=medium"
   ;;
esac
CFLAGS="$CFLAGS -Werror"


case "$OS" in
   darwin)
      CFLAGS="$CFLAGS -Wno-unknown-pragmas"
      export DYLD_LIBRARY_PATH=".libs:src/libbson/.libs"
      export PATH=/usr/local/Cellar/llvm/3.8.1/bin/:$PATH
   ;;

   linux)
      # Make linux builds a tad faster by parallelise the build
      cpus=$(grep -c '^processor' /proc/cpuinfo)
      MAKEFLAGS="-j${cpus}"
      export LD_LIBRARY_PATH=".libs:src/libbson/.libs"
   ;;

   sunos)
      PATH="/opt/mongodbtoolchain/bin:$PATH"
      if [  "$SASL" != "no" ]; then
         export SASL_CFLAGS="-I/opt/csw/include/"
         export SASL_LIBS="-L/opt/csw/lib/amd64/ -lsasl2"
      fi
      export LD_LIBRARY_PATH="/opt/csw/lib/amd64/:.libs:src/libbson/.libs"
   ;;
esac

CONFIGURE_FLAGS="$CONFIGURE_FLAGS $EXTRA_CONFIGURE_FLAGS"
export MONGOC_TEST_FUTURE_TIMEOUT_MS=30000
export MONGOC_TEST_SKIP_LIVE=on
export MONGOC_TEST_SKIP_SLOW=on

export CFLAGS="$CFLAGS"
export CC="$CC"

if [ "$LIBBSON" = "external" ]; then
   # This usually happens in mongoc ./autogen.sh, but since we are compiling against
   # external libbson we need to install libbson before running mongoc ./autogen.sh
   git submodule update --init
   cd src/libbson
   ./autogen.sh $CONFIGURE_FLAGS
   make all
   make install
   cd ../../
   export PKG_CONFIG_PATH=$INSTALL_DIR/lib/pkgconfig:$PKG_CONFIG_PATH
fi

$SCAN_BUILD $CONFIGURE_SCRIPT $CONFIGURE_FLAGS
$SCAN_BUILD make all
$SCAN_BUILD make $TARGET TEST_ARGS="-d -F test-results.json"


if [ "$COVERAGE" = "yes" ]; then
   case "$CC" in
      clang)
         lcov --gcov-tool `pwd`/.evergreen/llvm-gcov.sh --capture --derive-func-data --directory . --output-file .coverage.lcov --no-external
         ;;
      *)
         lcov --gcov-tool gcov --capture --derive-func-data --directory . --output-file .coverage.lcov --no-external
         ;;
   esac
   genhtml .coverage.lcov --legend --title "mongoc code coverage" --output-directory coverage
fi

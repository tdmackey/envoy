#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e

. "$(dirname "$0")"/build_setup.sh
echo "building using ${NUM_CPUS} CPUs"
export COV_DIR=/cov
export PATH=$PATH:${COV_DIR}/

function bazel_coverity_release_binary_build() {
  echo "Building..."
  cd "${ENVOY_CI_DIR}"
  rm -rf cov-int
  ls ${COV_DIR}
  cov-build --dir cov-int bazel --batch build --action_env=LD_PRELOAD ${BAZEL_BUILD_OPTIONS} -c opt //source/exe:envoy-static.stamped
  # tar up the coverity results
  tar czvf envoy-coverity-output.tgz cov-int
  # Copy the coverity results somwherethat we can access outside of the
  # container.
  cp -f \
    "${ENVOY_CI_DIR}"/envoy-coverity-output.tgz \
    "${ENVOY_DELIVERY_DIR}"/envoy-coverity-output.tgz
}

function bazel_release_binary_build() {
  echo "Building..."
  cd "${ENVOY_CI_DIR}"
  bazel --batch build ${BAZEL_BUILD_OPTIONS} -c opt //source/exe:envoy-static.stamped
  # Copy the envoy-static binary somewhere that we can access outside of the
  # container.
  cp -f \
    "${ENVOY_CI_DIR}"/bazel-genfiles/source/exe/envoy-static.stamped \
    "${ENVOY_DELIVERY_DIR}"/envoy
}

function bazel_debug_binary_build() {
  echo "Building..."
  cd "${ENVOY_CI_DIR}"
  bazel --batch build ${BAZEL_BUILD_OPTIONS} -c dbg //source/exe:envoy-static.stamped
  # Copy the envoy-static binary somewhere that we can access outside of the
  # container.
  cp -f \
    "${ENVOY_CI_DIR}"/bazel-genfiles/source/exe/envoy-static.stamped \
    "${ENVOY_DELIVERY_DIR}"/envoy-debug
}

if [[ "$1" == "bazel.release" ]]; then
  setup_gcc_toolchain
  echo "bazel release build with tests..."
  bazel_release_binary_build
  echo "Testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c opt //test/...
  exit 0
elif [[ "$1" == "bazel.release.server_only" ]]; then
  setup_gcc_toolchain
  echo "bazel release build..."
  bazel_release_binary_build
  exit 0
elif [[ "$1" == "bazel.cov-build" ]]; then
  setup_gcc_toolchain
  setup_coverity_toolchain
  echo "bazel coverity build"
  bazel_coverity_release_binary_build
  exit 0
elif [[ "$1" == "bazel.debug" ]]; then
  setup_gcc_toolchain
  echo "bazel debug build with tests..."
  bazel_debug_binary_build
  echo "Testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c dbg //test/...
  exit 0
elif [[ "$1" == "bazel.debug.server_only" ]]; then
  setup_gcc_toolchain
  echo "bazel debug build..."
  bazel_debug_binary_build
  exit 0
elif [[ "$1" == "bazel.asan" ]]; then
  setup_clang_toolchain
  echo "bazel ASAN/UBSAN debug build with tests..."
  # Due to Travis CI limits, we build and run the single fat coverage test binary rather than
  # build O(100) * O(200MB) static test binaries. This saves 20GB of disk space, see #1400.
  cd "${ENVOY_BUILD_DIR}"
  NO_GCOV=1 "${ENVOY_SRCDIR}"/test/coverage/gen_build.sh
  cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  echo "Building and testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c dbg --config=clang-asan @envoy//test/coverage:coverage_tests \
    //:echo2_integration_test //:envoy_binary_test
  exit 0
elif [[ "$1" == "bazel.tsan" ]]; then
  setup_clang_toolchain
  echo "bazel TSAN debug build with tests..."
  cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  echo "Building and testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c dbg --config=clang-tsan @envoy//test/... \
    //:echo2_integration_test //:envoy_binary_test
  exit 0
elif [[ "$1" == "bazel.dev" ]]; then
  setup_clang_toolchain
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel fastbuild build with tests..."
  cd "${ENVOY_CI_DIR}"
  echo "Building..."
  bazel --batch build ${BAZEL_BUILD_OPTIONS} -c fastbuild //source/exe:envoy-static
  # Copy the envoy-static binary somewhere that we can access outside of the
  # container for developers.
  cp -f \
    "${ENVOY_CI_DIR}"/bazel-bin/source/exe/envoy-static \
    "${ENVOY_DELIVERY_DIR}"/envoy-fastbuild
  echo "Building and testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c fastbuild //test/...
  exit 0
elif [[ "$1" == "bazel.coverage" ]]; then
  setup_gcc_toolchain
  echo "bazel coverage build with tests..."
  export GCOVR="/thirdparty/gcovr/scripts/gcovr"
  export GCOVR_DIR="${ENVOY_BUILD_DIR}/bazel-envoy"
  export TESTLOGS_DIR="${ENVOY_BUILD_DIR}/bazel-testlogs"
  export WORKSPACE=ci
  # There is a bug in gcovr 3.3, where it takes the -r path,
  # in our case /source, and does a regex replacement of various
  # source file paths during HTML generation. It attempts to strip
  # out the prefix (e.g. /source), but because it doesn't do a match
  # and only strip at the start of the string, it removes /source from
  # the middle of the string, corrupting the path. The workaround is
  # to point -r in the gcovr invocation in run_envoy_bazel_coverage.sh at
  # some Bazel created symlinks to the source directory in its output
  # directory. Wow.
  cd "${ENVOY_BUILD_DIR}"
  SRCDIR="${GCOVR_DIR}" "${ENVOY_SRCDIR}"/test/run_envoy_bazel_coverage.sh
  rsync -av "${ENVOY_BUILD_DIR}"/bazel-envoy/generated/coverage/ "${ENVOY_COVERAGE_DIR}"
  exit 0
elif [[ "$1" == "fix_format" ]]; then
  echo "fix_format..."
  cd "${ENVOY_SRCDIR}"
  ./tools/check_format.py fix
  exit 0
elif [[ "$1" == "check_format" ]]; then
  echo "check_format..."
  cd "${ENVOY_SRCDIR}"
  ./tools/check_format.py check
  exit 0
else
  echo "Invalid do_ci.sh target, see ci/README.md for valid targets."
  exit 1
fi

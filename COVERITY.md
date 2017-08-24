# Coverity

1. Add vsyscall=emulate to kernel boot parametres and reboot if necessary
2. Download Coverity Scan build tool https://scan.coverity.com/download
3. extract and add coverity scan bin directory to $PATH
4. run ./cov-build.sh
5. upload ci/envoy-coverity-output.tar.gz

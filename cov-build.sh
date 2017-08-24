#!/bin/bash
TEST_TYPE=bazel.cov-build
ENVOY_BUILD_SHA=2cf029ca44a2238a90460cf5269dbc342e87d24f

docker run -t -i -v /tmp/envoy-docker-build:/build -v $PWD:/source -v /home/tdmackey/workspace/cov-analysis-linux64-2017.07:/cov \
 lyft/envoy-build:$ENVOY_BUILD_SHA /bin/bash -c "cd /source && ci/do_ci.sh $TEST_TYPE"

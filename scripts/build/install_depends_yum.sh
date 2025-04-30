#!/bin/sh

set -e

if [ "${PY_VER}" = "3.9" -o "${PY_VER}" = "3.8" ]
then
  PY_VER="3${PY_VER#3.}"
  dnf module enable python${PY_VER} -y
fi

dnf install -y redhat-lsb-core python${PY_VER} python${PY_VER}-devel

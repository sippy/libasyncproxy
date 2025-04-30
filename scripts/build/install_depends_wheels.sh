#!/bin/sh

set -e

python${PY_VER} -m ensurepip --upgrade
python${PY_VER} -m pip install --upgrade pip
python${PY_VER} -m pip install --upgrade build setuptools wheel auditwheel

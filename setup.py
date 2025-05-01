#!/usr/bin/env python

import os

from distutils.core import setup
from distutils.core import Extension
from sysconfig import get_platform

os.environ['LAP_NO_INIT'] = '1'
from python.env import LAP_MOD_NAME

is_win = get_platform().startswith('win')
is_mac = get_platform().startswith('macosx-')

lap_srcs = ['src/asyncproxy.c', 'src/asp_sock.c']

extra_compile_args = ['-Wall', '-DPYTHON_AWARE']
if not is_win:
    extra_compile_args += ['--std=c11', '-Wno-zero-length-array', '-Isrc/',
                           '-flto', '-pedantic']
else:
    extra_compile_args.append('/std:clatest')
extra_link_args = ['-flto'] if not is_win else []

debug_opts = ('-g3', '-O0')
nodebug_opts = ('-DNO_DEBUG',)
nodebug_opts += ('-O3',) if not is_win else ()

if get_platform() == 'linux-x86_64':
    # This is to disable x86-64-v2, see
    # https://github.com/pypa/manylinux/issues/1725
    extra_compile_args.append('-march=x86-64')

if False:
    extra_compile_args.extend(debug_opts)
    extra_link_args.extend(debug_opts)
else:
    extra_compile_args.extend(nodebug_opts)
    extra_link_args.extend(nodebug_opts)

if not is_mac and not is_win:
    extra_link_args.append('-Wl,--version-script=src/Symbol.map')
elif is_mac:
    extra_link_args.extend(['-undefined', 'dynamic_lookup'])

module1 = Extension(LAP_MOD_NAME, sources = lap_srcs, \
    extra_link_args = extra_link_args, \
    extra_compile_args = extra_compile_args)

def get_ex_mod():
    if 'NO_PY_EXT' in os.environ:
        return None
    return [module1]

with open("README.md", "r") as fh:
    long_description = fh.read()

kwargs = {'name':'asyncproxy',
      'version':'1.1',
      'description':'Background TCP proxy for async IO',
      'long_description': long_description,
      'long_description_content_type': "text/markdown",
      'author':'Maksym Sobolyev',
      'author_email':'sobomax@sippysoft.com',
      'url':'https://github.com/sippy/libasyncproxy.git',
      'packages':['asyncproxy',],
      'package_dir':{'asyncproxy':'python'},
      'ext_modules': get_ex_mod(),
      'license': 'BSD-2-Clause',
      'classifiers': [
            'Operating System :: POSIX',
            'Programming Language :: C',
            'Programming Language :: Python'
      ]
     }

if __name__ == '__main__':
    setup(**kwargs)

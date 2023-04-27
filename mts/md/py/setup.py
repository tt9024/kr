from distutils.core import setup, Extension
import numpy
import os
import sysconfig

# to compile, run:
#      python3 setup.py build_ext --inplace

extra_compile_args = sysconfig.get_config_var('CFLAGS').split()

#replace any -O with -O3
#extra_compile_args += ['-DNDEBUG','-O3']
extra_compile_args += ['-g','-I/home/mts/dev/src/util','-I/home/mts/dev/src/util/plcc','-I/home/mts/dev/src/md']

tp_parser_module_np = Extension('td_parser_module_np', \
                                sources = ['td_parser_np.cpp'], \
                                include_dirs = [numpy.get_include()], \
                                extra_compile_args=extra_compile_args)
setup(ext_modules = [tp_parser_module_np])

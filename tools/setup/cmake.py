"Manages CMake."
import os
import re
import shutil
from subprocess import check_call, check_output
import sys
import distutils
import distutils.sysconfig
from distutils.version import LooseVersion
from setuptools import Extension
from collections import defaultdict
from .env import BUILD_DIR, check_env_flag
# from .numpy_ import USE_NUMPY, NUMPY_INCLUDE_DIR


def _mkdir_p(d):
    try:
        os.makedirs(d)
    except OSError:
        pass


# Ninja
# Use ninja if it is on the PATH. Previous version of PyTorch required the
# ninja python package, but we no longer use it, so we do not have to import it
# USE_NINJA = (not check_negative_env_flag('USE_NINJA') and
#              shutil.which('ninja') is not None)
def convert_cmake_value_to_python_value(cmake_value, cmake_type):
    r"""Convert a CMake value in a string form to a Python value.

    Arguments:
      cmake_value (string): The CMake value in a string form (e.g., "ON", "OFF", "1").
      cmake_type (string): The CMake type of :attr:`cmake_value`.

    Returns:
      A Python value corresponding to :attr:`cmake_value` with type :attr:`cmake_type`.
    """

    cmake_type = cmake_type.upper()
    up_val = cmake_value.upper()
    if cmake_type == 'BOOL':
        # https://gitlab.kitware.com/cmake/community/wikis/doc/cmake/VariablesListsStrings#boolean-values-in-cmake
        return not (up_val in ('FALSE', 'OFF', 'N', 'NO', '0', '', 'NOTFOUND') or up_val.endswith('-NOTFOUND'))
    elif cmake_type == 'FILEPATH':
        if up_val.endswith('-NOTFOUND'):
            return None
        else:
            return cmake_value
    else:  # Directly return the cmake_value.
        return cmake_value


def get_cmake_cache_variables_from_file(cmake_cache_file):
    r"""Gets values in CMakeCache.txt into a dictionary.

    Arguments:
      cmake_cache_file: A CMakeCache.txt file object.
    Returns:
      dict: A ``dict`` containing the value of cached CMake variables.
    """

    results = dict()
    for i, line in enumerate(cmake_cache_file, 1):
        line = line.strip()
        if not line or line.startswith(('#', '//')):
            # Blank or comment line, skip
            continue

        # Almost any character can be part of variable name and value. As a practical matter, we assume the type must be
        # valid if it were a C variable name. It should match the following kinds of strings:
        #
        #   USE_CUDA:BOOL=ON
        #   "USE_CUDA":BOOL=ON
        #   USE_CUDA=ON
        #   USE_CUDA:=ON
        #   Intel(R) MKL-DNN_SOURCE_DIR:STATIC=/path/to/pytorch/third_party/ideep/mkl-dnn
        #   "OpenMP_COMPILE_RESULT_CXX_openmp:experimental":INTERNAL=FALSE
        matched = re.match(r'("?)(.+?)\1(?::\s*([a-zA-Z_-][a-zA-Z0-9_-]*)?)?\s*=\s*(.*)', line)
        if matched is None:  # Illegal line
            raise ValueError('Unexpected line {} in {}: {}'.format(i, repr(cmake_cache_file), line))
        _, variable, type_, value = matched.groups()
        if type_ is None:
            type_ = ''
        if type_.upper() in ('INTERNAL', 'STATIC'):
            # CMake internal variable, do not touch
            continue
        results[variable] = convert_cmake_value_to_python_value(value, type_)

    return results


class CMakeExtension(Extension):
    """CMake extension"""
    def __init__(self, name, cmake_file):
        super().__init__(name, [])
        self.build_dir = BUILD_DIR
        self.cmake_file = cmake_file
        self._cmake_command = CMakeExtension._get_cmake_command()
        self.debug = True
        self.cmake_dir = os.path.dirname(cmake_file)

    @staticmethod
    def _get_version(cmd):
        """Returns cmake version."""

        for line in check_output([cmd, '--version']).decode('utf-8').split('\n'):
            if 'version' in line:
                return LooseVersion(line.strip().split(' ')[2])
        raise RuntimeError('no version found')

    @staticmethod
    def _get_cmake_command():
        """Returns cmake command."""

        cmake_command = shutil.which('cmake')
        cmake3 = shutil.which('cmake3')
        if cmake3 is not None:
            cmake = shutil.which('cmake')
            if cmake is not None:
                bare_version = CMakeExtension._get_version(cmake)
                if (bare_version < LooseVersion("3.5.0") and
                        CMakeExtension._get_version(cmake3) > bare_version):
                    cmake_command = 'cmake3'
        return cmake_command

    @staticmethod
    def defines(args, **kwargs):
        "Adds definitions to a cmake argument list."
        for key, value in sorted(kwargs.items()):
            if value is not None:
                args.append('-D{}={}'.format(key, value))

    @staticmethod
    def _cmake_value(value):
        if type(value) is str:
            if value.startswith(('OFF', '0', 'False', 'FALSE')):
                return False
            if value.startswith(('ON', '1', 'True', 'TRUE')):
                return True
        return value

    @staticmethod
    def extract(args):
        "Adds definitions to a cmake argument list."
        build_options = {}
        pat = re.compile(r'^-D(.*)=(.*)')
        for arg in args:
            match = pat.match(arg)

            build_options[match.group(1)] = CMakeExtension._cmake_value(match.group(2))

        return build_options

    @staticmethod
    def convert_cmake_dirs(paths):
        def converttostr(input_seq, seperator):
            # Join all the strings in list
            final_str = seperator.join(input_seq)
            return final_str
        try:
            return converttostr(paths, ";")
        except:
            return paths

    @property
    def _cmake_cache_file(self):
        r"""Returns the path to CMakeCache.txt.

        Returns:
          string: The path to CMakeCache.txt.
        """
        return os.path.join(self.build_dir, 'CMakeCache.txt')

    def _get_cmake_cache_variables(self):
        r"""Gets values in CMakeCache.txt into a dictionary.
        Returns:
          dict: A ``dict`` containing the value of cached CMake variables.
        """
        with open(self._cmake_cache_file) as f:
            return get_cmake_cache_variables_from_file(f)

    def _run(self, args, env):
        """Executes cmake with arguments and an environment."""
        command = [self._cmake_command] + args + [self.cmake_dir]
        if command is None:
            print('cmake not found, please install cmake before continuing...')
            sys.exit(1)
        print(' '.join(command))
        check_call(command, cwd=self.build_dir, env=env)

    def generate(self, build_options, env, build_dir, install_dir):
        """Runs cmake to generate native build files."""

        self.build_dir = build_dir

        cmake_args = []

        for var, val in env.items():
            if var.startswith(('BUILD_', 'USE_', 'CMAKE_')):
                # TODO: DO NOT OVERWRITE CMAKE_PREFIX_PATH
                if var.strip() == "CMAKE_PREFIX_PATH":
                    build_options[var] += ";" + val
                else:
                    build_options[var] = val

        if 'CMAKE_BUILD_TYPE' not in env:
            if check_env_flag('DEBUG', env=env):
                build_options['CMAKE_BUILD_TYPE'] = 'Debug'
            elif check_env_flag('REL_WITH_DEB_INFO', env=env):
                build_options['CMAKE_BUILD_TYPE'] = 'RelWithDebInfo'
            else:
                build_options['CMAKE_BUILD_TYPE'] = 'Release'
        build_options['CMAKE_INSTALL_PREFIX'] = install_dir

        CMakeExtension.defines(cmake_args, **build_options)
        if os.path.exists(self._cmake_cache_file):
            try:
                cmake_cache_vars = defaultdict(lambda: False, self._get_cmake_cache_variables())
            except FileNotFoundError:
                # CMakeCache.txt does not exist. Probably running "python setup.py clean" over a clean directory.
                cmake_cache_vars = defaultdict(lambda: False)

            cache_build_options = CMakeExtension.extract(cmake_args)
            if all(option in cmake_cache_vars and
                   CMakeExtension._cmake_value(cache_build_options[option]) == CMakeExtension._cmake_value(cmake_cache_vars[option])
                   for option in cache_build_options):
                # Everything's in place. Do not rerun.
                return
        self._run(cmake_args, env=env)

#!/usr/bin/env bash

# Copyright 2022 The Dredd Project Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -x
set -e
set -u

# Old bash versions can't expand empty arrays, so we always include at least this option.
CMAKE_OPTIONS=("-DCMAKE_OSX_ARCHITECTURES=x86_64")

help | head

uname

DREDD_LLVM_TAG=$(./scripts/llvm_tag.sh)

case "$(uname)" in
"Linux")
  NINJA_OS="linux"
  # Provided by build.yml.
  export CC="${LINUX_CC}"
  export CXX="${LINUX_CXX}"
  # Free up some space
  df -h
  sudo apt clean
  # shellcheck disable=SC2046
  docker rmi -f $(docker image ls -aq)
  sudo rm -rf /usr/share/dotnet /usr/local/lib/android /opt/ghc
  df -h

  # Install clang.
  pushd ./third_party/clang+llvm
    curl -fsSL -o clang+llvm.tar.xz "https://github.com/llvm/llvm-project/releases/download/llvmorg-${DREDD_LLVM_TAG}/clang+llvm-${DREDD_LLVM_TAG}-x86_64-linux-gnu-ubuntu-22.04.tar.xz"
    tar xf clang+llvm.tar.xz
    mv clang+llvm-${DREDD_LLVM_TAG}-x86_64-linux-gnu-ubuntu-22.04/* .
    rm clang+llvm.tar.xz
  popd
  ;;

"Darwin")
  NINJA_OS="mac"

  # Install clang.
  pushd ./third_party/clang+llvm
    curl -fsSL -o clang+llvm.zip "https://github.com/mc-imperial/build-clang/releases/download/llvmorg-${DREDD_LLVM_TAG}/build-clang-llvmorg-${DREDD_LLVM_TAG}-Mac_x64_Release.zip"
    unzip clang+llvm.zip
    rm clang+llvm.zip
  popd
  ;;

"MINGW"*|"MSYS_NT"*)
  NINJA_OS="win"
  CMAKE_OPTIONS+=("-DCMAKE_C_COMPILER=cl.exe" "-DCMAKE_CXX_COMPILER=cl.exe")
  choco install zip

  # Install clang.
  pushd ./third_party/clang+llvm
    curl -fsSL -o clang+llvm.zip "https://github.com/mc-imperial/build-clang/releases/download/llvmorg-${DREDD_LLVM_TAG}/build-clang-llvmorg-${DREDD_LLVM_TAG}-Windows_x64_Release.zip"
    unzip clang+llvm.zip
    rm clang+llvm.zip
  popd
  ;;

*)
  echo "Unknown OS"
  exit 1
  ;;
esac

export PATH="${HOME}/bin:$PATH"

mkdir -p "${HOME}/bin"

pushd "${HOME}/bin"

  # Install ninja.
  curl -fsSL -o ninja-build.zip "https://github.com/ninja-build/ninja/releases/download/v1.11.0/ninja-${NINJA_OS}.zip"
  unzip ninja-build.zip

  ls

popd

case "$(uname)" in
"Linux")

  # On Linux, source the dev shell to download clang-tidy and other tools.
  # Developers should *run* the dev shell, but we want to continue executing this script.
  export DREDD_SKIP_COMPILER_SET=1
  export DREDD_SKIP_BASH=1

  source ./dev_shell.sh.template

  ;;

"Darwin")
  ;;

"MINGW"*|"MSYS_NT"*)
  ;;

*)
  echo "Unknown OS"
  exit 1
  ;;
esac

mkdir -p build
pushd build
  cmake -G Ninja .. -DCMAKE_BUILD_TYPE="${CONFIG}" "${CMAKE_OPTIONS[@]}"
  cmake --build . --config "${CONFIG}"
  cmake -DCMAKE_INSTALL_PREFIX=./install -DBUILD_TYPE="${CONFIG}" -P cmake_install.cmake
  # Run the unit tests
  ./src/libdreddtest/libdreddtest
popd

cp build/src/dredd/dredd third_party/clang+llvm/bin/
DREDD_REPO_ROOT=$(pwd)
export DREDD_REPO_ROOT
export PATH=${PATH}:${DREDD_REPO_ROOT}/scripts

case "$(uname)" in
  "Linux")

  if [[ "${CXX}" =~ ^clang.*  ]]; then
    # This test leads to Dredd producing code that the current version
    # of Clang on Linux cannot compile due to a Clang bug. The bug is
    # fixed in Clang 16, so when the GitHub runners have a suitably high
    # version of Clang they should be re-enabled.
    rm test/single_file/structured_binding.cc
    rm test/single_file/structured_binding.cc.expected
    rm test/single_file/structured_binding.cc.noopt.expected
  fi
	
  # The C++ code generated by Dredd may require C++20.
  export DREDD_EXTRA_CXX_ARGS="-std=c++20"
  export DREDD_EXTRA_C_ARGS="-std=c17"
  DREDD_SKIP_COPY_EXECUTABLE=1 ./scripts/check_single_file_tests.sh
  ;;

"Darwin"*)
  # On Mac, run the single-file tests
  SDKROOT=$(xcrun --show-sdk-path)
  export SDKROOT
  export CC=clang
  export CXX=clang++

  # The following single-file tests give different expected results on Mac
  # due to differences in how certain builtin types, such as size_t and
  # uint64_t, expand. For simplicity, remove them before running single-file
  # tests.
  rm test/single_file/initializer_list.cc
  rm test/single_file/initializer_list.cc.expected
  rm test/single_file/initializer_list.cc.noopt.expected
  rm test/single_file/add_type_aliases.c
  rm test/single_file/add_type_aliases.c.expected
  rm test/single_file/add_type_aliases.c.noopt.expected
  rm test/single_file/add_type_aliases.cc
  rm test/single_file/add_type_aliases.cc.expected
  rm test/single_file/add_type_aliases.cc.noopt.expected

  # This test leads to Dredd producing code that the current version
  # of Clang on Mac cannot compile due to a Clang bug. The bug is
  # fixed in Clang 16, so when the GitHub runners have a suitably high
  # version of Clang they should be re-enabled.
  rm test/single_file/structured_binding.cc
  rm test/single_file/structured_binding.cc.expected
  rm test/single_file/structured_binding.cc.noopt.expected
  
  # The C++ code generated by Dredd may require recent C/C++ support.
  export DREDD_EXTRA_CXX_ARGS="-std=c++20"
  export DREDD_EXTRA_C_ARGS="-std=c17"
  DREDD_SKIP_COPY_EXECUTABLE=1 ./scripts/check_single_file_tests.sh
  ;;

"MINGW"*|"MSYS_NT"*)
  # On Windows, run the single-file tests
  export CC=cl.exe
  export CXX=cl.exe

  # The following single-file tests give different expected results on Windows
  # due to differences in how certain builtin types, such as size_t and
  # uint64_t, expand. For simplicity, remove them before running single-file
  # tests.
  rm test/single_file/do_not_mutate_under_alignof.cc
  rm test/single_file/do_not_mutate_under_alignof.cc.expected
  rm test/single_file/do_not_mutate_under_alignof.cc.noopt.expected
  rm test/single_file/do_not_mutate_under_sizeof.cc
  rm test/single_file/do_not_mutate_under_sizeof.cc.expected
  rm test/single_file/do_not_mutate_under_sizeof.cc.noopt.expected
  rm test/single_file/initializer_list.cc
  rm test/single_file/initializer_list.cc.expected
  rm test/single_file/initializer_list.cc.noopt.expected
  rm test/single_file/add_type_aliases.c
  rm test/single_file/add_type_aliases.c.expected
  rm test/single_file/add_type_aliases.c.noopt.expected
  rm test/single_file/add_type_aliases.cc
  rm test/single_file/add_type_aliases.cc.expected
  rm test/single_file/add_type_aliases.cc.noopt.expected
  rm test/single_file/positive_int_as_minus_one.c
  rm test/single_file/positive_int_as_minus_one.c.expected
  rm test/single_file/positive_int_as_minus_one.c.noopt.expected
  rm test/single_file/positive_int_as_minus_one.cc
  rm test/single_file/positive_int_as_minus_one.cc.expected
  rm test/single_file/positive_int_as_minus_one.cc.noopt.expected

  # These tests rely on non-constant sized arrays, which the Microsoft compiler
  # does not support.
  rm test/single_file/non_const_sized_array.cc
  rm test/single_file/non_const_sized_array.cc.expected
  rm test/single_file/non_const_sized_array.cc.noopt.expected
  rm test/single_file/non_const_sized_array.c
  rm test/single_file/non_const_sized_array.c.expected
  rm test/single_file/non_const_sized_array.c.noopt.expected

  # These tests expose a difference in how value-dependent types are handled in
  # Windows vs. Linux builds of Clang. Not sure what's going on, so removing
  # the tests under Windows for simplicity.
  rm test/single_file/sizeof_template.cc
  rm test/single_file/sizeof_template.cc.expected
  rm test/single_file/sizeof_template.cc.noopt.expected
  rm test/single_file/sizeof_template2.cc
  rm test/single_file/sizeof_template2.cc.expected
  rm test/single_file/sizeof_template2.cc.noopt.expected
  
  # The C code generated by Dredd may require recent C/C++ support.
  export DREDD_EXTRA_C_ARGS="/std:c17"
  export DREDD_EXTRA_CXX_ARGS="/std:c++20"
  
  DREDD_SKIP_COPY_EXECUTABLE=1 ./scripts/check_single_file_tests.sh
  ;;

*)
  echo "Unknown OS"
  exit 1
  ;;
esac

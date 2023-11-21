# Dredd

Dredd is a tool that aims to enable [mutation testing and mutation
analysis](https://en.wikipedia.org/wiki/Mutation_testing) of large C++
code bases. Dredd is named after [Judge
Dredd](https://en.wikipedia.org/wiki/Judge_Dredd), because mutation
analysis can be used to judge the effectiveness of test suites, by
measuring test suite adequacy.

To apply Dredd to a C++ project, you must first generate a
[*compilation
database*](https://clang.llvm.org/docs/JSONCompilationDatabase.html)
for the project: a `compile_commands.json` file. Compilation databases
can be automatically generated by most modern build systems that
support C++ - [here is a great resource on compilation
databases](https://sarcasm.github.io/notes/dev/compilation-database.html).

You then point Dredd at (a) the C++ source files that you would like
to be mutated, and (b) the compilation database, which must provide
entries describing the compile commands that these source files
require.

Dredd will modify the source files in place, injecting a large number
of mutants. By default, each mutant is *disabled* and will have no
effect, so if you execute your project's executables or test suites in
the normal way they should behave normally (albeit running more slowly
due to the overhead of Dredd's instrumentation).

An environment variable, `DREDD_ENABLED_MUTATION`, can be used to
enable a mutant. Setting this environment variable to a non-negative
integer value $i$ will enable mutant number $i$.
Alternatively, a comma-separated list of non-negative integer values
can be used to enable multiple mutants, leading to a so-called
*higher-order mutant*.

In mutation testing parlance, rather than creating mutants, Dredd
creates a *mutant schema* or *meta-mutant*, simultaneously encoding
all the mutants that it *could* generate into the source code of the
program.

## Using Dredd

To get started, either

* [download the latest Dredd release](https://github.com/mc-imperial/dredd/releases/latest) and unzip at a location of your choice, or

* [build Dredd from source](#building-dredd-from-source) and copy the `dredd` executable from `build/src/dredd/` into `third_party/clang+llvm/bin`.

Set the `DREDD_CHECKOUT` environment variable to refer to the root of the directory into which Dredd is checked out, e.g.:

```
export DREDD_CHECKOUT=/path/to/dredd
```

Set the `DREDD_CLANG_BIN_DIR` environment variable to refer to the `bin` directory of the build of Clang/LLVM used by Dredd, e.g.:

```
export DREDD_CLANG_BIN_DIR=${DREDD_CHECKOUT}/third_party/clang+llvm/bin
```

Set the `DREDD_EXECUTABLE` environment variable to refer to the `dredd` executable at this location, e.g.:

```
export DREDD_EXECUTABLE=${DREDD_CLANG_BIN_DIR}/dredd
```

We first show how to apply Dredd to a simple stand-alone program. We will then show how to apply it to a larger C++ CMake project.

### Applying Dredd to a single file example

`pi.cc` is a small example in `examples/simple` that calculates an approximation of pi. To mutate this example, run the following from the root of the repository:

```
# This will modify pi.cc in-place.
${DREDD_EXECUTABLE} examples/simple/pi.cc
# Now compile the mutated version of the example
${DREDD_CLANG_BIN_DIR}/clang++ examples/simple/pi.cc -o examples/simple/pi
```

Due to the changes made by Dredd, the compile command may lead to warnings being issued but this is expected.  The changes to `pi.cc` can be viewed by running `git diff` although, these changes are not meant to be human-readable.

Running the executable with no additional arguments via `./examples/simple/pi` will produce the expected result of 3.14159.  You can specify which mutants you want to use at runtime by running:

```
DREDD_ENABLED_MUTATION=id ./examples/simple/pi
```

where $id$ is the id of the mutation in the program generated by Dredd. For example, running `DREDD_ENABLE_MUTATION=2 ./examples/simple/pi` will enable mutation number 2 in the modified program. 
This may print a different output to the original program and in some cases may not terminate. 
There is a possibility that equivalent mutants are generated which can lead to the printed value being unchanged.

You can also enable multiple mutants by setting the environment variable to a comma-separated list. For example, running `DREDD_ENABLE_MUTATION="0,2,4" ./examples/simple/pi` requests that mutants 0, 2 and 4 are enabled simultaneously.

To clean up and restore the file `pi.cc` to it's initial state, run
```
rm examples/simple/pi
git checkout HEAD examples/simple/pi.cc
```

### Applying Dredd to a CMake project

The `examples/math` directory contains a small math library to illustrate how to apply Dredd to a larger project.

First, make a copy of the code for this example.
Having a copy of the original, non-mutated code available allows information about the potential mutants that Dredd has introduced to be displayed, as explained further below.

```
cp -r examples/math examples/math-original
```

Compile the math library. It is important to use the Clang that ships with Dredd to compile the application. This is to ensure that, when it comes to mutating the application's code, any standard header files that the application includes will also be available to Dredd:

```
cd examples/math
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_C_COMPILER=${DREDD_CLANG_BIN_DIR}/clang -DCMAKE_CXX_COMPILER=${DREDD_CLANG_BIN_DIR}/clang++
cmake --build build
```

The `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` argument causes CMake to output a compilation database called `compile_comands.json` to the `build` directory.
You can read more about how to generate compilation databases using various build systems [here](https://sarcasm.github.io/notes/dev/compilation-database.html).

Check that the tests pass before mutations have been applied:

```
./build/mathtest/mathtest
```

All tests should pass.

To mutate all of the `.cc` files in the library use the following command:

```
${DREDD_EXECUTABLE} -p build math/src/*.cc --mutation-info-file mutant-info.json
```

The `-p` option allows the compilation database generated by CMake above to be passed to Dredd: the `compile_commands.json` file in `build` will be used as a compilation database.
This is so that Dredd knows the correct compiler options to use when processing each source file.

The optional `--mutation-info-file` argument is used to specify a JSON file to which Dredd will output
machine-readable information about the mutations it applied. We explain how the `mutant-info.json` file created
via the `--mutation-info-file` argument can be used to query the mutants that Dredd has introduced.

You can run `git status` to see which files have changed, and `git diff` to see
the effect that Dredd has had on these files. These changes will be hard to understand as they are not intended to be
readable by humans.

The project can then be recompiled with

```
cmake --build build
```

Due to the changes made by Dredd, it is likely that many warnings will be issued during compilation.
This is expected, because the mutants that Dredd introduces leads to code that violates many good
programming practices. If your project treats compiler warnings as errors then you will need to disable
this feature in your project's build configuration.

Running the test suite with:

```
./build/mathtest/mathtest
```

will result in all the tests passing as before, because no mutants are enabled. 
You can specify a mutant to apply at runtime by setting the `DREDD_ENABLED_MUTATION` environment variable to a non-negative integer value.

For example, running:

```
DREDD_ENABLED_MUTATION=1 ./build/mathtest/mathtest
```

will enable mutation number 1 in the modified library.
This may cause some tests to fail or in certain cases, not terminate. There is the possibility of equivalent mutants
being generated which can lead to all tests still passing.

You can also enable multiple mutants by setting `DREDD_ENABLED_MUTATION` to a comma-separated list of values. For example, running:

```
DREDD_ENABLED_MUTATION="0,2,4" ./build/mathtest/mathtest
```

will enable mutants 0, 2 and 4 in the modified library (which may lead to some tests failing and possibly to non-termination).

To learn about the mutants that Dredd has made available you can use the `query_mutant_info.py` under `scripts` in the Dredd repository.

To see how many mutants are available, do:

```
python3 ${DREDD_CHECKOUT}/scripts/query_mutant_info.py mutant-info.json --largest-mutant-id
```

This queries the `mutant-info.json` file that was generated by Dredd and shows the largest id among all mutants that were introduced.

To see the source code change to which a particular mutant, e.g. the mutant with id 10, corresponds, do:

```
python3 ${DREDD_CHECKOUT}/scripts/query_mutant_info.py mutant-info.json --show-info-for-mutant 10 --path-prefix-replacement ${DREDD_CHECKOUT}/examples/math ${DREDD_CHECKOUT}/examples/math-original
```

Notice that this takes the path to the original source code for the math library, under `math-original`, as well as the path to the mutated source code, under `math`. This is because `mutant-info.json` contains information about the original source code locations associated with mutants, but refers to filenames in the mutated source tree.
The `--show-info-for-mutant` query determines which part of which source file should be queried to provide a summary of the effect of the given mutant, and then replaces the path prefix to the mutated code with the path prefix of the original code in order to extract source code text from the original source code file.

To clean up the `examples/math` directory, return to the repository root and delete the copy of the non-mutated source code for this example, run the following:

```
rm mutant-info.json
rm -rf build
git checkout HEAD .
cd ../..
rm -rf examples/math-original
```

## Building Dredd from source

The following instructions have been tested on Ubuntu 22.04.

### Prerequisites

- curl (used to fetch a Clang/LLVM release)
- CMake version 3.13 or higher
- ninja version 1.10.0 or higher

### Clone the repository and get submodules

Either do:

```
git clone --recursive https://github.com/mc-imperial/dredd.git
```

or:

```
git clone --recursive git@github.com:mc-imperial/dredd.git
```

The `--recursive` flag ensures that submodules are fetched.

### Downloading Clang/LLVM

Dredd builds against various Clang and LLVM libraries. Rather than including Clang/LLVM as a submodule of Dredd and building it from source, Dredd assumes that a built release of Clang/LLVM has been downloaded into `third_party`.

From the root of the repository, execute the following commands:

```
cd third_party
# The release file is pretty large, so this download may take a while
curl -Lo clang+llvm.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-17.0.6/clang+llvm-17.0.6-x86_64-linux-gnu-ubuntu-22.04.tar.xz
tar xf clang+llvm.tar.xz
mv clang+llvm-17.0.6-x86_64-linux-gnu-ubuntu-22.04 clang+llvm
rm clang+llvm.tar.xz
cd ..
```

### Build steps

From the root of the repository, execute the following commands.
On Linux, you may change `Release` to `Debug` for a debug build; only a release build of Clang/LLVM is available, but linking against this with a Debug build of Dredd still works.

```
mkdir build && cd build
cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
cp src/dredd/dredd ../third_party/clang+llvm/bin
```

## Guide for developers

This project uses the [Google C++ style guide](https://google.github.io/styleguide/cppguide.html).

The following guide assumes that you are using Linux for development.

### Prerequisites

- Python
- cmake-format python package (used by `scripts/check_format`)
                                       
### Scripts Directory

The `scripts` directory contains a number of commands that are useful for developers. To make use of these commands,
you must first run `./dev_shell.sh.template` from the root of the Dredd repo. This will ensure that the necessary
environment variables are set as well as building tools that are used in other check commands.

The last four scripts in this section assume that a build Dredd is under `temp/build-Debug`, as they copy the Dredd
binary to `third_party/clang+llvm/bin` to execute the test files. Therefore, to use these scripts, 
one should ensure that `check_build.sh` has been executed at least up to the point where a debug build has finished.

The commands that can be run in isolation in the `scripts` directory are:
- `check_all.sh` : This runs a combination of the commands below to ensure that Dredd is formatted and functioning as expected.
- `check_build.sh` : This checks that dredd can be build and that the unit tests included in the `test` directory still pass
- `check_clang_tidy.sh` : This runs `clang-tidy` on Dredd's source files.
- `check_clean_build.sh` : This checks that Dredd can be build from scratch by deleting any previous builds first and running
the provided unit tests.
- `check_cmakelint.sh` : This runs cmake-lint on Dredd's cmake files. 
- `check_cpplint.sh` : Runs a static check to ensure the code style is correct.
- `check_format.sh` : This uses `clang-format` and `cmake-format` to ensure that all source files and cmake files `src`
and `examples` are formatted correctly.
- `check_headers.py` : This checks that the necessary files contain a copyright header.
- `fix_format.sh` : This command formats all the Dredd source files and example files using `clang-format` and formats
  the Dredd cmake files and example cmake files using `cmake-format`.
- `check_one_single_file_test.sh` :This will run Dredd on a specific `.c` or `.cc` file in the `tests/single_file` directory
  and ensure the output is the same as the respective `.expected` file.
- `check_single_file_tests.sh` : This acts the same as the above commands, but will compare the output from Dredd for all
`.c` and `.cc` files in the `test/single_file` directory.
- `regenerate_one_single_file_expectation.sh` : This takes in a `.c` or `.cc` file from the `test/single_file` directory and
regenerates its respective `.expected` file. You should only do this if you are certain the changes you have made to Dredd are correct.
- `regenerate_single_file_expectations` : This acts the same as the above command but will regenerate all the `.expected`
files for the `.c` and `.cc` files in the `test/single_file` file directory.

The auxiliary commands in the `scripts` directory are:
- `check_cppcheck.sh` : This runs a static check to detect bugs and undefined behaviour.
- `check_iwyu.sh` : This runs include what you use to check that all the necessary header files are included.

### Working on Dredd from CLion

To work on Dredd from CLion, open the `CMakeLists.txt` within the root of the Dredd repository.

## Planned features

See [planned feature
issues](https://github.com/mc-imperial/dredd/issues?q=is%3Aissue+is%3Aopen+label%3Aplanned-feature)
in the Dredd GitHub repository.

If you would like to see a feature added to Dredd, please [open an
issue](https://github.com/mc-imperial/dredd/issues) and we will
consider it.

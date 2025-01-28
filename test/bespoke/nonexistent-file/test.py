import os
import subprocess
import sys
from pathlib import Path

DREDD_REPO_ROOT = os.environ['DREDD_REPO_ROOT']
DREDD_INSTALLED_EXECUTABLE = Path(DREDD_REPO_ROOT, 'third_party', 'clang+llvm', 'bin', 'dredd')


def main():
    cmd = [DREDD_INSTALLED_EXECUTABLE, 'tomutate.c', '--']
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # We expect an error due to a non-existent file.
    assert result.returncode != 0
    stderr_output = result.stderr.decode('utf-8')
    # Unless Clang's diagnostics change, these are fragments of the error
    # messages that are expected:
    assert "no such file or directory" in stderr_output
    assert "no input files" in stderr_output
    assert "unable to handle compilation, expected exactly one compiler job" in stderr_output
    return 0


if __name__ == '__main__':
    sys.exit(main())

"""Run the modified generator, including with an isolated embedded Python."""
import argparse
import runpy
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('exefs', type=Path)
args = parser.parse_args()
sys.path.insert(0, str(root / 'tools'))
sys.argv = ['gen.py', 'all', str(args.exefs.resolve()), str(root / 'generated'),
            '--hle', str(root / 'runtime/hle/hle_symbols.txt'), '--simd', 'c']
runpy.run_path(str(root / 'tools/gen.py'), run_name='__main__')

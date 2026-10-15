"""One analysis path for the broad matrix, using the same raw validator as prior studies."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).parent
subprocess.run([sys.executable,str(root/'../topology/analyze.py'),str(root)],check=True)

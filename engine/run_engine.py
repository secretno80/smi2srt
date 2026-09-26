"""PyInstaller entry point for the subConverter conversion engine."""

import sys

from subconverter.cli import main

if __name__ == "__main__":
    sys.exit(main())

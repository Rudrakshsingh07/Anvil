"""Shared read-only state-bridge library used by the visualizer.

The editor itself is the C binary in ``build/anvil``; it does not import any
Python.  This package now only carries the JSON schema helpers and HTTP client
that the separate visualizer (and its tests) already depend on.
"""

__version__ = "0.2.0"

__all__ = ["__version__"]

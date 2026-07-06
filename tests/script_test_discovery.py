"""Small helpers for the repository's plain Python test scripts."""

from __future__ import annotations

from collections.abc import Callable


TestFunction = Callable[[], None]


def discovered_test_functions(namespace: dict[str, object]) -> list[tuple[str, TestFunction]]:
    return [
        (name, value)
        for name, value in namespace.items()
        if name.startswith("test_") and callable(value)
    ]


def run_discovered_tests(namespace: dict[str, object]) -> None:
    for _, test_func in discovered_test_functions(namespace):
        test_func()

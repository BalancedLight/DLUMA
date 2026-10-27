from __future__ import annotations

from dataclasses import dataclass, field

import pytest


@dataclass
class _Route:
    name: str
    mandatory: bool
    current: str = "original"
    enabled: bool = True
    installed: bool = False

    @property
    def routing(self) -> str:
        return f"loader:{self.name}"

    def apply(self, failure: str | None = None) -> bool:
        if not self.enabled or self.installed or self.current != "original":
            return False
        if failure == "before_write":
            return False
        self.current = self.routing
        self.installed = self.current == self.routing
        return failure != "after_write" and self.installed

    def restore(self, restored: list[str]) -> bool:
        if not self.installed:
            return True
        if self.current != self.routing:
            return False
        self.current = "original"
        self.installed = False
        restored.append(self.name)
        return True


@dataclass
class _HookTransaction:
    mandatory_names: tuple[str, ...] = (
        "transition",
        "filesystem",
        "load-pack",
        "free-pack",
        "release-packs",
    )
    optional_names: tuple[str, ...] = ("on-demand-evict", "script-cache")
    routes: list[_Route] = field(init=False)
    restored: list[str] = field(default_factory=list)
    state: str = "UnsupportedBuild"
    retained_for_process_exit: bool = False

    def __post_init__(self) -> None:
        self.routes = [
            *(_Route(name, True) for name in self.mandatory_names),
            *(_Route(name, False) for name in self.optional_names),
        ]

    def _restore(self, mandatory_only: bool) -> bool:
        succeeded = True
        for route in reversed(self.routes):
            if mandatory_only and not route.mandatory:
                continue
            if not route.restore(self.restored):
                succeeded = False
        if not succeeded:
            self.retained_for_process_exit = True
        return succeeded

    def install(
        self,
        failure_name: str | None = None,
        failure_phase: str | None = None,
    ) -> bool:
        for route in self.routes:
            if not route.mandatory:
                continue
            failure = failure_phase if route.name == failure_name else None
            if not route.apply(failure):
                self._restore(mandatory_only=True)
                self.state = "UnsupportedBuild"
                return False

        for route in self.routes:
            if route.mandatory:
                continue
            failure = failure_phase if route.name == failure_name else None
            if route.apply(failure):
                continue
            if route.installed and not route.restore(self.restored):
                self.retained_for_process_exit = True
                continue
            route.enabled = False

        self.state = "DiagnosticOnly"
        return True

    def shutdown(self) -> bool:
        return self._restore(mandatory_only=False)


class _FakeOriginal:
    def __init__(self, result: object) -> None:
        self.result = result
        self.calls: list[tuple[object, ...]] = []

    def __call__(self, *arguments: object) -> object:
        self.calls.append(arguments)
        return self.result


def _observing_wrapper(original: _FakeOriginal, *arguments: object) -> object:
    # This executable model mirrors the source-contract checks on every real
    # wrapper: observe before/after, forward once, and return the exact result.
    before_observation = tuple(arguments)
    result = original(*arguments)
    after_observation = (before_observation, result)
    assert after_observation[0] == arguments
    return result


@pytest.mark.parametrize(
    "failure_index",
    range(len(_HookTransaction.mandatory_names)),
)
def test_any_mandatory_pre_write_failure_rolls_back_without_partial_set(
    failure_index: int,
):
    transaction = _HookTransaction()
    failed_name = transaction.mandatory_names[failure_index]

    assert not transaction.install(failed_name, "before_write")
    assert transaction.state == "UnsupportedBuild"
    assert all(route.current == "original" for route in transaction.routes)
    assert not any(route.installed for route in transaction.routes)
    assert transaction.restored == list(
        reversed(transaction.mandatory_names[:failure_index])
    )


@pytest.mark.parametrize(
    "failure_index",
    range(len(_HookTransaction.mandatory_names)),
)
def test_any_mandatory_post_write_failure_includes_failed_route_in_rollback(
    failure_index: int,
):
    transaction = _HookTransaction()
    failed_name = transaction.mandatory_names[failure_index]

    assert not transaction.install(failed_name, "after_write")
    assert transaction.state == "UnsupportedBuild"
    assert all(route.current == "original" for route in transaction.routes)
    assert not any(route.installed for route in transaction.routes)
    assert transaction.restored == list(
        reversed(transaction.mandatory_names[: failure_index + 1])
    )


@pytest.mark.parametrize("failure_phase", ["before_write", "after_write"])
def test_optional_failure_keeps_complete_mandatory_set_diagnostic_only(
    failure_phase: str,
):
    transaction = _HookTransaction()
    failed_optional = transaction.optional_names[0]

    assert transaction.install(failed_optional, failure_phase)
    assert transaction.state == "DiagnosticOnly"
    mandatory = [route for route in transaction.routes if route.mandatory]
    assert all(route.installed and route.current == route.routing for route in mandatory)
    optional = next(route for route in transaction.routes if route.name == failed_optional)
    assert not optional.installed
    assert not optional.enabled


def test_successful_shutdown_restores_strict_reverse_installation_order():
    transaction = _HookTransaction()

    assert transaction.install()
    installed_order = [route.name for route in transaction.routes]
    assert transaction.shutdown()
    assert transaction.restored == list(reversed(installed_order))
    assert all(route.current == "original" for route in transaction.routes)


def test_foreign_routing_is_not_overwritten_and_requires_retention():
    transaction = _HookTransaction()
    assert transaction.install()
    foreign = transaction.routes[2]
    foreign.current = "foreign-component"

    assert not transaction.shutdown()
    assert foreign.current == "foreign-component"
    assert foreign.installed
    assert transaction.retained_for_process_exit
    assert all(
        route.current == "original" for route in transaction.routes if route is not foreign
    )
    expected_restored = [
        route.name for route in reversed(transaction.routes) if route is not foreign
    ]
    assert transaction.restored == expected_restored


@pytest.mark.parametrize("result", [False, True, -0x1234, 0x12345678, None])
def test_observation_wrapper_forwards_once_and_preserves_exact_result(result: object):
    original = _FakeOriginal(result)
    arguments = (object(), "watched/path.scr", 7)

    observed = _observing_wrapper(original, *arguments)

    assert original.calls == [arguments]
    assert observed is result

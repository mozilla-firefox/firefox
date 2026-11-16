import mozunit
import pytest
from moztest.outcome import Outcome


@pytest.mark.parametrize(
    "record,expected",
    (
        pytest.param({"status": "PASS"}, Outcome.EXPECTED, id="no_expected"),
        pytest.param({"status": "FAIL"}, Outcome.EXPECTED, id="expected_fail"),
        pytest.param({"status": "SKIP"}, Outcome.EXPECTED, id="skip"),
        pytest.param(
            {"status": "FAIL", "expected": "FAIL"},
            Outcome.EXPECTED,
            id="expected_equal_to_status",
        ),
        pytest.param(
            {"status": "FAIL", "expected": "PASS"},
            Outcome.UNEXPECTED,
            id="unexpected_fail",
        ),
        pytest.param(
            {"status": "PASS", "expected": "FAIL"},
            Outcome.UNEXPECTED,
            id="unexpected_pass",
        ),
        pytest.param(
            {"status": "OK", "expected": "ERROR"},
            Outcome.UNEXPECTED,
            id="unexpected_ok",
        ),
        pytest.param(
            {"status": "FAIL", "expected": "PASS", "known_intermittent": ["FAIL"]},
            Outcome.KNOWN_INTERMITTENT,
            id="known_intermittent",
        ),
        pytest.param(
            {"status": "TIMEOUT", "expected": "PASS", "known_intermittent": ["FAIL"]},
            Outcome.UNEXPECTED,
            id="other_known_intermittent",
        ),
        pytest.param(
            {"status": "FAIL", "expected": "PASS", "known_intermittent": []},
            Outcome.UNEXPECTED,
            id="empty_known_intermittent",
        ),
        pytest.param(
            {"status": "FAIL", "will_retry": True},
            Outcome.WILL_RETRY,
            id="will_retry",
        ),
        pytest.param(
            {"status": "FAIL", "expected": "PASS", "will_retry": True},
            Outcome.WILL_RETRY,
            id="will_retry_with_expected",
        ),
        pytest.param(
            {"status": "FAIL", "expected": "PASS", "will_retry": False},
            Outcome.UNEXPECTED,
            id="will_retry_false",
        ),
    ),
)
def test_from_log(record, expected):
    assert Outcome.from_log(record) is expected


def test_severity_order():
    assert list(Outcome) == [
        Outcome.EXPECTED,
        Outcome.KNOWN_INTERMITTENT,
        Outcome.WILL_RETRY,
        Outcome.UNEXPECTED,
    ]
    assert (
        max([Outcome.KNOWN_INTERMITTENT, Outcome.WILL_RETRY, Outcome.EXPECTED])
        is Outcome.WILL_RETRY
    )
    assert max([Outcome.UNEXPECTED, Outcome.WILL_RETRY]) is Outcome.UNEXPECTED


if __name__ == "__main__":
    mozunit.main()

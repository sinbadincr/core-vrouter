"""SC-005 (spec 004): every harness e2e test is classified in docs/traceability.md, and every
guest-showable one names an existing KVM counterpart or a reason. No root, no KVM."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLASSES = {"crafted-frame", "forwarder-only", "guest-showable"}
HEADING = "## Harness tests (namespace VMs)"


def harness_tests():
    found = []
    for f in sorted((ROOT / "tests" / "e2e").glob("test_*.py")):
        for m in re.finditer(r"^def (test_\w+)\(", f.read_text(), re.M):
            found.append(f"{f.name}::{m.group(1)}")
    return found


def table_rows():
    text = (ROOT / "docs" / "traceability.md").read_text()
    assert HEADING in text, f"missing section {HEADING!r}"
    section = text.split(HEADING, 1)[1]
    section = re.split(r"^## ", section, maxsplit=1, flags=re.M)[0]
    rows = []
    for line in section.splitlines():
        if not line.startswith("|") or line.startswith("|---") or line.startswith("| test "):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        assert len(cells) == 3, f"bad row: {line}"
        rows.append(cells)
    return rows


def kvm_functions():
    names = set()
    for f in (ROOT / "tests" / "e2e_kvm").glob("test_*.py"):
        names.update(re.findall(r"^def (test_\w+)\(", f.read_text(), re.M))
    return names


class Traceability(unittest.TestCase):
    def test_every_harness_test_is_classified_exactly_once(self):
        listed = [c[0].strip("`") for c in table_rows()]
        for t in harness_tests():
            self.assertEqual(listed.count(t), 1, f"{t} must appear exactly once in the Harness tests table")
        self.assertEqual(sorted(set(listed) - set(harness_tests())), [],
                         "table names a test that does not exist")

    def test_classes_are_known(self):
        for test, cls, _ in table_rows():
            self.assertIn(cls.strip("`"), CLASSES, test)

    def test_guest_showable_has_counterpart_or_reason(self):
        kvm = kvm_functions()
        for test, cls, note in table_rows():
            if cls.strip("`") != "guest-showable":
                continue
            self.assertTrue(note, f"{test}: guest-showable needs a KVM counterpart or a reason")
            named = re.findall(r"tests/e2e_kvm/[\w.]+::(test_\w+)|`(?:test_kvm_\w+\.py)::(test_\w+)`", note)
            for a, b in named:
                self.assertIn(a or b, kvm, f"{test}: KVM counterpart {a or b} does not exist")


if __name__ == "__main__":
    unittest.main()

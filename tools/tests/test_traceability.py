"""Tests for the requirement trace checker (traceability.py).

Two jobs here. Most of these build a synthetic tree and prove the checker actually reports each
kind of rot - a checker only ever run against a passing repo has not been shown to catch
anything. The last one runs it against the real document, which is what makes REQ-VV-004 hold.
"""

from pathlib import Path

from traceability import check, find_paths, parse_requirements, split_artifacts

REPO_ROOT = Path(__file__).resolve().parents[2]

# assembled rather than written out, because this file sits inside the tree the backward scan
# walks: a literal id here would be reported as one cited in code but missing from the document.
# which is the checker working - the fixtures just must not trip it
REAL = "REQ-" + "AAA-001"
MISSING = "REQ-" + "ZZZ-999"


def write_doc(tmp_path: Path, body: str) -> Path:
    doc = tmp_path / "requirements.md"
    doc.write_text(body, encoding="utf-8")
    return doc


def planned_doc(tmp_path: Path) -> Path:
    return write_doc(tmp_path, f"**{REAL}** - a statement.  \n**Status**: planned  \n")


# --- parsing ---


def test_parses_a_block_into_its_fields(tmp_path):
    doc = write_doc(
        tmp_path,
        f"**{REAL}** - a statement.  \n"
        "**Type**: Functional  \n"
        "**Status**: unit-verified  \n"
        "**Verification**: unit test  \n"
        "**Artifact**: fsw/test/x.cpp\n",
    )
    (req,) = parse_requirements(doc)
    assert req.req_id == REAL
    assert req.status == "unit-verified"
    assert req.claims_verification
    assert req.artifacts == ["fsw/test/x.cpp"]


def test_planned_requirements_have_no_artifact_and_claim_nothing(tmp_path):
    (req,) = parse_requirements(planned_doc(tmp_path))
    assert not req.claims_verification
    assert req.artifacts == []


def test_a_parenthetical_comma_does_not_split_one_artifact_in_two():
    # "dual UART (USART2 console, USART6 downlink); LoRa transport" is two artifacts, not three
    assert split_artifacts("dual UART (USART2 console, USART6 downlink); LoRa transport") == [
        "dual UART (USART2 console, USART6 downlink)",
        "LoRa transport",
    ]


def test_a_path_inside_a_parenthetical_is_still_checked(tmp_path):
    # the note is where several requirements actually name their files; discarding it would
    # quietly stop checking them
    doc = write_doc(
        tmp_path,
        f"**{REAL}** - a statement.  \n"
        "**Status**: unit-verified  \n"
        "**Artifact**: backends (SIL in fsw/sil/gone.cpp, host in fsw/platform/host/gone.cpp)\n",
    )
    violations, _, _ = check(root=tmp_path, requirements=doc)
    assert len(violations) == 2
    assert all("artifact not found" in v for v in violations)


def test_paths_are_found_inside_prose():
    # the entry is a sentence; the file in it is still the part worth checking
    assert find_paths("the task table in obc/Inc/rtos_tasks.h") == ["obc/Inc/rtos_tasks.h"]
    assert find_paths("`cfg 0` in obc/Inc/FreeRTOSConfig.h") == ["obc/Inc/FreeRTOSConfig.h"]
    assert find_paths("a, b") == []
    assert find_paths("this document") == []


# --- the forward direction ---


def test_a_missing_artifact_file_is_a_violation(tmp_path):
    doc = write_doc(
        tmp_path,
        f"**{REAL}** - a statement.  \n"
        "**Status**: unit-verified  \n"
        "**Artifact**: fsw/test/deleted.cpp\n",
    )
    violations, _, _ = check(root=tmp_path, requirements=doc)
    assert any("artifact not found" in v and "deleted.cpp" in v for v in violations)


def test_an_existing_artifact_file_is_not_a_violation(tmp_path):
    (tmp_path / "fsw" / "test").mkdir(parents=True)
    (tmp_path / "fsw" / "test" / "present.cpp").write_text("", encoding="utf-8")
    doc = write_doc(
        tmp_path,
        f"**{REAL}** - a statement.  \n"
        "**Status**: unit-verified  \n"
        "**Artifact**: fsw/test/present.cpp\n",
    )
    violations, _, _ = check(root=tmp_path, requirements=doc)
    assert violations == []


def test_claiming_verification_without_naming_an_artifact_is_a_violation(tmp_path):
    doc = write_doc(tmp_path, f"**{REAL}** - a statement.  \n**Status**: bench-verified  \n")
    violations, _, _ = check(root=tmp_path, requirements=doc)
    assert any("names no artifact" in v for v in violations)


def test_a_planned_requirement_owes_no_artifact(tmp_path):
    violations, _, _ = check(root=tmp_path, requirements=planned_doc(tmp_path))
    assert violations == []


def test_prose_artifacts_are_reported_unchecked_rather_than_failed(tmp_path):
    doc = write_doc(
        tmp_path,
        f"**{REAL}** - a statement.  \n**Status**: unit-verified  \n**Artifact**: this document\n",
    )
    violations, unchecked, _ = check(root=tmp_path, requirements=doc)
    assert violations == []
    assert any("this document" in u for u in unchecked)


# --- the backward direction ---


def test_a_req_id_cited_in_code_but_absent_from_the_document_is_a_violation(tmp_path):
    (tmp_path / "fsw").mkdir()
    (tmp_path / "fsw" / "thing.cpp").write_text(f"// {MISSING}\n", encoding="utf-8")
    violations, _, _ = check(root=tmp_path, requirements=planned_doc(tmp_path))
    assert any(MISSING in v and "not in requirements.md" in v for v in violations)


def test_a_req_id_cited_in_code_and_present_in_the_document_is_fine(tmp_path):
    (tmp_path / "fsw").mkdir()
    (tmp_path / "fsw" / "thing.cpp").write_text(f"// {REAL}\n", encoding="utf-8")
    violations, _, _ = check(root=tmp_path, requirements=planned_doc(tmp_path))
    assert violations == []


def test_vendored_code_is_not_scanned_for_req_ids(tmp_path):
    # vendored sources are not ours to annotate, and doctest.h is full of unrelated text
    (tmp_path / "fsw" / "third_party").mkdir(parents=True)
    (tmp_path / "fsw" / "third_party" / "vendored.hpp").write_text(
        f"// {MISSING}\n", encoding="utf-8"
    )
    violations, _, _ = check(root=tmp_path, requirements=planned_doc(tmp_path))
    assert violations == []


# --- the live document ---


def test_the_repository_trace_is_current():
    """REQ-VV-004 itself: every claimed artifact exists, every cited id is real."""
    violations, _, reqs = check()
    assert reqs, "no requirements parsed - the document format changed"
    assert violations == [], "\n".join(violations)

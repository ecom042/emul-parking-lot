"""Cross-check inner assessment with official Robot lifecycle evidence."""

import xml.etree.ElementTree as ET


def infrastructure_error(directory, expected):
    """Return an explanation, or None when Robot agrees with the assessment."""
    files = list(directory.rglob("robot_output.xml"))
    if len(files) != 1:
        return "expected exactly one official robot_output.xml"
    try:
        root = ET.parse(files[0]).getroot()
    except (OSError, ET.ParseError) as error:
        return f"invalid official Robot XML: {error}"
    for keyword in root.iter("kw"):
        if keyword.get("type", "").upper() in ("SETUP", "TEARDOWN"):
            status = keyword.find("status")
            if status is None or status.get("status") != "PASS":
                return f'official Robot {keyword.get("type")} failed: {keyword.get("name")}'
    for errors in root.iter("errors"):
        if any(message.get("level") == "ERROR" for message in errors.iter("msg")):
            return "official Robot recorded infrastructure errors"
    tests = list(root.iter("test"))
    if len(tests) != 1:
        return "expected exactly one scenario test in official Robot XML"
    test = tests[0]
    status = test.find("status")
    wanted = "FAIL" if expected == "firmware_failure" else "PASS"
    if status is None or status.get("status") != wanted:
        return "official Robot test status disagrees with assessment"
    if expected == "firmware_failure" and "firmware_failure:" not in (status.text or ""):
        return "official Robot failure was not the reported firmware assertion"
    body = [kw for kw in test.findall("kw") if kw.get("type", "").upper() not in
            ("SETUP", "TEARDOWN")]
    if len(body) != 1 or body[0].get("name") != "Execute Scenario":
        return "unexpected official scenario test body"
    return None

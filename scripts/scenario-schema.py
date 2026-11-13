"""Emit the version-1 JSON Schema without optional host dependencies."""

import json
from scenario.schema import schema

if __name__ == "__main__":
    print(json.dumps(schema(), indent=2))

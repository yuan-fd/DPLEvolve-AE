# Calibration Schemas

JSON schemas in this directory describe tracked Level 1 calibration records.

- `calibration_manifest.schema.json`: one frozen calibration run manifest.
- `mechanism_evidence.schema.json`: reviewed mechanism evidence.
- `source_start.schema.json`: tracked source-start provenance.

Schemas should use portable ids and environment-variable path conventions; do
not encode machine-local absolute paths.

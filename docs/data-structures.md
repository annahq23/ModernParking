# Data structures

- Fixed array of 10 slot states: direct indexed slot status.
- `Vehicle` and `Payment` structs: group related fields.
- `vector<Vehicle>` and `vector<Payment>` are used in the earlier in-memory teaching version; this web implementation persists records in SQLite tables.
- Linear search is demonstrated in the in-memory version; the web backend looks up active vehicles with a SQL query.

# Database design

SQLite tables: `slots`, `visits`, `payments`. `visits.slot_id` references `slots.id`; `payments.visit_id` references `visits.id`. Completed visit history and successful payment history are retained. The database file `parking.db` is created automatically at runtime.

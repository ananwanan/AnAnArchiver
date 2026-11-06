# LiteArc Flutter desktop frontend

Runnable Home, bounded archive browser (F3), extraction and creation (F4).
Password retry, task-scoped conflict answers, actual progress and cancellation
use the native task service. The archive implementation is the
existing C++ Application/Core, accessed through `packages/litearc_binding` on a
persistent isolate. No Qt runtime is used.

See [development and build instructions](../../docs/development/flutter.md) and
[Windows validation, scale measurements and remaining platforms](../../docs/validation/2026-11-06-flutter-browsing.md).
Operation evidence is in [F4 validation](../../docs/validation/2026-11-06-flutter-operations.md).

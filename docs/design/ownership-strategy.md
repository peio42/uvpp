# Ownership and lifetime

## Native storage

All native handles remain address-stable through close completion. Existing
low-level wrappers own inline native storage, disable copying/moving, and require
explicit close without a destructor fallback. These wrappers are still awaiting
the complete `uv::raw` namespace migration.

Implemented high-level TCP/pipe connections/listeners, UDP sockets, and files
move ownership of stable storage. Their native addresses and callback reconstruction
remain valid after a move. Named native accessors borrow; they do not transfer
close authority or permit replacing an operation's callback slots.

## Close and destruction

The owners expose awaitable `close()`, initiating `request_close()`, and explicit
`uv::ops::close(owner)`. They share one internal close transition and retain storage
through the native callback. Connection/UDP active borrowed I/O rejects close with
`UV_EBUSY`; listener close quiesces its pending accept before native close.
An exported TCP connection also remains pinned during IPC write completion.

Direct-owner destruction initiates asynchronous close when preconditions hold.
Destroying owners with incompatible active I/O, or listeners with pending accept,
is a terminating violation. Destructors never drive the loop. The application
retains and drives the loop until all native close callbacks finish.

`uv::fs::file` is a request-based exception: `uv_fs_close` has a completion
status. Submission failure leaves its descriptor open. After a native close
completion, even an error completion, the descriptor identity is terminal and
cannot be retried safely. Direct destruction diagnoses an unclosed file rather
than starting unobserved request cleanup.

## Tasks, views, and bytes

Cold tasks acquire a loop at spawn or child await. A `spawn_handle<T>` retains a
root frame and supports same-loop join; destruction while active currently
terminates. Task scopes own child execution and must settle before destruction.
The proposed active-handle retention fallback is not implemented.

Borrowed views never become another resource owner. Scope views retain validity
bookkeeping, not native lifetime; cleanup invalidates them before releasing storage.
Coroutine frames and operation state do not retain external payloads. Writes/sends
borrow bytes unchanged through actual completion; reads borrow mutable storage.
Cancellation alone releases neither the slot nor the bytes.

The high-level DNS resolver has no persistent owner. Its one-shot coroutine
awaiter owns a stable `uv_getaddrinfo_t`, copied node/service/`resolve_options`, and
the native result list until its terminal callback. That callback releases the
task-stop registration before resuming the task; only then may frame destruction
free the native list. A failed `uv_cancel` leaves this storage intact until the
ordinary completion callback.

## Low-level implementation boundaries

Caller-owned requests must survive native completion and cleanup. Existing
`uv::fs::raw::request` requires explicit request cleanup; the v3 namespace target
is `uv::raw::fs`. Filesystem convenience operation state may own a request and
result without owning the opened file. `file_descriptor` remains a copyable
integer wrapper, not an asynchronous file owner. Raw directory handles require
explicit close. Do not extend network scope behavior to those resources without
a dedicated lifetime contract.

Native `data` is a non-owning application pointer. Typed getters do not perform
runtime type checking. `loop_view` and walked `handle_view` values likewise borrow;
wrapper recovery is valid only for the exact low-level wrapper representation,
never for foreign handles or high-level native state.

## Experimental v3 resource-scope cleanup

`uv::co::resource_scope` owns TCP/pipe connections and listeners, UDP sockets,
and files.
Its cold `finish()` task borrows the scope. Construction has no state effect;
startup checks loop affinity before transitioning from `open` or `interrupted`
to `active`. Starting cleanup seals adoption permanently. An overlapping attempt
throws without changing the active attempt; `finished` is idempotent after the
same affinity check.

Cleanup runs serially, listeners before dependent resources. Each successful
record invalidates borrowed access after native close completion, releases owner
storage, and is immediately retired. Empty slots in the record vector preserve
ordering without repeated vector erasure and are skipped on retry. The vector is
cleared on full success (`finished`). Any exception during the cleanup pass,
including record-task or waiter allocation failure, sets `interrupted` and is
propagated. Remaining records retain ownership; completed cleanup is not rolled
back. A retry joins an already-started native close through the existing close
protocol rather than submitting it twice.

Active incompatible borrowed I/O is a recoverable `std::logic_error`: the caller
must settle/join that work before retrying. Listener accept retains its distinct
quiescence protocol. No blanket cancellation, background cleanup, or nested loop
is introduced. Destruction with remaining resources terminates, including after
an observed cleanup failure. Merely creating an unstarted finish task does not
satisfy asynchronous exit, and that task must not outlive the scope it borrows.

Current handle close has no native completion error. Files use `uv_fs_close`:
their scope record releases the terminal owner before propagating a completion
error, so a retry only retires that record and never repeats close. Cleanup is
fail-fast, with no aggregate-error type; callers preserve primary task failures
separately. Generic cleanup-error aggregation remains proposed in
[011](../proposals/011-resource-scopes.md).

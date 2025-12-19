# Analysis of Kernel Oops Message

The command `echo "hello_world" > /dev/faulty` causes a kernel oops due to a NULL pointer dereference.

The oops message shows:

* A Data Abort in kernel mode
* Faulting address `0x0000000000000000`
* A **write operation** (`WnR = 1`)
* Crash location at `faulty_write+0x10/0x20`

This indicates that the crash occurred early inside the driver’s `faulty_write()` function. The register state (`x0 = 0x0`) and the faulting instruction (`STR`) confirm that the driver attempted to write through a NULL pointer.

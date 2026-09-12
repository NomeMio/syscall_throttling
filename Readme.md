

# SysThrot

# Requirements : 
The module was  tested on 6.18.18-0 lts alpine and debian 13 with kernel 6.12 lts, but should work on any x86_64 kernel >= 6.4, this is due module some module initialization and cleanup functions that were changed, and due some timer functions names that were changed.
---


A Linux kernel module that monitors and **throttles system calls** made by selected
users or programs. The module hooks the chosen syscalls in the kernel text, counts how
many times they are invoked during an **epoch** (a time window, 1 second by default),
and **blocks** the callers that exceed the configured quota until the next epoch begins.
The quota is shared beetween all registered users/syscall/programs, so if the limit is 5 calls per epoch, the first 5 calls from any combination of registered users/programs will go through, and the rest will be blocked until the next epoch.
---

## Table of contents

- [Features](#features)
- [How it works](#how-it-works)
- [Repository layout](#repository-layout)
- [Syscall hooking](#syscall-hooking)
- [The stub and the trampoline](#the-stub-and-the-trampoline)
- [Throttling logic](#throttling-logic)
- [Epoch and timer](#epoch-and-timer)
- [Data storage](#data-storage)
- [Wait queue](#wait-queue)
- [Logging](#logging)
- [Statistics monitor](#statistics-monitor)
- [Public interface](#public-interface)
- [Build and usage](#build-and-usage)
- [User-space tools](#user-space-tools)
- [Known limitations](#known-limitations)

---

## Features

- Select the syscalls to throttle (add/remove at runtime).
- Select the *entities* that are subject to throttling, by:
  - **user** (UID), and/or
  - **program** (executable name).
- Configure the **maximum number of calls per epoch** (module parameter
  `max_calls_monitor`).
- **Turn throttling on/off** at runtime without unloading the module.
- Per-syscall **statistics** exposed through `/proc/SYSTHROT`.
- All memberships are exposed through `/dev/sysThrot_dev`.
- O(1) membership checks thanks to Cuckoo hash stores protected by RCU.
- All management operations are performed through **ioctls** and are restricted to
  **root**.

---

## How it works

```
                userspace                  kernel
  ┌─────────────────────────┐   ┌──────────────────────────────────────┐
  │   systhrot (CLI tool)   │──▶│  /dev/sysThrot_dev (char device)     │
  │                         │   │   ├── ioctls (admin commands)        │
  │   /proc/SYSTHROT        │◀──│   └── read (config + status)         │
  └─────────────────────────┘   └───────────────┬──────────────────────┘
                                               │ patch (CALL rel32)
  a syscall (e.g. __x64_sys_getpid) ◀──────────┘
        │ 5 leading NOPs replaced
        ▼
  stub_trampoline (asm) ──▶ stub() (C)
                                │ 1. not working?            → allow
                                │ 2. not a monitored entity? → allow
                                │ 3. token available?        → allow
                                │ 4. otherwise               → block in wait queue
                                └──────────▶  return 0      → syscall runs
                                             return -EAGAIN → syscall is skipped,
                                                              caller gets EAGAIN
```

Only syscalls that are explicitly registered are patched, so every other syscall runs
with **zero overhead**. A *small* per-call overhead exists for monitored syscalls, even for unmonitored callers.

---

## Repository layout

| Path | Purpose |
|------|---------|
| `Makefile` | Build/load/remove the module, generate the syscall table |
| `sysThrot.c` | Core: init/cleanup, char-device driver, stub, trampoline, syscall hooking, timer |
| `sysThrot_ioctl.c` | ioctl dispatch and the user/program/syscall register wrappers |
| `sysThrot_log.c` | `printk` wrapper used by the whole module |
| `sysThrot_queue.c` | FIFO wait queue for threads blocked by the monitor |
| `sysThrot_store.c` | Cuckoo-hash stores for monitored users and programs |
| `sysThrot_statmonitor.c` | Per-syscall statistics and the `/proc/SYSTHROT` file |
| `lib/sysThrot.h` | Main header: driver struct, log levels, memory-protection helpers |
| `lib/sysThrot_ioctl.h` | ioctl command definitions |
| `lib/sysThrot_store.h` | Store API |
| `lib/sysThrot_queue.h` | Queue API |
| `lib/sysThrot_statmonitor.h` | Statistics data structures |
| `lib/syscalls.h` | **Generated** mapping `nr → __x64_sys_*` symbol |
| `user/systhrot.c` | CLI tool to drive the module |
| `user/client.c` | Multi-threaded stress/benchmark tool |
| `user/lib/sysThrot.h` | User-space API (ioctl wrappers) |
| `user/lib/syscalls.h` | **Generated** copy of the syscall mapping |

---

## Syscall hooking

When a syscall is registered and it is not already hooked, the module resolves its
address and patches the beginning of its `x86_64` wrapper (`__x64_sys_*`). Hooking the
**wrapper** (instead of the raw syscall handler) is preferred because:

1. the wrapper names are always present in `/proc/kallsyms`.
2. the wrappers are always probe-able with **kprobes**.

### Steps

1. **Resolve the address** — the wrapper symbol is resolved by registering a temporary
   kprobe and reading `kp.addr`.
2. **Patch the code** — the wrapper is compiled with **5 NOP bytes** at its entry
   (reserved for debugging/probing). The module replaces those 5 bytes with a
   `CALL rel32` (opcode `0xE8`) that jumps to `stub_trampoline`, a function written
   directly in assembly.
3. **Run or bypass the syscall** — `stub_trampoline` saves the registers used by the
   syscall calling convention, calls the C function `stub()`:
   - if `stub()` returns `0`, control returns into the wrapper and the syscall runs
     normally;
   - if `stub()` returns a non-zero error, the trampoline **skips the syscall body**:
     it drops the wrapper's return address and returns the error (e.g. `-EAGAIN`)
     straight to userspace.

### Patching methods

Two patching strategies are available, selected at compile time with the
`USE_TEXT_POKE_SINGLE` macro (`1` by default):

1. **Kernel text poking** (`smp_text_poke_single`) — the safe, concurrency-aware
   mechanism the kernel itself uses. The module resolves `smp_text_poke_single` and the
   `text_mutex` at runtime via `kallsyms_lookup_name` (also resolved through a kprobe),
   because these symbols are not always exported in the headers.
2. **Classic method** — disables write-protection (`CR0.WP`, and `CR4.CET` if
   enabled), `memcpy`s the instructions, re-enables protection, and flushes with the
   required memory barriers.

### Unhooking

To remove a hook, the 5 bytes are restored with a multi-byte NOP
(`0F 1F 44 00 00`), patching with any other instruction like 5 NOPs (`90 90 90 90 90`) would lead to **undefined behavior** once the module is unloaded and reloaded because the probing system does a check for the multi-byte NOP.

The resolved addresses are cached in `syscall_addresses[]` and the installed hooks are
tracked in a **bitmap** (`syscall_presence_bitmap`). Re-arming a previously hooked
syscall reuses the cached address and **avoids re-probing with kprobes** (which have a
non-trivial overhead).

---

## The stub and the trampoline

`stub_trampoline` is a small assembly routine that:

```
push the registers clobbered by the calling convention
call stub
save stub's return value (rbx)
pop the registers back
if return value == 0  → ret                 (continue the wrapper, syscall runs)
else                  → rax = error, drop the wrapper return address, ret
                       (syscall body is skipped, error returned to user)
```

Register preservation is critical: if the dirty registers are not saved/restored the
whole kernel crashes.

---

## Throttling logic

The `stub()` function implements the actual monitor (the trampoline already accounted
the thread in `sysThrot_stub_counter`, see above):

1. If the monitor is **off** → leave the stub (no throttling).
2. `atomic_inc(threads_in_module)` — tracks threads inside the throttling logic
   (used to drain before turning off / unloading).
3. **Membership check** — the caller is throttled only if its `comm` matches a
   registered program **or** its UID is a registered user. Otherwise it leaves the
   stub immediately.
4. **Token check** — `atomic_dec_return(current_epoch_tokens)`:
   - `>= 0` → allowed to run;
   - `< 0` → quota exceeded, the thread is **blocked**.
5. **Blocking** — the thread is added to the FIFO wait queue and sleeps until the next
  epoch (or until the monitor is turned off). If queue-entry memory allocation fails,
  or if the thread wakes up because of a **signal or interrupt**, the syscall is
  aborted and `-EAGAIN` is returned to userspace.
6. When a blocked thread is finally allowed to run, its blocking time is recorded in
   the statistics.

```
┌─────────────┐     ┌───────────────┐     ┌───────────────┐     ┌─────────────────────┐
│ monitor on? ├─no─▶│   allow (0)   │     │ registered?   ├─no─▶│   allow (0)         │
└──────┬──────┘     └───────────────┘     └──────┬────────┘     └─────────────────────┘
       │yes                                       │yes
       ▼                                          ▼
┌─────────────┐     ┌────────────────┐     ┌───────────────┐
│ token >= 0? ├─yes▶│   allow (0)    │     │   block       │
└──────┬──────┘     └────────────────┘     │ wait next epoch│
       │no                                 └───────────────┘
       ▼
  return -EAGAIN (syscall skipped)  if allocation fails or woken by signal
```

---

## Epoch and timer

A kernel timer fires every epoch (`EPOCH_DURATION_MS`, **1000 ms**) and the callback:

1. wakes up the first `max_syscalls_for_epoch` blocked threads (`unqueue(n)`), i.e. the
   ones that receive a token;
2. wakes up the whole wait queue (`wake_up_interruptible`);
3. updates the per-epoch statistics;
4. increments the epoch counter;
5. resets `current_epoch_tokens` to `max_syscalls_for_epoch - woken_threads`;
6. re-arms itself (while throttling is on).

---

## Data storage

The set of monitored users and programs is stored in two **Cuckoo hash** tables
(2 tables of 128 buckets each, with two independent hash functions). This gives:

- **O(1)** membership checks (the common hot path in the stub);
- copy-on-write updates: writers clone the map, apply the change, atomically swap the
  pointer (`rcu_assign_pointer`) and free the old map after `synchronize_rcu()`;
- **RCU**-based lock-free reads for the stub path.

Even though ioctls are serialized, RCU is still used so that a writer can never expose
an inconsistent map to concurrent readers. A pathological insertion could in theory hit
many "kicks" (bounded by `MAX_KICKS = 64`); the code returns `-ENOSPC` in that case and
future work could rehash with different hash functions or grow the tables.

---

## Wait queue

Blocked threads are parked in a **FIFO wait queue**:

- each element carries a `wake_up_flag` and an `exited` marker;
- elements are allocated from a dedicated `kmem_cache`;
- the epoch callback grants tokens to the **oldest** waiters first (fairness);
- granted waiters are moved to a "junk list" and freed once they set their `exited`
  flag (the free happens safely after they wake);
- when the monitor is turned off, all waiters are woken (`wake_up_queue`) so they can
  exit the stub.

---

## Logging

Logging is a thin wrapper around `printk` (`sysThrot_log`). Each message has a
**bitmask level** (core, ioctl, admin, stats, stub, store, queue, …). A single
`LOG_LEVEL` bitmask in `lib/sysThrot.h` selects which levels are actually compiled in,
so only the wanted messages appear in `dmesg`.

---

## Statistics monitor

Every time a syscall is blocked, the monitor records:

- blocked calls (cumulative and per epoch);
- mean and **peak** delay, with the **PID / UID / command** that suffered the peak;
- waits **aborted** by a signal/interrupt or queue allocation failure (those that return
  `-EAGAIN`);
- wait-condition evaluations (context switches while waiting), accumulated and peaked
  per epoch.

The data is kept in a per-syscall linked list, refreshed every epoch, and exposed
through `/proc/SYSTHROT` (read-only, root only). Each row shows the syscall **name**,
a **rolling window** (last 20 epochs) for the `AVG/EPOCH` and mean-delay columns, and
the context-switch stats per epoch:

```
=== Syscall Throttling Statistics ===
  epochs elapsed: 45   window: 20 epochs   epoch duration: 1000 ms
    NR SYSCALL                    TOTAL      LAST  AVG/EPOCH   MEAN(ms)   PEAK(ms)   ABORT  CTX/EPOCH  CTX PEAK  PEAK PROC (PID:UID)
  ---------------------------------------------------------------------------------------------------------------------
     2 __x64_sys_open               340       12        17       1.02       4.83       31       210        402  sysThrot (1523:1000)
```

All columns are cumulative since module load, except `LAST` (last completed epoch)
and `AVG/EPOCH` (mean over the window), which are explicitly labeled.

---

## Public interface

### ioctl commands

All commands go through `/dev/sysThrot_dev` and **require root** (uid 0). The ioctl
magic number is `-`.

| Command | Macro | Argument | Description |
|---------|-------|----------|-------------|
| 1 | `sysThrot_IOC_REGISTER_USER` | `int*` (UID) | add a user to the monitor |
| 2 | `sysThrot_IOC_DEREGISTER_USER` | `int*` (UID) | remove a user |
| 3 | `sysThrot_IOC_REGISTER_PROGRAM` | `char*` (name) | add a program to the monitor |
| 4 | `sysThrot_IOC_DEREGISTER_PROGRAM` | `char*` (name) | remove a program |
| 5 | `sysThrot_IOC_REGISTER_SYSCALL` | `int*` (nr) | hook a syscall |
| 6 | `sysThrot_IOC_DEREGISTER_SYSCALL` | `int*` (nr) | unhook a syscall |
| 7 | `sysThrot_IOC_TURN_ON` | — | enable throttling |
| 8 | `sysThrot_IOC_TURN_OFF` | — | disable throttling |

The ioctl handlers and the register/deregister logic live in `sysThrot_ioctl.c`, the
command definitions in `lib/sysThrot_ioctl.h`.

### Pseudo-files

| File | Content |
|------|---------|
| `/dev/sysThrot_dev` | char device; `read` → current status and configuration (monitor on/off, max calls per epoch, registered users, programs and syscalls); `ioctl` → admin commands. Only one opener at a time (subsequent opens get `-EBUSY`). |
| `/proc/SYSTHROT` | per-syscall statistics table (read-only). |

---

## Build and usage

The module is compiled against the headers of the currently running kernel
(`/lib/modules/$(uname -r)/build`). All targets must be run as **root**.

```sh
make          # generate lib/syscalls.h and build sysThrot_module.ko
make load     # insmod sysThrot_module.ko max_calls_monitor=5
make remove   # rmmod sysThrot_module
make clean    # remove build artifacts
```

The `max_calls_monitor` module parameter sets how many monitored calls are allowed per
epoch. With `0` (default), every monitored call is blocked immediately.

Check it is working:

```sh
cat /dev/sysThrot_dev   # configuration / status
cat /proc/SYSTHROT      # statistics
dmesg | grep SYSTHROT   # module log
```

### Generating the syscall table

At build time the Makefile (`load_sys_calls` + `generate_syscalls_header`) produces
`lib/syscalls.h` and `user/lib/syscalls.h`, an array mapping each syscall number to its
`__x64_sys_*` symbol. It does so by:

1. extracting every `__x64_sys_*` symbol from `/proc/kallsyms`;
2. matching them against the `__NR_*` numbers in `asm/unistd_64.h`;
3. emitting `syscall_symbols[SUPPORTED_SYSCALLS]`.

The kernel module uses this table to know which symbol to kprobe for a given syscall
number; the user-space tools use it to translate a symbol name into a syscall number.

---

## User-space tools

Build them from `user/`:

```sh
cd user
make            # builds ./systhrot and ./client
```

### `systhrot` — admin CLI

```sh
./systhrot register-user <uid>
./systhrot deregister-user <uid>
./systhrot register-program <name>
./systhrot deregister-program <name>
./systhrot register-syscall __x64_sys_getpid
./systhrot deregister-syscall __x64_sys_getpid
./systhrot on
./systhrot off
```

### `client` — stress/benchmark tool

```sh
./client -t 16 -d 5 --setup --teardown -k 2
```

Spawns `-t` worker threads that hammer the selected syscalls for `-d` seconds,
impersonating the configured program names, and reports:

- how many calls went through (`OK`) vs. how many were blocked (`EAGAIN`);
- aggregate throughput (calls/s, throttled/s);
- a per-syscall `OK / EAGAIN` breakdown;
- the `/proc/SYSTHROT` dump.
- `-k <time>` send a kill signal to all threads after `<time>` seconds (to test signal handling).

Useful flags: `-s __x64_sys_getpid,__x64_sys_getuid` to pick syscalls, `-p name1,name2`
to pick program names, `--setup` to register everything and turn throttling on,
`--teardown` to undo it all.
- `-k <time>` send a kill signal to all threads after `<time>` seconds (to test signal handling).

An example run:

```sh
./client -t 15 -d 4 -s __x64_sys_getpid -p p1 -k 5
```

Runs 15 threads every seacond each one calling one time `getpid` impersonating the program `p1`, for 4 seconds, and sends a kill signal to all threads after 5 seconds. So in total 60 calls are mande with ~25 calls that go trough and ~35 calls that are blocked and return `EAGAIN` because the limit is 5 calls per epoch.
---

## Known limitations

Thera are some optimizations that can be made, expicially in the stub path, to reduce the overhead of monitored syscalls in both memory usage and concurrency performance:
  - For memory optimizations i could remove or at least reduce the memory footprint of the array with the syscalls strings, and make the list of monitored syscalls a linked list instead of an array, so that the memory footprint is proportional to the number of monitored syscalls and not to the total number of syscalls in the kernel, and access is not O(1) but the speed is not important where its used.
  - For concurrency, the numbers of locks could be reduced, and a lock-free queue could be implemented for the wait queue, so that the threads that are waiting for a token do not block each other when they are added to the queue.




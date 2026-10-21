# Contributing to libevent

## Coding style

First and most generic rule: **just look around**.

But, we have a script for checking patches/files/git-refs:
```shell
# Chech HEAD git ref
./checkpatch.sh -r
./checkpatch.sh -r HEAD

# Check patch
git format-patch --stdout -1 | ./checkpatch.sh -p
git show -1 | ./checkpatch.sh -p

# Or via regular files
git format-patch --stdout -2
./checkpatch.sh *.patch

# Over a file
./checkpatch.sh -d event.c
./checkpatch.sh -d < event.c

# And print the whole file not only summary
./checkpatch.sh -f event.c
./checkpatch.sh -f < event.c

# See
./checkpatch.sh -h
```

## Testing
- Write new unit test in `test/regress_{MORE_SUITABLE_FOR_YOU}.c`
- `make verify`

`test-allocfail` runs deterministic allocation-failure scenarios for buffers and
timers. Each scenario first counts allocations in a successful control, then
replays it with each allocation failing in turn. Setup and cleanup run without
injection; the scenarios check preserved state and recovery after failure.
Successful buffer expansion must permit an allocation-free append. Timers
scheduled before a failed heap growth must expire through the event loop
without being rescheduled. This suite covers allocations reached by the
successful control; it does not provide a probabilistic workload or cover
additional allocations reached only on error paths.

Both CMake's `verify` target and Autotools' `make verify` run this executable
directly. When built with sanitizers, failures at process exit propagate to
the test runner. Builds without
memory replacement skip it. To run it alone with CMake, use
`ctest --test-dir build -R '^test-allocfail$' --output-on-failure` after building.

## Asking Questions
It's all right to have questions! Instead of filing an issue here, go ahead and leave a message in our [Gitter Room](https://gitter.im/libevent/libevent)

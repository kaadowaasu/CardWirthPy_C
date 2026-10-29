# Berkeley SoftFloat Release 3e

This directory contains the unmodified `source/` tree and `COPYING.txt` from
John R. Hauser's official SoftFloat Release 3e archive:

- Source: <https://www.jhauser.us/arithmetic/SoftFloat-3e.zip>
- Archive SHA-256: `21130CE885D35C1FE73FC1E1BF2244178167E05C6747CAD5F450CC991714C746`
- Upstream release documentation: <https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html>
- License: BSD 3-Clause; see `COPYING.txt` and the headers retained in `source/`.

`build/cardwirth/platform.h` is the only CardWirth platform configuration. It
detects target byte order, defines `LITTLEENDIAN` only for little-endian
targets, leaves it undefined for known big-endian targets, and rejects targets
whose byte order is unknown. It also maps SoftFloat's thread-local state to the
compiler's C or C++ TLS primitive. The common Real80 wrapper sets the
arithmetic policy and selects one NaN specialization for every target.

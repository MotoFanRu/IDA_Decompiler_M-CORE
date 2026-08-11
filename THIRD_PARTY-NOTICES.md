# Third-party notices

## IDA Pro and IDA SDK

This plugin targets IDA Pro 9.4 and its built-in Motorola MCORE processor
module. IDA Pro is proprietary Hex-Rays software and is not redistributed by
this repository.

Builds use the official [HexRaysSA/ida-sdk](https://github.com/HexRaysSA/ida-sdk)
at tag `v9.4.0-release`. The SDK is obtained separately and remains under the
license published in that repository; no SDK files are copied into this tree.

## Historical M·CORE processor module

Earlier revisions used the community
[M-CORE_IDA-Pro](https://github.com/MotoFanRu/M-CORE_IDA-Pro) processor module
for instruction identifiers and disassembly. The current code has no required
build-time or runtime dependency on that module and does not include its headers
or sources. It accepts the legacy `M*CORE` processor name only so existing IDA
databases remain usable when their original module is installed separately.

Historical attribution: the original “IDA MCORE Plugin” was written by
`rshade@hushmail.com` (2004–2005) and later ported to modern IDA by
[@usernameak](https://github.com/usernameak) and MotoFan.Ru contributors.

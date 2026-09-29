# Third-party notices

ZarGUI itself is licensed under the MIT License (see `LICENSE`). It includes or
is built with the following third-party software.

## ZArchive

<https://github.com/Exzap/ZArchive> (commit `965b66c8d67b6b7e30fd63b3b75aa91a99ff303b`),
statically linked. Licensed under MIT No Attribution:

```
MIT No Attribution

Copyright 2022 Exzap

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.```

## Zstandard (zstd)

<https://github.com/facebook/zstd> (version 1.5.7), statically linked. zstd is
dual-licensed under BSD-3-Clause and GPL-2.0; ZarGUI uses it under the BSD-3-Clause license:

```
BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name Facebook, nor Meta, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Microsoft components (Windows download only)

The Windows zip is self-contained and includes Microsoft runtime files. These are **not** covered by
ZarGUI's MIT license; they remain under Microsoft's terms, included in `licenses/` and in the Windows zip:

- **.NET runtime** (MIT License, <https://github.com/dotnet/runtime>):
  `dotnet-runtime-LICENSE.txt`, `dotnet-runtime-THIRD-PARTY-NOTICES.txt`, `dotnet-windowsdesktop-LICENSE.txt`
- **Windows App SDK** (proprietary Microsoft Software License Terms, <https://github.com/microsoft/WindowsAppSDK>):
  `WindowsAppSDK-LICENSE.txt`, `WindowsAppSDK-NOTICE.txt`. Use of these files is subject to those terms.

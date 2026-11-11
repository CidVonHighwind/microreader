# Third-party code

## Xteink X3 display support

The X3 display driver (`platforms/esp32/epd_x3.h`) — the UC8253 and UC8279
refresh flows, their waveform tables (LUTs), the controller init sequences and
the controller probe — is ported from:

- **PapyriX** (https://github.com/bigbag/papyrix-reader), MIT License,
  Copyright (c) 2026 Pavel Liashkov
- **FreeInk SDK** waveform data and stock-protocol probe, as vendored by
  PapyriX (`lib/EInkDisplay/FREEINK_LICENSE`), MIT License,
  Copyright (c) 2026 FreeInk

MIT License text for both:

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The X3 hardware details (pins, battery gauge, SD power switch) also follow
PapyriX's board profiles and an X3 display-driver analysis by CrazyCoder
(https://gist.github.com/CrazyCoder/82fec0bbd0e515dcc237d3db7451ec6f).

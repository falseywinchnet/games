# CONV for offline assets

Plan Paint's CONV resampler (`falseywinchnet/plan-paint`, `src/conv.cpp` at `a9592570695d`, MIT),
used wherever PlaySuite downscales an image offline. Along each axis it builds a quintic
Bezier profile between the samples, projects its control "currents" so their signs follow
the data's own rises and falls (so it never overshoots, and never rings), and integrates
that profile exactly over each target pixel's area (`conv_resize_area`).

Use it in linear light on premultiplied colour: decode sRGB to linear, multiply by alpha,
resize, divide by alpha, encode, and round only at the end. Runtime scaling in the games
keeps its own faster methods.

```sh
c++ -std=c++20 -O2 authoring/conv/conv.cpp authoring/conv/conv_tool.cpp -o .build/conv_tool
.build/conv_tool IN.f64 W H TW TH OUT.f64 [1|4]
```

`IN.f64` is little-endian doubles, row-major: one channel, or four (premultiplied RGBA).
`conv.py` wraps it for Python: `resize_area(array, width, height)` and
`resize_srgb_image(PIL image, width, height)` (linear, premultiplied, rounded at the end).

# Material Web reference renderer

This fixture renders real `@material/web` 2.5.0 custom elements in Chromium. It is a **test fixture**, never shipped or embedded in the Android application.

```sh
cd tests/md3_web
npm ci
npx playwright install --with-deps chromium
npm run capture
```

For a preinstalled compatible Chromium, set `CHROMIUM_PATH=/path/to/chromium`. Set `MD3_REFERENCE_OUT=/absolute/path` to override the default `docs/images/md3` output folder.

The capture writes ten full light/dark pages, individual component crops, and `web-reference-manifest.json` with the actual browser version and crop bounds. Browser console errors fail the capture. Components finish their Lit updates and fonts finish loading before screenshots. Device scale factor is one; viewport is 1000 × 1000 and full-page screenshots retain content extending below the viewport. Hover is parked outside the samples. Menu and dialog hosts use `display: contents`; the capture targets their visible shadow-root `div.menu` / `dialog[open]` surfaces. Missing surfaces fail with the host/open state, computed styles, bounds, and a diagnostic screenshot instead of silently skipping a component.

`fixture.js` follows the sample texts, values and explicitly supplied sizes in `md3_showcase.cpp` (range 20–70; tabs Overview/Activity/Settings; Fruit Apple/Banana/Cherry; progress 40%/75%); `style.css` sets explicit theme colors, Roboto and only outer layout widths. Component internals are upstream implementations. Cards, navigation bar, segmented buttons and badges use upstream **experimental labs** components. Icons use `+` or letters intentionally so comparisons do not mix icon fonts. FABs explicitly use the upstream `variant="primary"` option to match the native primary-container variant; Switches use upstream `icons` to match the native selected checkmark.

The fixture does not invent web versions of components absent from this npm package. See [reference and coverage notes](../../docs/MD3_REFERENCE.md).

Native screenshot fonts are vendored in `fonts/` with the upstream OFL-1.1 license. They were losslessly decompressed from `@fontsource/roboto` 5.2.8 latin WOFF using fontTools (`TTFont(...); font.flavor = None; font.save(...)`). No outline, metrics or naming changes were made. Use weight 400 for body and 500 for labels.

| File | SHA-256 |
| --- | --- |
| Original `roboto-latin-400-normal.woff` | `901ccd1d83ba9d19e2ba904964da569a3c61c6d652bd39d1ba2f86f26936e8c9` |
| Original `roboto-latin-500-normal.woff` | `5bca83bcb66718b34465473c3af02832b1367e8fc99406510a9924b9afeffbad` |
| `fonts/Roboto-400.ttf` | `ae5676deb6b3e57e2856bead5fbc0905e01eda2398b03af98939938c4dcc112c` |
| `fonts/Roboto-500.ttf` | `98189dadd0e2f265c8ef0fde75e7e320dbb4c0babde3ed1e6baf33ea20c2d910` |

Material Web is Apache-2.0. No upstream code is copied into the fixture source; dependencies are pinned by `package-lock.json`.

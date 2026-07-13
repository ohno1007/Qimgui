# live2d/models — drop a Cubism model here

Place one Cubism 3+ model directory here, e.g.:

```
live2d/models/
└── Hiyori/
    ├── Hiyori.model3.json
    ├── Hiyori.moc3
    ├── Hiyori.physics3.json      (optional)
    ├── Hiyori.pose3.json         (optional)
    ├── Hiyori.2048/              (textures, *.png)
    ├── expressions/*.exp3.json   (optional)
    └── motions/*.motion3.json    (optional)
```

Free sample models ship inside the Cubism SDK package
(`Samples/Resources/`) and on Live2D's site — mind each model's license.

The model directory is pushed to the device and its path passed to the app.
By default the app looks for it under `/data/local/tmp/live2d/<Model>/` — see
[docs/LIVE2D.md](../../docs/LIVE2D.md). Model files are **not** committed
(gitignored) unless you intend to and the license allows it.

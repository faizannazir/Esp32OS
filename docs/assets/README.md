# Documentation visuals

[Documentation](../README.md) · [Dashboard guide](../DASHBOARD.md)

These assets belong to the ESP32OS documentation and use the repository's [MIT License](../../LICENSE). No third-party board photographs, stock illustrations or component artwork are included.

## Assets and provenance

| Asset | Source and meaning |
|---|---|
| `esp32os-hero.svg` | Original vector banner showing interfaces, services and the ESP-IDF/FreeRTOS foundation; a conceptual diagram, not a board photograph |
| `esp32os-architecture.svg` | Original vector responsibility map checked against component headers, startup code and the partition table |
| `dashboard-desktop.png` | Chromium capture of the actual dashboard at 1440 px width, full page |
| `dashboard-mobile.png` | Chromium viewport capture of the actual dashboard at 390 × 900 px; shows the mobile overview, not the entire page |
| `dashboard-settings.png` | Chromium capture of the actual settings dialog at desktop width |
| `dashboard-sample-data.json` | Sample JSON responses used to populate the dashboard captures, keyed by API path |

The dashboard screenshots render [`components/webportal/dashboard/index.html`](../../components/webportal/dashboard/index.html) from this checkout with default dark/mint/comfortable preferences. They use intercepted API responses, not a physical device. Heap, CPU, uptime, process state and network addresses are illustrative. No hardware measurements or production credentials appear in them. The “Device connected” label is the interface's response to sample API data, not evidence of a connected board.

The previous hand-drawn dashboard previews have been replaced with these captures so the documentation reflects the bundled interface.

## Refreshing screenshots

1. Serve the repository on loopback only, for example with `python3 -m http.server 8765 --bind 127.0.0.1` from its root.
2. Open `/components/webportal/dashboard/index.html` in an isolated browser profile with browser automation such as Playwright.
3. Before navigation, intercept the four GET API paths in `dashboard-sample-data.json` and return the matching JSON values. Do not connect the capture browser to a real device or send control requests.
4. Clear this origin's dashboard preferences and use the default appearance. Wait for the status, process row and 24 telemetry samples to render.
5. Capture desktop at 1440 px width. Open **Settings** and capture the dialog. Reload at 390 × 900 px and capture the mobile viewport.
6. Check each resulting image for clipping, duplicated capture regions and stale information. Keep the adjacent sample-data captions when updating the docs.
7. Stop the local server and close the capture session when finished.

Browser font rendering and the refresh timestamp may vary. Use viewport captures if full-page capture introduces stitching artifacts. Do not repaint or invent UI elements to hide a limitation in the actual dashboard.

## Maintaining diagrams

Keep text in SVG format for crisp scaling and include a descriptive `<title>` and `<desc>`. Check the diagram at README width and on a narrow screen. When changing capabilities or defaults, update the diagram and its surrounding explanation together. The architecture map groups responsibilities; it does not claim strict dependency isolation or separate process memory.

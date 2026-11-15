# Documentation visuals

[Documentation](../README.md) · [Dashboard guide](../DASHBOARD.md)

These assets belong to the ESP32OS documentation and use the repository's [MIT License](../../LICENSE). No third-party board photographs, stock illustrations or component artwork are included.

## Assets and provenance

| Asset | Source |
|---|---|
| `esp32os-hero.svg` | Original vector banner for the project |
| `esp32os-architecture.svg` | Original diagram of firmware responsibilities |
| `usb-uart-connection.svg` | USB data cable and UART0 setup diagram |
| `dashboard-live-desktop.jpg` | Safari showing live heap and task metrics from the board |
| `dashboard-live-terminal.jpg` | Safari showing the board’s response to `uname -a` |
| `dashboard-live-settings.jpg` | Safari showing the board’s widget settings |

The live JPEGs were captured on November 15, 2026, from ESP32OS running on an ESP32-D0WD-V3 revision 3.1 with 4 MB flash. The browser connected to the board over Wi-Fi. The overview shows the bench device's local IP. Captures exclude the Wi-Fi screen because it displays the network name and nearby networks.

The older dashboard PNGs and `dashboard-sample-data.json` document the previous layout and use sample responses. The README and Dashboard guide use the live JPEGs.

To refresh the live images, enable the portal on an isolated network, open the board’s station IP, and wait for telemetry to load. Capture the overview, a successful command response and settings. Inspect the images for clipping and private network names before adding them to the repository.

## Maintaining diagrams

Keep text in SVG format for crisp scaling and include a descriptive `<title>` and `<desc>`. Check the diagram at README width and on a narrow screen. When changing capabilities or defaults, update the diagram and its surrounding explanation together. The architecture map groups responsibilities; it does not claim strict dependency isolation or separate process memory.

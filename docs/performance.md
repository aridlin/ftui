# FTHT Performance Notes

FTHT is aimed at small ESP32-style control panels, so the default path is biased toward low request count, small update payloads, and no required runtime dependencies.

## Optimizations In This Build

- Full document is sent only on normal page load.
- Fetch-driven widget updates receive only a `<main>` fragment instead of the complete CSS/JS document.
- Background polling sends only `_ftht_poll=1`, not the whole form.
- Browser debug logging is off by default.
- Server debug logging is off by default.
- Sliders update locally while dragging and sync to C++ on release/change.

## Local Benchmark

Environment:

- Windows desktop loopback
- `g++ 13.2.0`
- `-O2 -DNDEBUG`
- Demo: `examples/device_panel.cpp`
- Command helper: `tools/bench_http.py`

Commands:

```bash
g++ examples/device_panel.cpp -o build/ftht_device_panel.exe -std=c++17 -O2 -DNDEBUG -lws2_32
python tools/bench_http.py http://127.0.0.1:8080/ -n 200 --method GET
python tools/bench_http.py http://127.0.0.1:8080/ -n 200 --method POST --body "_ftht_poll=1" --ftht-client
```

Results:

| Path | Avg bytes | Avg latency | P50 | P95 |
| --- | ---: | ---: | ---: | ---: |
| Full page GET | 13,898.6 B | 11.437 ms | 15.039 ms | 15.831 ms |
| FTHT partial poll POST | 1,140.5 B | 11.139 ms | 15.034 ms | 15.831 ms |

Payload reduction for polling/async updates: about 91.8% versus a full page response.

Browser-side update sample with `FTHT_DEMO_CLIENT_DEBUG` enabled:

| Metric | Value |
| --- | ---: |
| Updates observed | 98 |
| Avg total update | 8.25 ms |
| Max total update | 25.3 ms |
| Avg fetch | 6.88 ms |
| Avg DOM parse/swap | 1.37 ms |

These are loopback numbers, not ESP32 numbers. They are still useful because payload size and DOM swap cost are the parts FTHT controls directly. On ESP32, network and Wi-Fi latency will dominate, so keeping partial responses near 1 KB matters more than shaving fractions of a millisecond from desktop loopback.

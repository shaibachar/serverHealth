# serverHealth

A C++ microservice that exposes a web dashboard on **port 9090** showing live health indicators of your Ubuntu home server.

## Health Indicators

| Indicator | Source |
|-----------|--------|
| CPU usage & idle % | `/proc/stat` |
| Memory usage | `/proc/meminfo` |
| Disk space per mount | `statvfs()` |
| Disk I/O stats | `/proc/diskstats` |
| Network RX/TX | `/proc/net/dev` |
| CPU / SoC temperature | `/sys/class/thermal/` |

## Quick Start (Docker Compose)

```bash
docker compose up --build -d
```

Open <http://localhost:9090> in your browser.  The dashboard auto-refreshes every 5 seconds.

## API

| Endpoint | Description |
|----------|-------------|
| `GET /` | Web dashboard |
| `GET /api/health` | JSON health metrics |

## Build Locally (without Docker)

Requirements: `cmake ≥ 3.14`, `g++ ≥ 9`, internet access (to pull cpp-httplib).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run (reads /proc and /sys directly)
./build/serverhealth
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PROC_PATH` | `/proc` | Path to the proc filesystem |
| `SYS_PATH` | `/sys` | Path to the sys filesystem |
| `PORT` | `9090` | Listening port |
| `WEB_ROOT` | `/usr/share/serverhealth` | Directory containing `index.html` |

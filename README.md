# serverHealth

A C++ microservice that exposes a web dashboard on **port 9091** showing live health indicators of your Ubuntu home server.

## Health Indicators

| Indicator | Source |
|-----------|--------|
| CPU usage & idle % | Host `/proc/stat` (mounted into container) |
| Memory usage | Host `/proc/meminfo` (mounted into container) |
| Disk space per mount | Host `statvfs()` via mounted host root (`/host/root`) |
| Disk I/O stats | Host `/proc/diskstats` (mounted into container) |
| Network RX/TX | Host `/proc/net/dev` (mounted into container) |
| CPU / SoC temperature | Host `/sys/class/thermal/` (mounted into container) |
| Docker containers | Host Docker daemon via `/var/run/docker.sock` |

## Quick Start (Docker Compose)

```bash
docker compose up --build -d
```

Open <http://localhost:9091> in your browser. The dashboard auto-refreshes every 5 seconds.

### Host-level metrics (important)

The Docker Compose setup is configured to collect metrics from the **Ubuntu host machine** (the machine running Docker), not from the container namespace only.

It does this by mounting:

- `/proc` -> `/host/proc` (host CPU/memory/network/disk I/O)
- `/sys` -> `/host/sys` (host thermal sensors)
- `/` -> `/host/root` (host filesystem `statvfs()` disk usage)
- `/var/run/docker.sock` -> `/var/run/docker.sock` (host Docker containers via `docker ps`)

If you run without these mounts, some metrics will reflect the container instead of the host.

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

# Run (reads local machine /proc and /sys directly)
./build/serverhealth
```

## Start On Boot (systemd + Docker Compose)

`docker-compose.yml` already uses `restart: unless-stopped`, which restarts the container when Docker starts. To ensure the stack starts when the server boots, enable Docker and create a `systemd` unit for this Compose project.

### 1. Ensure Docker starts on boot

```bash
sudo systemctl enable docker
sudo systemctl start docker
```

### 2. Create a systemd service for this project

Create `/etc/systemd/system/serverhealth.service`:

```ini
[Unit]
Description=serverHealth Docker Compose stack
Requires=docker.service
After=docker.service network-online.target
Wants=network-online.target

[Service]
Type=oneshot
WorkingDirectory=/path/to/serverHealth
ExecStart=/usr/bin/docker compose up -d --build
ExecStop=/usr/bin/docker compose down
RemainAfterExit=yes
TimeoutStartSec=0

[Install]
WantedBy=multi-user.target
```

Replace `/path/to/serverHealth` with the actual project directory on your Ubuntu server.

### 3. Enable and start the service

```bash
sudo systemctl daemon-reload
sudo systemctl enable serverhealth.service
sudo systemctl start serverhealth.service
```

### 4. Check status

```bash
sudo systemctl status serverhealth.service
docker compose ps
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `PROC_PATH` | `/proc` | Path to the proc filesystem |
| `SYS_PATH` | `/sys` | Path to the sys filesystem |
| `HOST_ROOT_PATH` | (empty) | Optional host root mount prefix used for host disk `statvfs()` lookups |
| `PORT` | `9091` | Listening port |
| `WEB_ROOT` | `/usr/share/serverhealth` | Directory containing `index.html` |

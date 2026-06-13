# Watering — MQTT + Prometheus monitoring stack

Runs on the Raspberry Pi (ARM64; all images are multi-arch). Three containers:

| Container        | Image                               | Port  | Role                                   |
|------------------|-------------------------------------|-------|----------------------------------------|
| `mosquitto`      | `eclipse-mosquitto:2`               | 1883  | MQTT broker; ESP32-S3 publishes here   |
| `mqtt2prometheus`| `ghcr.io/hikhvar/mqtt2prometheus`   | 9641  | Translates MQTT → Prometheus `/metrics`|
| `mqtt-explorer`  | `smeagolworms4/mqtt-explorer`       | 4000  | Browser MQTT viewer + live value graphs|

```
ESP32-S3 --MQTT--> mosquitto:1883 --sub--> mqtt2prometheus:9641 <--scrape-- Prometheus
                          \--sub--> mqtt-explorer:4000 (browser, live graphs)
```

## Deploy

```bash
# on the Raspberry Pi, in this directory
docker compose up -d
docker compose logs -f
```

## The contract (device ⇄ exporter)

**Topic:** `watering/<device_name>/moisture`

**Payload:** JSON — moisture keys for configured pins, plus both relay states:
```json
{ "s0": 2731, "s1": 2540, "s2": 2600, "r0": 0, "r1": 1 }
```
- `s0` → GPIO4, `s1` → GPIO5, `s2` → GPIO6; value = raw 12-bit ADC, `0..4095`
  (no calibration applied on-device — see note)
- `r0` → GPIO38, `r1` → GPIO39; value = relay/pump state, `1`=on `0`=off (always both)
- Published every `<interval>` s **and** immediately on any relay change, so a short
  pump run (default 10 s) is not missed by the periodic grid.

**Exposed metrics** (sample at `http://<pi>:9641/metrics`):
```
soil_moisture_raw{sensor="garden-node1", channel="0"} 2731
soil_moisture_raw{sensor="garden-node1", channel="1"} 2540
soil_moisture_raw{sensor="garden-node1", channel="2"} 2600
relay_on{sensor="garden-node1", relay="0"} 0
relay_on{sensor="garden-node1", relay="1"} 1
```
`sensor` is the `<device_name>` from the topic. Add more ESP32 nodes → more `sensor` values, no config change.

## Prometheus scrape config (add on your Prometheus server later)

```yaml
scrape_configs:
  - job_name: watering
    scrape_interval: 30s
    static_configs:
      - targets: ["<raspberry-pi-ip>:9641"]
    # Optional: rename the exporter's default `sensor` label to `device`
    metric_relabel_configs:
      - source_labels: [sensor]
        target_label: device
      - regex: sensor
        action: labeldrop
```

## Quick test without the ESP32

```bash
# publish a fake reading
docker compose exec mosquitto \
  mosquitto_pub -t watering/garden-node1/moisture -m '{"s0":2731,"s1":2540,"s2":2600,"r0":0,"r1":1}'

# watch the broker
docker compose exec mosquitto mosquitto_sub -t 'watering/#' -v

# confirm it surfaced
curl -s localhost:9641/metrics | grep -E 'soil_moisture_raw|relay_on'
```

## Live graphs in the browser (MQTT Explorer)

A quick, history-free way to *watch* values arrive — no Prometheus query, no Grafana.

1. Open `http://<raspberry-pi-ip>:4000`.
2. Add a connection (saved to the `mqtt-explorer-config` volume, so you do this once):
   - **Host:** `mosquitto`  ·  **Port:** `1883`  ·  **Protocol:** `mqtt://`
   - (`mosquitto` resolves over the compose network — that's why it's the host, not the Pi's IP.)
3. Connect, expand `watering/<device>/moisture`. The JSON is parsed into a tree; click a
   numeric leaf (`s0`, `r0`, …) and the right pane draws a live chart that updates on each
   published message.

Update rate = your publish cadence (V11 `interval`, default **60 s**, min **10 s**), except
relay edges (`r0`/`r1`), which publish within ~1 s of a change. For faster moisture updates
while testing, lower the V11 interval, e.g. `tomato;test;<pi-ip>;10`.

### Enable a login (optional)

The UI is anonymous on the LAN by default, matching the broker. To put a password in front
of it, uncomment these in the `mqtt-explorer` service in `docker-compose.yml` and set values:

```yaml
    environment:
      HTTP_PORT: "4000"
      CONFIG_PATH: /mqtt-explorer/config
      HTTP_USER: admin
      HTTP_PASSWORD: choose-a-strong-one
```

Then recreate just that container: `docker compose up -d mqtt-explorer`. The browser will
prompt for those credentials. (Do this if the Pi is reachable beyond a trusted LAN.)

## Live graphs in Grafana (MQTT data source, no history)

If you'd rather watch live values inside Grafana — next to your historical Prometheus
panels — use the official **MQTT data source**. It **streams live only and stores no
history** (timestamps are stamped when messages reach Grafana), so a panel starts empty and
fills forward. This path does **not** touch Prometheus.

1. Install the plugin and restart Grafana:
   ```bash
   grafana-cli plugins install grafana-mqtt-datasource
   # then: systemctl restart grafana-server   (or restart your Grafana container)
   ```
2. **Connections → Data sources → Add → MQTT**:
   - **Host/URI:** `tcp://<raspberry-pi-ip>:1883`
     (Grafana runs outside this compose stack, so use the Pi's LAN IP + the published
     `1883`, not the `mosquitto` service name. If you ever run Grafana *in* this stack,
     use `tcp://mosquitto:1883` and add it to the `watering` network.)
3. New **Time series** panel → MQTT data source → **Topic:** `watering/<device>/moisture`.
   JSON payload fields (`s0`, `r0`, …) come through as separate series. Set the dashboard
   refresh to a few seconds; new points push in via Grafana Live as they arrive.

## Which viewer should I use?

| | History | Setup | Best for |
|--|--|--|--|
| **MQTT Explorer** (`:4000`) | none | already in this stack | quick "is data flowing?", debugging the raw payload tree |
| **Grafana + MQTT data source** | none (live stream) | one plugin | a live tile beside your other Grafana panels |
| **Grafana + Prometheus** | **yes** (retained) | scrape config (above) | trends, alerts, anything you look back on |

Short version: use **MQTT Explorer** or the **MQTT data source** to *watch in real time*; use
**Prometheus + Grafana** for anything you want to keep and query later. They coexist — all
three read the same broker.

## Notes

- **Raw values only.** The device publishes raw ADC; convert raw→percent later (calibration
  dry/wet differs per sensor). Easiest place is a Prometheus recording rule or Grafana transform,
  so re-calibrating never means re-flashing.
- **Liveness** is handled by `cache.timeout` in `config.yaml`: when a node stops publishing, its
  series expires and goes absent in Prometheus. Alert with `absent(soil_moisture_raw{...})`.
- **Relay state** rides in the same message, so it shares that liveness window. Because every relay
  transition publishes immediately, `relay_on` tracks edges to ~1 s — but metrics still can't
  guarantee catching a run shorter than the Prometheus `scrape_interval`; sampling is best-effort.
- **Friendly names** (tomato/basil/…) are not Prometheus labels in this design — see the chat
  notes for why and the options.

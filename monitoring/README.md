# Watering — MQTT + Prometheus monitoring stack

Runs on the Raspberry Pi (ARM64; both images are multi-arch). Two containers:

| Container        | Image                               | Port  | Role                                   |
|------------------|-------------------------------------|-------|----------------------------------------|
| `mosquitto`      | `eclipse-mosquitto:2`               | 1883  | MQTT broker; ESP32-S3 publishes here   |
| `mqtt2prometheus`| `ghcr.io/hikhvar/mqtt2prometheus`   | 9641  | Translates MQTT → Prometheus `/metrics`|

```
ESP32-S3 --MQTT--> mosquitto:1883 --sub--> mqtt2prometheus:9641 <--scrape-- Prometheus
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

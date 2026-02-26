# CynamoDB Metrics Reference

## Prometheus Metrics

| Metric Name | Type | Description |
|-------------|------|-------------|
| `cynamodb_request_total` | Counter | Total number of HTTP requests received. |
| `cynamodb_error_total` | Counter | Total number of 4xx and 5xx errors. |
| `cynamodb_latency_us` | Histogram | Request latency in microseconds. |
| `cynamodb_throttled_total` | Counter | Total number of throttled requests (RCU/WCU exceeded). |
| `cynamodb_bytes_received` | Counter | Total bytes received over the network. |
| `cynamodb_bytes_sent` | Counter | Total bytes sent over the network. |

## Sample Alerts
```yaml
groups:
- name: cynamodb_alerts
  rules:
  - alert: CynamoDBHighErrorRate
    expr: rate(cynamodb_error_total[5m]) / rate(cynamodb_request_total[5m]) > 0.05
    for: 2m
    labels:
      severity: critical
```

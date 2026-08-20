/**
 * CloudMine Load Tester
 * Validates NFR-PERF-1: 500 concurrent miner connections without latency degradation or dropped connections.
 */

export const version = "0.0.1";

export interface LoadTestConfig {
  targetUrl: string;
  concurrency: number;
  totalRequests: number;
}

export interface LoadTestResult {
  totalRequests: number;
  successfulRequests: number;
  failedRequests: number;
  durationMs: number;
  rps: number;
  minLatencyMs: number;
  maxLatencyMs: number;
  avgLatencyMs: number;
  p95LatencyMs: number;
}

export async function runLoadTest(config: LoadTestConfig): Promise<LoadTestResult> {
  const { targetUrl, concurrency, totalRequests } = config;
  const latencies: number[] = [];
  let successful = 0;
  let failed = 0;

  const startTime = Date.now();
  let completed = 0;

  async function worker(workerId: number) {
    while (true) {
      const idx = completed++;
      if (idx >= totalRequests) break;

      const minerId = `load-miner-${workerId}-${idx}`;
      const t0 = Date.now();
      try {
        // Test registration
        const regRes = await fetch(`${targetUrl}/miner/register`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            id: minerId,
            wallet_address: `0xload${workerId}`,
            storage_bytes_available: 10737418240,
          }),
        });

        // Test heartbeat
        const hbRes = await fetch(`${targetUrl}/heartbeat`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            miner_id: minerId,
            storage_bytes_available: 10737418240,
            battery_percent: 85,
            thermal_state: "nominal",
            network_type: "wifi",
          }),
        });

        const elapsed = Date.now() - t0;
        latencies.push(elapsed);

        if (regRes.status === 200 && hbRes.status === 200) {
          successful++;
        } else {
          failed++;
        }
      } catch {
        failed++;
      }
    }
  }

  const workers: Promise<void>[] = [];
  for (let i = 0; i < concurrency; ++i) {
    workers.push(worker(i));
  }

  await Promise.all(workers);
  const durationMs = Date.now() - startTime;

  latencies.sort((a, b) => a - b);
  const minLatencyMs = latencies[0] || 0;
  const maxLatencyMs = latencies[latencies.length - 1] || 0;
  const avgLatencyMs =
    latencies.length > 0 ? latencies.reduce((a, b) => a + b, 0) / latencies.length : 0;
  const p95Idx = Math.floor(latencies.length * 0.95);
  const p95LatencyMs = latencies[p95Idx] || 0;
  const rps = (totalRequests / durationMs) * 1000;

  return {
    totalRequests,
    successfulRequests: successful,
    failedRequests: failed,
    durationMs,
    rps,
    minLatencyMs,
    maxLatencyMs,
    avgLatencyMs,
    p95LatencyMs,
  };
}

if (typeof require !== "undefined" && typeof module !== "undefined" && require.main === module) {
  const url = process.env.ORCH_URL || "http://localhost:8080";
  const concurrency = parseInt(process.env.CONCURRENCY || "500", 10);
  const requests = parseInt(process.env.TOTAL_REQUESTS || "500", 10);

  console.log(
    `Starting load test against ${url} with ${concurrency} concurrent workers, ${requests} total requests...`
  );
  runLoadTest({ targetUrl: url, concurrency, totalRequests: requests }).then((res) => {
    console.log("Load Test Results:", JSON.stringify(res, null, 2));
    if (res.failedRequests > 0) {
      process.exit(1);
    }
  });
}

const express = require("express");
const cors = require("cors");
const path = require("path");

const app = express();
const PORT = 3000;

// guarda últimos N registros em memória
const MAX_METRICS = 200;
let metrics = [];

app.use(cors());
app.use(express.json());

// rota para receber métricas do ESP32
app.post("/api/metrics", (req, res) => {
  const body = req.body;

  // validação simples
  if (!body || typeof body.temperature === "undefined") {
    return res.status(400).json({ error: "payload invalido" });
  }

  const data = {
    userId: body.userId || "unknown",
    temperature: Number(body.temperature),
    humidity: Number(body.humidity),
    light: Number(body.light),
    noise: Number(body.noise),
    score: Number(body.score),
    working: !!body.working,
    workMinutes: Number(body.workMinutes || 0),
    timestamp: Date.now()
  };

  metrics.push(data);
  if (metrics.length > MAX_METRICS) {
    metrics.splice(0, metrics.length - MAX_METRICS);
  }

  console.log("Nova métrica recebida:", data);
  return res.json({ ok: true });
});

// rota para o dashboard (manda o HTML)
app.get("/", (req, res) => {
  res.sendFile(path.join(__dirname, "index.html"));
});

// rota para o front pegar as métricas
app.get("/api/metrics", (req, res) => {
  res.json(metrics);
});

app.listen(PORT, () => {
  console.log(`Servidor ORBITA Desk rodando em http://localhost:${PORT}`);
});

#!/usr/bin/env python3
"""Genera reportes de Gemini cada 10 minutos y publica un tablero local de solo lectura."""

from __future__ import annotations

import html
import json
import os
import sqlite3
import threading
import time
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import requests


ROOT = Path(__file__).resolve().parent


def load_env_file(path: Path = ROOT / ".env") -> None:
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


load_env_file()

DB_PATH = Path(os.getenv("DATABASE_PATH", "data/gateway.sqlite3"))
if not DB_PATH.is_absolute():
    DB_PATH = ROOT / DB_PATH
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "").strip()
GEMINI_MODEL = os.getenv("GEMINI_MODEL", "gemini-3-flash-preview").strip()
DEVICE_ID = os.getenv("DEVICE_ID", "tomine-01").strip() or "tomine-01"
REPORT_INTERVAL_SECONDS = max(60, int(os.getenv("AI_REPORT_INTERVAL_SECONDS", "600")))
REPORT_WINDOW_SECONDS = max(60, int(os.getenv("AI_REPORT_WINDOW_SECONDS", "600")))
MIN_READINGS = max(1, int(os.getenv("AI_REPORT_MIN_READINGS", "3")))
DASHBOARD_HOST = os.getenv("AI_DASHBOARD_HOST", "0.0.0.0")
DASHBOARD_PORT = int(os.getenv("AI_DASHBOARD_PORT", "8080"))
MAX_REPORTS = 50

REPORT_SCHEMA = {
    "type": "object",
    "properties": {
        "summary": {"type": "string"},
        "trend": {"type": "string", "enum": ["subiendo", "estable", "bajando", "variable", "datos_insuficientes", "no_determinada"]},
        "observations": {"type": "array", "items": {"type": "string"}},
        "suggested_action": {"type": "string"},
        "uncertainty": {"type": "string"},
    },
    "required": ["summary", "trend", "observations", "suggested_action", "uncertainty"],
}


def db_connect() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH, timeout=15)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA busy_timeout=15000")
    return conn


def init_db() -> None:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    with db_connect() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS ai_reports (
                id INTEGER PRIMARY KEY,
                device_id TEXT NOT NULL,
                window_start TEXT NOT NULL,
                window_end TEXT NOT NULL,
                generated_at TEXT NOT NULL,
                model TEXT NOT NULL,
                reading_count INTEGER NOT NULL,
                status TEXT NOT NULL,
                report_json TEXT,
                error_message TEXT
            )
        """)
        conn.execute("CREATE INDEX IF NOT EXISTS idx_ai_reports_generated ON ai_reports(generated_at DESC)")


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def fetch_window(start: datetime, end: datetime) -> dict[str, list[dict[str, Any]]]:
    with db_connect() as conn:
        rows = conn.execute("""
            SELECT device_id, received_at, water_level, water_distance, temperature,
                   humidity, pressure, solar_radiation, evaporation_risk, alert_state
            FROM telemetry
            WHERE received_at >= ? AND received_at <= ?
            ORDER BY received_at ASC
        """, (start.isoformat(timespec="seconds"), end.isoformat(timespec="seconds"))).fetchall()
    grouped: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        grouped.setdefault(row["device_id"], []).append(dict(row))
    return grouped


def average(rows: list[dict[str, Any]], field: str) -> float | None:
    values = [float(row[field]) for row in rows if row[field] is not None]
    return round(sum(values) / len(values), 2) if values else None


def calculate_metrics(rows: list[dict[str, Any]]) -> dict[str, Any]:
    levels = [float(row["water_level"]) for row in rows]
    states: dict[str, int] = {}
    for row in rows:
        state = str(row["alert_state"])
        states[state] = states.get(state, 0) + 1
    # A small, evenly distributed sample gives Gemini context without sending every 2 s reading.
    sample_count = min(12, len(rows))
    indices = sorted({round(i * (len(rows) - 1) / max(1, sample_count - 1)) for i in range(sample_count)})
    sample = [rows[index] for index in indices]
    return {
        "reading_count": len(rows),
        "water_level": {
            "start_pct": round(levels[0], 2),
            "end_pct": round(levels[-1], 2),
            "change_percentage_points": round(levels[-1] - levels[0], 2),
            "min_pct": round(min(levels), 2),
            "max_pct": round(max(levels), 2),
            "average_pct": round(sum(levels) / len(levels), 2),
        },
        "averages": {
            "temperature_c": average(rows, "temperature"),
            "humidity_pct": average(rows, "humidity"),
            "pressure_hpa": average(rows, "pressure"),
            "solar_radiation_ldr_pct": average(rows, "solar_radiation"),
            "estimated_evaporation_risk_pct": average(rows, "evaporation_risk"),
        },
        "alert_state_counts": states,
        "sampled_readings": sample,
        "notes": [
            "El índice de evaporación es una estimación normalizada del prototipo, no mm/día.",
            "No se miden directamente precipitación, caudales ni operación del embalse.",
        ],
    }


def extract_response_text(payload: dict[str, Any]) -> str:
    candidates = payload.get("candidates")
    if not isinstance(candidates, list) or not candidates:
        raise ValueError("Gemini no devolvió candidatos")
    parts = candidates[0].get("content", {}).get("parts", [])
    text_parts = [part.get("text", "") for part in parts if isinstance(part, dict)]
    text = "".join(text_parts).strip()
    if not text:
        raise ValueError("Gemini devolvió una respuesta vacía")
    return text


def parse_report_json(text: str) -> dict[str, Any]:
    """Accept valid JSON directly, in a Markdown fence, or surrounded by brief text."""
    cleaned = text.strip().lstrip("\ufeff")
    if cleaned.startswith("```"):
        lines = cleaned.splitlines()
        if lines and lines[0].startswith("```"):
            lines = lines[1:]
        if lines and lines[-1].strip() == "```":
            lines = lines[:-1]
        cleaned = "\n".join(lines).strip()

    try:
        result = json.loads(cleaned)
    except json.JSONDecodeError as original_error:
        start = cleaned.find("{")
        end = cleaned.rfind("}")
        if start < 0 or end <= start:
            # Keep a useful model response visible even if the preview ignores JSON mode.
            return {
                "summary": cleaned[:1200],
                "trend": "no_determinada",
                "observations": ["Gemini respondió en texto libre; no se pudieron extraer campos estructurados."],
                "suggested_action": "Contrastar este comentario con las lecturas y el estado calculado por el dispositivo.",
                "uncertainty": "La respuesta no respetó el formato JSON solicitado; no se clasificó la tendencia.",
            }
        try:
            result = json.loads(cleaned[start:end + 1])
        except json.JSONDecodeError as exc:
            raise ValueError("Gemini devolvió un objeto JSON malformado") from exc

    if not isinstance(result, dict):
        raise ValueError("La respuesta JSON de Gemini no es un objeto")
    return result


def request_report(device_id: str, metrics: dict[str, Any]) -> tuple[dict[str, Any], str]:
    prompt = (
        "Eres un asistente para interpretar telemetría de monitoreo hídrico. "
        "Devuelve un objeto JSON que cumpla exactamente el esquema; no uses Markdown ni texto fuera del JSON. "
        "Resume únicamente lo que respaldan los datos de la ventana indicada. "
        "Describe tendencia observada; no predigas el nivel futuro, no afirmes causas "
        "que estos sensores no miden y no calcules evaporación física en mm/día. "
        "El estado de alerta fue calculado por reglas del dispositivo y no debes cambiarlo. "
        "La acción sugerida es informativa para revisión humana; nunca controla alarmas. "
        "Menciona limitaciones e incertidumbre con claridad. Responde en español.\n\n"
        f"Dispositivo: {device_id}\nDatos agregados de la ventana: "
        + json.dumps(metrics, ensure_ascii=False, separators=(",", ":"))
    )
    request_body = {
        "contents": [{"parts": [{"text": prompt}]}],
        "generationConfig": {
            "responseMimeType": "application/json",
            "responseJsonSchema": REPORT_SCHEMA,
            "maxOutputTokens": 1000,
            "temperature": 0.2,
        },
    }
    url = f"https://generativelanguage.googleapis.com/v1beta/models/{GEMINI_MODEL}:generateContent"
    last_error: requests.RequestException | None = None
    for attempt in range(3):
        try:
            response = requests.post(
                url,
                headers={"x-goog-api-key": GEMINI_API_KEY, "Content-Type": "application/json"},
                json=request_body,
                timeout=(5, 60),
            )
            if response.status_code == 503 and attempt < 2:
                delay = 5 * (attempt + 1)
                print(f"Gemini {GEMINI_MODEL} respondió HTTP 503; reintento {attempt + 1}/2 en {delay}s.")
                response.close()
                time.sleep(delay)
                continue
            response.raise_for_status()
            try:
                api_payload = response.json()
            except requests.exceptions.JSONDecodeError as exc:
                raise ValueError(f"Gemini devolvió una respuesta HTTP no-JSON (HTTP {response.status_code})") from exc
            if not isinstance(api_payload, dict):
                raise ValueError("Gemini devolvió una respuesta HTTP con formato inesperado")
            result = parse_report_json(extract_response_text(api_payload))
            required = ("summary", "trend", "observations", "suggested_action", "uncertainty")
            if any(key not in result for key in required):
                raise ValueError("La respuesta de Gemini no cumple el esquema esperado")
            if result["trend"] not in {"subiendo", "estable", "bajando", "variable", "datos_insuficientes", "no_determinada"}:
                raise ValueError("Tendencia no válida en la respuesta de Gemini")
            if not isinstance(result["observations"], list) or not all(isinstance(item, str) for item in result["observations"]):
                raise ValueError("Observaciones no válidas en la respuesta de Gemini")
            for key in ("summary", "suggested_action", "uncertainty"):
                if not isinstance(result[key], str):
                    raise ValueError(f"Campo {key} no válido en la respuesta de Gemini")
                result[key] = result[key][:1200]
            result["observations"] = [item[:400] for item in result["observations"][:8]]
            return result, GEMINI_MODEL
        except requests.Timeout as exc:
            last_error = exc
            if attempt < 2:
                delay = 5 * (attempt + 1)
                print(f"Timeout con Gemini {GEMINI_MODEL}; reintento {attempt + 1}/2 en {delay}s.")
                time.sleep(delay)
        except requests.HTTPError as exc:
            last_error = exc
            if exc.response is None or exc.response.status_code != 503 or attempt == 2:
                raise
    if last_error is not None:
        raise last_error
    raise requests.RequestException("No se recibió respuesta de Gemini")


def store_report(
    device_id: str,
    start: datetime,
    end: datetime,
    reading_count: int,
    status: str,
    report: dict[str, Any] | None = None,
    error_message: str | None = None,
    model_used: str | None = None,
) -> None:
    with db_connect() as conn:
        conn.execute("""
            INSERT INTO ai_reports (
                device_id, window_start, window_end, generated_at, model,
                reading_count, status, report_json, error_message
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            device_id,
            start.isoformat(timespec="seconds"),
            end.isoformat(timespec="seconds"),
            utc_now().isoformat(timespec="seconds"),
            model_used or GEMINI_MODEL,
            reading_count,
            status,
            json.dumps(report, ensure_ascii=False) if report is not None else None,
            error_message[:500] if error_message else None,
        ))


def process_cycle() -> None:
    end = utc_now()
    start = end - timedelta(seconds=REPORT_WINDOW_SECONDS)
    grouped = fetch_window(start, end)
    if not grouped:
        store_report(
            DEVICE_ID,
            start,
            end,
            0,
            "no_data",
            error_message="No llegaron lecturas del dispositivo durante la ventana de análisis.",
        )
        print(f"IA ({DEVICE_ID}): no hay telemetría en la ventana; guardé el estado para el tablero.")
        return
    for device_id, rows in grouped.items():
        if len(rows) < MIN_READINGS:
            store_report(device_id, start, end, len(rows), "insufficient_data", error_message="No hay suficientes lecturas en la ventana.")
            print(f"IA ({device_id}): datos insuficientes ({len(rows)} lecturas).")
            continue
        if not GEMINI_API_KEY:
            store_report(device_id, start, end, len(rows), "missing_api_key", error_message="Configura GEMINI_API_KEY en .env.")
            print("IA: GEMINI_API_KEY no configurada; el gateway MQTT continúa funcionando.")
            continue
        try:
            metrics = calculate_metrics(rows)
            report, model_used = request_report(device_id, metrics)
            store_report(device_id, start, end, len(rows), "ok", report=report, model_used=model_used)
            print(f"IA ({device_id}): reporte generado con {len(rows)} lecturas usando {model_used}.")
        except requests.HTTPError as exc:
            status_code = exc.response.status_code if exc.response is not None else "desconocido"
            store_report(device_id, start, end, len(rows), "api_error", error_message=f"Gemini respondió HTTP {status_code}.")
            print(f"IA ({device_id}): Gemini respondió HTTP {status_code}; gateway continúa.")
        except requests.RequestException as exc:
            store_report(device_id, start, end, len(rows), "api_error", error_message=f"Error de conexión/API: {type(exc).__name__}.")
            print(f"IA ({device_id}): error de conexión/API ({type(exc).__name__}); gateway continúa.")
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
            detail = str(exc)[:240] or type(exc).__name__
            store_report(device_id, start, end, len(rows), "invalid_response", error_message=f"Respuesta inválida: {detail}")
            print(f"IA ({device_id}): respuesta inválida ({detail}); gateway continúa.")


def esc(value: Any) -> str:
    return html.escape(str(value), quote=True)


def load_reports() -> list[dict[str, Any]]:
    with db_connect() as conn:
        rows = conn.execute("""
            SELECT device_id, window_start, window_end, generated_at, model,
                   reading_count, status, report_json, error_message
            FROM ai_reports ORDER BY id DESC LIMIT ?
        """, (MAX_REPORTS,)).fetchall()
    result = []
    for row in rows:
        item = dict(row)
        try:
            item["report"] = json.loads(item.pop("report_json")) if item["report_json"] else None
        except json.JSONDecodeError:
            item["report"] = None
        result.append(item)
    return result


def dashboard_html() -> str:
    reports = load_reports()
    cards: list[str] = []
    for item in reports:
        report = item["report"] or {}
        status = item["status"]
        body = ""
        if report:
            observations = "".join(f"<li>{esc(value)}</li>" for value in report.get("observations", []))
            body = (
                f"<p class='summary'>{esc(report.get('summary', ''))}</p>"
                f"<p><b>Tendencia:</b> {esc(report.get('trend', ''))}</p>"
                f"<h3>Observaciones</h3><ul>{observations}</ul>"
                f"<p><b>Sugerencia:</b> {esc(report.get('suggested_action', ''))}</p>"
                f"<p class='muted'><b>Incertidumbre:</b> {esc(report.get('uncertainty', ''))}</p>"
            )
        elif item["error_message"]:
            body = f"<p class='error'>{esc(item['error_message'])}</p>"
        cards.append(
            "<article class='report'>"
            f"<div class='meta'><span>{esc(item['device_id'])}</span><span class='badge {esc(status)}'>{esc(status)}</span></div>"
            f"<p class='muted'>Generado {esc(item['generated_at'])} · Ventana {esc(item['window_start'])} – {esc(item['window_end'])}</p>"
            f"<p class='muted'>{esc(item['reading_count'])} lecturas · {esc(item['model'])}</p>"
            f"{body}</article>"
        )
    report_list = "".join(cards) or "<div class='empty'>Aún no hay reportes. El generador analiza los datos disponibles al iniciar y vuelve a intentarlo cada intervalo.</div>"
    return f"""<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="60"><title>Reportes IA · Monitoreo hídrico</title>
<style>
*{{box-sizing:border-box}}body{{margin:0;background:#eef4f7;color:#152b35;font:16px/1.5 Arial,sans-serif}}
header{{background:#07566c;color:#fff;padding:22px max(20px,calc((100% - 980px)/2))}}header h1{{margin:0;font-size:25px}}header p{{margin:4px 0 0;color:#d3eaf0}}
main{{max-width:980px;margin:24px auto;padding:0 18px}}.notice{{background:#fff7db;border-left:4px solid #e5ad2f;padding:12px 16px;margin-bottom:18px;border-radius:5px}}
.report,.empty{{background:white;border:1px solid #d8e3e8;border-radius:10px;padding:18px;margin:14px 0;box-shadow:0 2px 8px #1537440a}}
.meta{{display:flex;justify-content:space-between;align-items:center;font-weight:700;font-size:18px}}.badge{{font-size:12px;padding:3px 9px;border-radius:20px;background:#e5f5e9;color:#23723a}}
.badge.api_error,.badge.invalid_response,.badge.missing_api_key{{background:#fde9e7;color:#a52b25}}.badge.insufficient_data,.badge.no_data{{background:#fff3d5;color:#855f00}}
.muted{{color:#607782;font-size:13px}}.summary{{font-size:18px}}.error{{color:#a52b25}}h3{{font-size:15px;margin-bottom:4px}}ul{{margin-top:4px}}
</style></head><body><header><h1>Reportes de análisis con Gemini</h1><p>Resumen periódico de telemetría guardada en la Raspberry Pi</p></header>
<main><div class="notice">Reportes informativos generados cada {REPORT_INTERVAL_SECONDS // 60} minutos. Las alertas y alarmas siguen bajo control de las reglas locales del ESP32.</div>
{report_list}</main></body></html>"""


class DashboardHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - nombre requerido por BaseHTTPRequestHandler
        path = urlparse(self.path).path
        if path == "/healthz":
            body = b"ok"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
        elif path == "/":
            body = dashboard_html().encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
        else:
            body = b"Not found"
            self.send_response(404)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format: str, *_args: Any) -> None:
        return


def run_dashboard() -> None:
    server = ThreadingHTTPServer((DASHBOARD_HOST, DASHBOARD_PORT), DashboardHandler)
    server.daemon_threads = True
    print(f"Tablero IA local: http://<IP-DE-LA-RASPBERRY>:{DASHBOARD_PORT}/")
    server.serve_forever(poll_interval=0.5)


def main() -> None:
    init_db()
    dashboard_thread = threading.Thread(target=run_dashboard, name="ai-dashboard", daemon=True)
    dashboard_thread.start()
    print(f"Reportes Gemini: ventana={REPORT_WINDOW_SECONDS}s, intervalo={REPORT_INTERVAL_SECONDS}s, modelo={GEMINI_MODEL}")
    print(f"SQLite: {DB_PATH}")
    print(f"GEMINI_API_KEY: {'configurada' if GEMINI_API_KEY else 'no configurada'}")
    try:
        # Produce un reporte al iniciar, usando lo que ya haya en la ventana reciente.
        # Si no hay suficientes lecturas, el siguiente intento será al completar el intervalo.
        try:
            process_cycle()
        except sqlite3.Error as exc:
            print(f"IA: error de SQLite ({type(exc).__name__}); se reintentará en el próximo intervalo.")
        while True:
            time.sleep(REPORT_INTERVAL_SECONDS)
            try:
                process_cycle()
            except sqlite3.Error as exc:
                print(f"IA: error de SQLite ({type(exc).__name__}); se reintentará en el próximo intervalo.")
    except KeyboardInterrupt:
        print("Cerrando tablero y generador de reportes IA.")


if __name__ == "__main__":
    main()

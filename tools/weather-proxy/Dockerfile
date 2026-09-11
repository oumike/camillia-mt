# Camillia weather proxy — the HTTP->HTTPS bridge the weather screen needs.
# See docs/WEATHER.md.
FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY weather_proxy.py .

EXPOSE 5006

# gunicorn rather than Flask's dev server, for the reasons the elevation proxy's
# Dockerfile spells out. The timeout is shorter here because this service makes
# one upstream call with one retry (~25 s worst case) and never fans out.
CMD ["gunicorn", "-w", "2", "-b", "0.0.0.0:5006", "-t", "40", "weather_proxy:app"]

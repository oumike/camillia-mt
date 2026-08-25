# Camillia elevation proxy — the HTTP->HTTPS bridge the LOS feature needs.
# See docs/LOS.md.
FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY elev_proxy.py .

EXPOSE 5005

# gunicorn rather than Flask's dev server. The load is tiny, but the dev server
# prints a warning on every boot and is single-threaded by default, which would
# serialise a second device behind a 12 s upstream fetch.
#
# 2 workers, long timeout: a request can legitimately take ~25 s when the
# upstream retries, and gunicorn's 30 s default would kill it just short.
CMD ["gunicorn", "-w", "2", "-b", "0.0.0.0:5005", "-t", "60", "elev_proxy:app"]

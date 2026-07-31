import re
from pathlib import Path

CONFIG = Path("include/config.h")
OUTPUT = Path("data/price.html")

config = CONFIG.read_text()

match = re.search(r"#define\s+SLOT_COUNT\s+(\d+)", config)

if not match:
    raise RuntimeError("SLOT_COUNT not found")

slot_count = int(match.group(1))

fields = []

for i in range(slot_count):
    fields.append(f"""    <div class="slot">
        <label for="slot{i}">Slot {i + 1}</label>
        <input id="slot{i}" name="slot{i}" type="number" min="0" step="1" value="%PRICE_{i}%">
    </div>
""")

html = f"""<!DOCTYPE html>
<html lang="id">
<head>
    <title>Pengaturan Harga</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link href="/styles.css" rel="stylesheet" type="text/css">
</head>

<body>
    <a href="/" id="back">Kembali</a>
    <h1>Pengaturan Harga</h1>

    <form id="priceForm">

{''.join(fields)}

    <button type="submit">Simpan</button>
    </form>
    <div id="error"></div>
    <div id="success"></div>
    <script>
    document.getElementById('priceForm').addEventListener('submit', async (e) => {{
        e.preventDefault();
        const prices = [];

        for (let i = 0; i < {slot_count}; i++) {{
            const value = document.getElementById(`slot${{i}}`).value;

            prices.push(Number(value));
        }}
        
        const errorDiv = document.getElementById('error');
        const successDiv = document.getElementById('success');

        const res = await fetch('/harga', {{
            method: 'POST',
            headers: {{ 'Content-Type': 'application/json' }},
            body: JSON.stringify({{ prices: prices }}),
            credentials: 'same-origin'
        }});

        if (res.ok) {{
            successDiv.textContent = 'Berhasil menyimpan harga';
        }} else {{
            errorDiv.textContent = 'Gagal menyimpan harga: ' + await res.text();
        }}
    }});
    </script>
    </body>
</html>
"""

OUTPUT.write_text(html)
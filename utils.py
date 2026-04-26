from datetime import datetime

def now_hms() -> str:
    return datetime.now().strftime("%H:%M:%S")

def safe_float(v):
    try:
        return float(v)
    except Exception:
        return None

import asyncio
import serial
from collections import deque
from contextlib import asynccontextmanager
from pathlib import Path
from fastapi.responses import FileResponse
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles

# --- ZMIENNE GLOBALNE ---
# Przechowujemy aktualne wartości modłów/suwaków (0-255)
gods_state = {"sun": 0, "temp": 0, "wind": 0}

output_state = {
    "sun_power": 255,
    "storm_power": 0,
    "street_lights_power": 0,
    "laundry_open": 1,
    "rope_pull": 0,
    "shed_close": 0,
}

# Konfiguracja portu szeregowego (Zmień COM3 na właściwy port, np. /dev/ttyUSB0 na Linux)
SERIAL_PORT = "COM8"
BAUD_RATE = 115200

try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"⚡ Połączono z Valhallą przez port: {SERIAL_PORT}")
except serial.SerialException:
    ser = None
    print(f"⚠️ Nie można otworzyć portu {SERIAL_PORT}. Tryb symulacji.")

# --- FUNKCJE MATEMATYCZNE (MÓZG PREDYKCJI) ---
def get_mean(lst):
    """Zwraca średnią wartość z listy."""
    return sum(lst) / len(lst) if lst else 0.0

def get_trend(lst):
    """Oblicza nachylenie linii trendu (OLS). Dodatnie = rośnie, ujemne = maleje."""
    n = len(lst)
    if n < 3: return 0.0
    mx = (n - 1) / 2.0
    my = get_mean(lst)
    num   = sum((i - mx) * (lst[i] - my) for i in range(n))
    denom = sum((i - mx) ** 2 for i in range(n))
    return num / denom if denom else 0.0

# --- CYFROWA WYROCZNIA (KLASA BRONIĄCA WIOSKI) ---
class VillageDefender:
    def __init__(self):
        # Przechowujemy ostatnie 50 próbek (przy wysyłaniu co 0.1s daje to 5 sekund historii)
        self.wind_history = deque(maxlen=50)

    def evaluate(self):
        """Analizuje stan bogów i wysterowuje sprzęt na makiecie."""
        sun_in = gods_state["sun"]
        temp_in = gods_state["temp"]
        wind_in = gods_state["wind"]

        # --- 1. SŁOŃCE I ŚWIATŁA ---
        output_state["sun_power"] = sun_in
        # Latarnie zapalają się, gdy słońce spada poniżej 100. Im ciemniej, tym mocniej świecą (0-255).
        if sun_in < 100:
            output_state["street_lights_power"] = int(((100 - sun_in) / 100.0) * 255)
        else:
            output_state["street_lights_power"] = 0

        # --- 2. OBLICZANIE MOCY BURZY (STORM POWER) ---
        temp_extremity = abs(temp_in - 127) / 127.0
        temp_multiplier = 1.0 + (temp_extremity * 0.3) 
        sun_suppression = 1.0 - ((sun_in / 255.0) * 0.4) 
        
        final_storm = int(wind_in * temp_multiplier * sun_suppression)
        output_state["storm_power"] = max(0, min(255, final_storm))

        # --- 3. PREDYKCJA I ZARZĄDZANIE KRYZYSOWE ---
        self.wind_history.append(wind_in) # Zapisz aktualny wiatr do historii
        
        # Pobieramy średnią z ostatnich 10 próbek (1 sekunda) - ignoruje chwilowe szarpnięcia
        recent_wind = list(self.wind_history)[-10:] if len(self.wind_history) >= 10 else list(self.wind_history)
        wind_ma10 = get_mean(recent_wind)

        # Obliczamy trend ze wszystkich 50 próbek (5 sekund)
        wind_trend = get_trend(list(self.wind_history))

        # Wskaźnik Paniki: Aktualna moc burzy + potężny mnożnik, jeśli wiatr GWAŁTOWNIE rośnie
        # Jeśli trend jest ujemny (wiatr spada), zagrożenie szybciej maleje
        panic_score = output_state["storm_power"] + (wind_trend * 15) 

        # --- REAKCJE MECHANIKI ---
        # Zdejmij pranie, gdy mocno wieje (zwykła średnia) LUB panika rośnie (przed burzą)
        output_state["laundry_open"] = 0 if (wind_ma10 > 100 or panic_score > 120) else 1

        # Zamknij szopę, gdy panika przekroczy 160 (stan powagi)
        output_state["shed_close"] = 1 if panic_score > 160 else 0

        # Wciągaj łódkę tylko przy pełnej burzy, gdy sytuacja jest krytyczna
        output_state["rope_pull"] = 1 if panic_score > 210 else 0

# Tworzymy instancję obrońcy
defender = VillageDefender()

# --- ZADANIE W TLE (WYSYŁANIE CO 100ms) ---
async def serial_writer_task():
    while True:
        # Wywołanie sztucznej inteligencji makiety
        defender.evaluate()

        # Budowanie ramki danych CSV
        command = f"{output_state['sun_power']},{output_state['storm_power']}," \
                  f"{output_state['street_lights_power']},{output_state['laundry_open']}," \
                  f"{output_state['rope_pull']},{output_state['shed_close']}\n"
        
        if ser and ser.is_open:
            ser.write(command.encode('ascii'))
            
        print(f"[{command.strip()}] | Trend: {get_trend(list(defender.wind_history)):.2f}")
        
        await asyncio.sleep(0.1)


async def serial_writer_task():
    while True:
        # Wywołanie sztucznej inteligencji makiety
        defender.evaluate()

        # Budowanie ramki danych CSV
        command = f"{output_state['sun_power']},{output_state['storm_power']}," \
                  f"{output_state['street_lights_power']},{output_state['laundry_open']}," \
                  f"{output_state['rope_pull']},{output_state['shed_close']}\n"
        
        if ser and ser.is_open:
            ser.write(command.encode('ascii'))
            
        print(f"[{command.strip()}] | Trend: {get_trend(list(defender.wind_history)):.2f}")
        
        await asyncio.sleep(0.1)


# --- CYKL ŻYCIA APLIKACJI ---
@asynccontextmanager
async def lifespan(app: FastAPI):
    # Wykonywane podczas STARTU serwera
    task = asyncio.create_task(serial_writer_task())
    yield
    # Wykonywane podczas ZAMYKANIA serwera
    task.cancel()
    if ser and ser.is_open:
        ser.close()
        print("Brama do Valhalli zamknięta.")


# --- INICJALIZACJA FASTAPI ---
app = FastAPI(lifespan=lifespan)


@app.get("/")
async def index():
    return FileResponse(Path("static") / "index.html")


# --- ENDPOINT WEBSOCKET ---
@app.websocket("/ws/{god_id}")
async def websocket_endpoint(websocket: WebSocket, god_id: str):
    await websocket.accept()
    print(f"👁️ Bóg {god_id} połączył się ze swoim ołtarzem.")

    try:
        while True:
            # Oczekujemy na nową wartość z telefonu
            data = await websocket.receive_text()

            # Walidacja danych
            if god_id in gods_state:
                try:
                    value = int(data)
                    # Upewniamy się, że wartość mieści się w zakresie 0-255
                    if 0 <= value <= 255:
                        gods_state[god_id] = value
                except ValueError:
                    print(f"Odrzucono nieprawidłowe dane od {god_id}: {data}")

    except WebSocketDisconnect:
        print(f"💨 Bóg {god_id} opuścił nas.")


# --- SERWOWANIE FRONTENDU (KROK 1) ---
# Wystawia folder 'static', aby można było wejść na localhost:8000/sun.html
app.mount("/", StaticFiles(directory="static", html=True), name="static")

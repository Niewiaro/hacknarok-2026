#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/console/console.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <stdio.h>

#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_NUM_LEDS 21
#define NUM_CLOUDS 19
#define SUN_START_IDX 19 /* Diody nr 19 i 20 to słońce */

static struct led_rgb pixels[STRIP_NUM_LEDS];
static uint8_t cloud_map[NUM_CLOUDS] = {0};

/* --- Globalne Zmienne Środowiskowe (0 - 100%) --- */
volatile int sun_power = 100;
volatile int storm_intensity = 0;

/* * -------------------------------------------------------------------
 * WĄTEK: SILNIK RENDERUJĄCY NIEBO
 * -------------------------------------------------------------------
 */
void weather_led_thread(void *p1, void *p2, void *p3)
{
	const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

	while (1)
	{
		/* 1. OBLICZANIE SŁOŃCA (Im większa burza, tym słońce słabsze) */
		/* Wzór: Moc słońca * (100 - siła burzy) / 100 */
		int effective_sun = (sun_power * (100 - storm_intensity)) / 100;
		uint8_t sun_val = (effective_sun * 255) / 100;

		/* Rysowanie słońca (żółty kolor: Czerwony + Zielony) */
		pixels[SUN_START_IDX].r = sun_val;
		pixels[SUN_START_IDX].g = sun_val;
		pixels[SUN_START_IDX].b = 0;

		pixels[SUN_START_IDX + 1].r = sun_val;
		pixels[SUN_START_IDX + 1].g = sun_val;
		pixels[SUN_START_IDX + 1].b = 0;

		/* 2. LOGIKA CHMUR I BURZY */
		bool lightning_strike = false;

		if (storm_intensity > 0)
		{
			/* Wygaszanie poprzednich kropel (efekt Glitter) */
			for (int i = 0; i < NUM_CLOUDS; i++)
			{
				cloud_map[i] = (cloud_map[i] * 75) / 100; /* Dość szybkie wygaszanie */
			}

			/* Losowanie nowej kropli. Szansa zależy wprost od storm_intensity */
			int drop_chance = storm_intensity / 2; /* max 50% szans co klatkę */
			if ((sys_rand32_get() % 100) < drop_chance)
			{
				int random_led = sys_rand32_get() % NUM_CLOUDS;
				cloud_map[random_led] = 255;
			}

			/* Logika Piorunów (Tylko jeśli burza > 50%) */
			if (storm_intensity > 50)
			{
				/* Im bliżej 100%, tym większa szansa na piorun */
				int lightning_chance = (storm_intensity - 50) / 5;
				if ((sys_rand32_get() % 100) < lightning_chance)
				{
					lightning_strike = true;
				}
			}
		}

		/* 3. RYSOWANIE CHMUR NA PODSTAWIE OBLICZEŃ */
		for (int i = 0; i < NUM_CLOUDS; i++)
		{
			if (lightning_strike)
			{
				/* Oślepiający błysk pioruna zasłania wszystko inne */
				pixels[i].r = 255;
				pixels[i].g = 255;
				pixels[i].b = 255;
			}
			else if (storm_intensity > 0)
			{
				/* Tryb burzy: Kropelki deszczu przechodzące w granat (Glitter) */
				pixels[i].r = cloud_map[i] / 5; /* Mało czerwonego */
				pixels[i].g = cloud_map[i] / 2; /* Trochę zielonego */
				pixels[i].b = cloud_map[i];		/* Dużo niebieskiego */
			}
			else
			{
				/* Czyste niebo: Delikatny biały zależny od mocy słońca */
				/* Dzielimy przez 5, aby 100% słońca dało wartość 50 (delikatny biały, nie razi w oczy) */
				uint8_t gentle_white = (sun_power * 50) / 100;
				pixels[i].r = gentle_white;
				pixels[i].g = gentle_white;
				pixels[i].b = gentle_white;
			}
		}

		/* 4. Wysłanie zbuforowanej klatki do fizycznych diod */
		led_strip_update_rgb(strip, pixels, STRIP_NUM_LEDS);

		k_msleep(30); /* Odświeżanie ok. 33 FPS */
	}
}

K_THREAD_DEFINE(weather_thread, 2048, weather_led_thread, NULL, NULL, NULL, 5, 0, 0);

/* * -------------------------------------------------------------------
 * MAIN: INTERFEJS KONTROLNY (UART)
 * -------------------------------------------------------------------
 */
int main(void)
{
	console_init();
	printf("\n\n--- System Pogodowy nRF54L15 Gotowy ---\n");
	printf("Sterowanie (Wpisz i zatwierdz):\n");
	printf("[W] Slonce +10%%  |  [S] Slonce -10%%\n");
	printf("[E] Burza +10%%   |  [D] Burza -10%%\n");

	while (1)
	{
		uint8_t c = console_getchar();

		if (c == 'w' || c == 'W')
			sun_power = (sun_power <= 90) ? sun_power + 10 : 100;
		if (c == 's' || c == 'S')
			sun_power = (sun_power >= 10) ? sun_power - 10 : 0;

		if (c == 'e' || c == 'E')
			storm_intensity = (storm_intensity <= 90) ? storm_intensity + 10 : 100;
		if (c == 'd' || c == 'D')
			storm_intensity = (storm_intensity >= 10) ? storm_intensity - 10 : 0;

		if (c == 'w' || c == 'W' || c == 's' || c == 'S' || c == 'e' || c == 'E' || c == 'd' || c == 'D')
		{
			printf(" -> AKTUALIZACJA: Slonce = %d%%, Burza = %d%%\n", sun_power, storm_intensity);
		}
	}
	return 0;
}
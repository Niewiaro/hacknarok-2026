#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/console/console.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_NUM_LEDS 21
#define NUM_CLOUDS 19
#define SUN_START_IDX 19

static const struct pwm_dt_spec sun_servo = PWM_DT_SPEC_GET(DT_ALIAS(sun_servo));

static struct led_rgb pixels[STRIP_NUM_LEDS];
static uint8_t cloud_map[NUM_CLOUDS] = {0};

volatile int sun_power = 100;
volatile int storm_intensity = 0;
volatile int target_servo_pos = 50; // Nowa zmienna dla serwa (0-100%)

#define SERVO_PERIOD_NS PWM_MSEC(20)
#define SERVO_MIN_PULSE_NS 600000U   /* 0.6 ms */
#define SERVO_MAX_PULSE_NS 2400000U  /* 2.4 ms */

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

void servo_control_thread(void *p1, void *p2, void *p3)
{
	if (!pwm_is_ready_dt(&sun_servo)) {
		printf("[SERVO] PWM device not ready\n");
		return;
	}

	printf("[SERVO] PWM ready: period=%u ns, channel=%u\n",
		(unsigned int)sun_servo.period, sun_servo.channel);

	while (1)
	{
		/* Obliczamy puls: 1ms (0%) do 2ms (100%) */
		uint32_t pulse_ns = SERVO_MIN_PULSE_NS +
			((uint32_t)target_servo_pos * (SERVO_MAX_PULSE_NS - SERVO_MIN_PULSE_NS)) / 100U;

		int ret = pwm_set_dt(&sun_servo, SERVO_PERIOD_NS, pulse_ns);
		if (ret != 0) {
			printf("[SERVO] pwm_set_dt failed: %d (period=%u, pulse=%u)\n",
				ret, (unsigned int)SERVO_PERIOD_NS, (unsigned int)pulse_ns);
		}

		// Serwo nie musi być aktualizowane 30 razy na sekundę.
		// 100ms (10Hz) wystarczy dla płynnego ruchu i oszczędza procesor.
		k_msleep(100);
	}
}
K_THREAD_DEFINE(servo_tid, 1024, servo_control_thread, NULL, NULL, NULL, 6, 0, 0);

int main(void) {
    console_init();
	printf("System gotowy. Sterowanie: [W/S] Sun, [E/D] Storm, [R/F] Servo Pos\n");

    while (1) {
        uint8_t c = console_getchar();
		c = (uint8_t)tolower(c);
        
        if (c == 'w') sun_power = MIN(sun_power + 10, 100);
        if (c == 's') sun_power = MAX(sun_power - 10, 0);
        
        if (c == 'e') storm_intensity = MIN(storm_intensity + 10, 100);
        if (c == 'd') storm_intensity = MAX(storm_intensity - 10, 0);

        // Nowe sterowanie dedykowaną zmienną dla serwa
        if (c == 'r') target_servo_pos = MIN(target_servo_pos + 10, 100);
        if (c == 'f') target_servo_pos = MAX(target_servo_pos - 10, 0);

        printf("Stan: Sun %d%%, Storm %d%%, Servo %d%%\n", 
                sun_power, storm_intensity, target_servo_pos);
    }
    return 0;
}
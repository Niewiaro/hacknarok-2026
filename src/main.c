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

static const struct pwm_dt_spec laundry_servo = PWM_DT_SPEC_GET(DT_ALIAS(laundry_servo));
static const struct pwm_dt_spec rope_servo = PWM_DT_SPEC_GET(DT_ALIAS(rope_servo));

static struct led_rgb pixels[STRIP_NUM_LEDS];
static uint8_t cloud_map[NUM_CLOUDS] = {0};

volatile int sun_power = 100;
volatile int storm_intensity = 0;

volatile bool laundry_open = false;
volatile bool pull_rope = true;

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

void laundry_servo_thread(void *p1, void *p2, void *p3) {
    if (!pwm_is_ready_dt(&laundry_servo)) return;

    bool last_state = !laundry_open;

    while (1) {
        if (laundry_open != last_state) {
            uint32_t pos = laundry_open ? 0 : 50;
            uint32_t pulse = SERVO_MIN_PULSE_NS + (pos * (SERVO_MAX_PULSE_NS - SERVO_MIN_PULSE_NS)) / 100U;
            pwm_set_pulse_dt(&laundry_servo, pulse);
            
            last_state = laundry_open;
        }
        
        k_msleep(100); 
    }
}
K_THREAD_DEFINE(laundry_tid, 1024, laundry_servo_thread, NULL, NULL, NULL, 6, 0, 0);


#define ROPE_FPS_MS 20
#define ROPE_MIN_POS 40      // Prędkość max w dół (Rozwijanie)
#define ROPE_NEUTRAL_POS 50  // Zatrzymanie silnika
#define ROPE_MAX_POS 60      // Prędkość max w górę (Zwijanie)

#define ROPE_RAMP_TIME_MS 2000 // Czas rozpędzania i hamowania (2 s)
#define ROPE_HOLD_TIME_MS 2500 // Czas wyciągania z pełną prędkością (2 s)

void rope_servo_thread(void *p1, void *p2, void *p3) {
    if (!pwm_is_ready_dt(&rope_servo)) return;

    bool is_animating = false;
    bool last_state = pull_rope;
    
    int anim_step = 0;
    int ramp_steps = ROPE_RAMP_TIME_MS / ROPE_FPS_MS;
    int hold_steps = ROPE_HOLD_TIME_MS / ROPE_FPS_MS;
    int total_steps = (ramp_steps * 2) + hold_steps; // Faza 1 + Faza 2 + Faza 3
    
    float target_speed = ROPE_NEUTRAL_POS;

    /* Wymuszenie fizycznego zatrzymania wyciągarki przy starcie systemu */
    uint32_t init_pulse = SERVO_MIN_PULSE_NS + 
        (ROPE_NEUTRAL_POS * (SERVO_MAX_PULSE_NS - SERVO_MIN_PULSE_NS)) / 100U;
    pwm_set_pulse_dt(&rope_servo, init_pulse);

    while (1) {
        // Wykrywamy start nowej sekwencji
        if (!is_animating && pull_rope != last_state) {
            is_animating = true;
            anim_step = 0;
            target_speed = pull_rope ? ROPE_MAX_POS : ROPE_MIN_POS;
            last_state = pull_rope;
        }

        if (is_animating) {
            float current_speed = ROPE_NEUTRAL_POS;

            if (anim_step < ramp_steps) {
                /* --- FAZA 1: Rozpędzanie (Smoothstep) --- */
                float progress = (float)anim_step / ramp_steps;
                float ease = progress * progress * (3.0f - 2.0f * progress);
                current_speed = ROPE_NEUTRAL_POS + (target_speed - ROPE_NEUTRAL_POS) * ease;
            } 
            else if (anim_step < ramp_steps + hold_steps) {
                /* --- FAZA 2: Praca (Utrzymanie prędkości) --- */
                current_speed = target_speed;
            } 
            else {
                /* --- FAZA 3: Hamowanie (Smoothstep do zera) --- */
                float progress = (float)(anim_step - ramp_steps - hold_steps) / ramp_steps;
                float ease = progress * progress * (3.0f - 2.0f * progress);
                // Rozpoczynamy od target_speed i powoli zdejmujemy wartość aż do NEUTRAL
                current_speed = target_speed + (ROPE_NEUTRAL_POS - target_speed) * ease;
            }

            /* Wysłanie obliczonej prędkości do sprzętu */
            uint32_t pulse = SERVO_MIN_PULSE_NS + 
                ((uint32_t)current_speed * (SERVO_MAX_PULSE_NS - SERVO_MIN_PULSE_NS)) / 100U;
            pwm_set_pulse_dt(&rope_servo, pulse);

            anim_step++;
            
            /* Zakończenie sekwencji i twarde wymuszenie stanu neutralnego (dla pewności) */
            if (anim_step >= total_steps) {
                is_animating = false; 
                pulse = SERVO_MIN_PULSE_NS + (ROPE_NEUTRAL_POS * (SERVO_MAX_PULSE_NS - SERVO_MIN_PULSE_NS)) / 100U;
                pwm_set_pulse_dt(&rope_servo, pulse);
            }
            
            k_msleep(ROPE_FPS_MS);
        } else {
            k_msleep(100);
        }
    }
}
K_THREAD_DEFINE(rope_tid, 1024, rope_servo_thread, NULL, NULL, NULL, 4, 0, 0);

int main(void) {
    console_init();
    printf("Sterowanie: [W/S] Sun, [E/D] Storm, [Z] Loundry, [X] Pull Rope\n");

    while (1) {
        uint8_t c = console_getchar();
        c = (uint8_t)tolower(c);
        
        if (c == 'w') sun_power = MIN(sun_power + 10, 100);
        if (c == 's') sun_power = MAX(sun_power - 10, 0);
        if (c == 'e') storm_intensity = MIN(storm_intensity + 10, 100);
        if (c == 'd') storm_intensity = MAX(storm_intensity - 10, 0);

        if (c == 'z') laundry_open = !laundry_open;
        if (c == 'x') pull_rope = !pull_rope;

        if (strchr("wsedzx", c) != NULL) {
            printf("Stan: Sun:%d, Storm:%d, Lndry:%s, Rope:%s\n", 
                    sun_power, storm_intensity, 
                    laundry_open ? "OPEN" : "CLOSED", 
                    pull_rope ? "PULLING" : "RELEASED");
        }
    }
    return 0;
}
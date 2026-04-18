#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/gpio.h> /* Wymagane dla wbudowanej diody */
#include <string.h>
#include <stdio.h>

/* --- Konfiguracja paska WS2812B --- */
#define STRIP_NODE DT_ALIAS(led_strip)
#define STRIP_NUM_LEDS DT_PROP(STRIP_NODE, chain_length)
#define SNAKE_LEN 4

/* --- Konfiguracja wbudowanej diody na płytce --- */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static struct led_rgb pixels[STRIP_NUM_LEDS];

int main(void)
{
	const struct device *gpio1_port = DEVICE_DT_GET(DT_NODELABEL(gpio1));

    if (!device_is_ready(gpio1_port)) {
        printf("BLAD: Port GPIO1 nie jest gotowy!\n");
    } else {
        /* 2. Konfigurujemy pin nr 5 jako wyjście i od razu wymuszamy stan wysoki (ACTIVE) */
        int err = gpio_pin_configure(gpio1_port, 5, GPIO_OUTPUT_ACTIVE);
        
        if (err == 0) {
            printf("TEST: Pin P1.05 ustawiony na HIGH. Mozesz mierzyc!\n");
        } else {
            printf("TEST: Blad konfiguracji pinu P1.05: %d\n", err);
        }
    }
	const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
	int head_pos = 0;

	/* 1. Sprawdzenie paska LED */
	if (!device_is_ready(strip))
	{
		printf("BLAD: Pasek LED nie jest gotowy!\n");
		return 0;
	}

	/* 2. Sprawdzenie i konfiguracja wbudowanej diody (Heartbeat) */
	if (!gpio_is_ready_dt(&led0))
	{
		printf("BLAD: Dioda LED0 nie jest gotowa!\n");
		return 0;
	}
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);

	printf("--- Start programu: Waz WS2812B + Heartbeat LED0 ---\n");

	while (1)
	{
		/* Wyczyść pasek i narysuj węża */
		memset(pixels, 0, sizeof(pixels));

		for (int i = 0; i < SNAKE_LEN; i++)
		{
			int pos = (head_pos - i + STRIP_NUM_LEDS) % STRIP_NUM_LEDS;
			pixels[pos].b = 255 >> i;
		}

		/* Wyślij dane do WS2812B */
		led_strip_update_rgb(strip, pixels, STRIP_NUM_LEDS);

		/* Wypisz log do konsoli (UART) */
		printf("Waz zyje! Pozycja glowy: %d\n", head_pos);

		/* Zmiana stanu wbudowanej diody (Toggle) przy każdym przejściu pętli */
		gpio_pin_toggle_dt(&led0);

		/* Przesunięcie pozycji i uśpienie wątku */
		head_pos = (head_pos + 1) % STRIP_NUM_LEDS;
		k_msleep(100);
	}
	return 0;
}
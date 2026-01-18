// main.c
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "tusb.h"
#include "protocol.h"
#include "i2c_handler.h"
#include "uart_handler.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"

// Очередь для передачи пакетов между USB-ядром (0) и логическим ядром (1)
static queue_t packet_queue;

/**
 * ЯДРО 1: Исполнение команд
 * Здесь происходит вся работа с периферией, чтобы не блокировать USB-стек.
 */
void core1_entry() {
    usb_packet_t pkt;
    printf("PicoLink: Core 1 Executor Ready\n");
    
    while (1) {
        // Блокирующее ожидание пакета из очереди
        queue_remove_blocking(&packet_queue, &pkt);
        
        picolink_header_t *hdr = &pkt.header;

        // --- 1. ОБРАБОТКА I2C ---
        if (hdr->iface_idx == IFACE_I2C) {
            picolink_i2c_handle(&pkt);
            continue;
        }

        else if (hdr->iface_idx == IFACE_UART) {
            picolink_uart_handle(&pkt); // Твой новый обработчик
        }

        else if (hdr->iface_idx == IFACE_ADC) {
            uint8_t pin = pkt.payload[0]; // Мы будем передавать номер GP-пина

            if (hdr->type == CMD_TYPE_READ) {
                uint8_t input_ch;
                if (pin >= 26 && pin <= 28) input_ch = pin - 26; // Каналы 0-2
                else if (pin == 29) input_ch = 4; // Датчик температуры
                else continue;

                adc_select_input(input_ch);
                uint16_t result = adc_read();

                usb_packet_t resp_pkt;
                memset(&resp_pkt, 0, sizeof(resp_pkt));
                resp_pkt.header.type = CMD_TYPE_RESP;
                resp_pkt.header.iface_idx = IFACE_ADC;
                resp_pkt.header.length = 3; // Pin + 2 bytes value
                resp_pkt.payload[0] = pin;
                resp_pkt.payload[1] = (result >> 8) & 0xFF; // Старший байт
                resp_pkt.payload[2] = result & 0xFF;        // Младший байт

                tud_vendor_write(&resp_pkt, sizeof(resp_pkt));
                tud_vendor_write_flush();
                printf("ADC RD: Pin %d = %d\n", pin, result);
            }
        }

        // --- 2. ОБРАБОТКА GPIO ---
        else if (hdr->iface_idx == IFACE_GPIO) {
            uint8_t pin = pkt.payload[0];
            
            if (pin >= 30) continue; // Защита от выхода за пределы пинов

            if (hdr->type == CMD_TYPE_CONFIG) {
                uint8_t mode = pkt.payload[1]; 
                gpio_init(pin);
                
                if (mode == GPIO_MODE_OUT) {
                    gpio_set_dir(pin, GPIO_OUT);
                } else {
                    gpio_set_dir(pin, GPIO_IN);
                    // Управление подтяжками
                    gpio_set_pulls(pin, (mode == GPIO_MODE_IN_PULLUP), (mode == GPIO_MODE_IN_PULLDOWN));
                }
                printf("GPIO CFG: Pin %d -> Mode %d\n", pin, mode);
            } 
            else if (hdr->type == CMD_TYPE_DATA) {
                uint8_t value = pkt.payload[1];
                gpio_put(pin, value);
                printf("GPIO WR: Pin %d = %d\n", pin, value);
            }
            else if (hdr->type == CMD_TYPE_READ) {
                usb_packet_t resp_pkt;
                memset(&resp_pkt, 0, sizeof(resp_pkt));
                
                resp_pkt.header.type = CMD_TYPE_RESP;
                resp_pkt.header.iface_idx = IFACE_GPIO;
                resp_pkt.header.length = 2;
                resp_pkt.payload[0] = pin;
                resp_pkt.payload[1] = gpio_get(pin);

                tud_vendor_write(&resp_pkt, sizeof(resp_pkt));
                tud_vendor_write_flush();
                printf("GPIO RD: Pin %d is %d\n", pin, resp_pkt.payload[1]);
            }
        }
                // --- 3. ОБРАБОТКА PWM ---
            else if (hdr->iface_idx == IFACE_PWM) {
            uint8_t pin = pkt.payload[0];
            if (pin >= 30) continue;

            if (hdr->type == CMD_TYPE_CONFIG) {
                // Инициализация PWM на пине
                gpio_set_function(pin, GPIO_FUNC_PWM);
                uint slice_num = pwm_gpio_to_slice_num(pin);
                
                // Установим стандартную частоту (примерно 1 кГц при 125МГц)
                pwm_set_clkdiv(slice_num, 125.0f); 
                pwm_set_wrap(slice_num, 1000); // Диапазон 0-1000
                pwm_set_enabled(slice_num, true);
                printf("PWM CFG: Pin %d initialized\n", pin);
            }
            else if (hdr->type == CMD_TYPE_DATA) {
                // Установка скважности (Duty Cycle)
                uint16_t duty = (pkt.payload[1] << 8) | pkt.payload[2]; // Собираем 16-бит значение
                pwm_set_gpio_level(pin, duty);
                printf("PWM WR: Pin %d -> Duty %d\n", pin, duty);
            }
        }
    }
}

/**
 * ЯДРО 0: USB-стек и инициализация
 */
int main() {
    // Инициализация стандартного вывода (UART/USB-Serial)
    stdio_init_all();
    
    // Очередь на 16 пакетов. Размер очереди важен для предотвращения потерь при сканировании I2C
    queue_init(&packet_queue, sizeof(usb_packet_t), 16);

    // Небольшая задержка, чтобы успеть подключить монитор порта
    sleep_ms(1000);
    printf("--- PicoLink MFD Bridge ---\n");
    adc_init();

    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);

    // Запуск USB и второго ядра
    tusb_init();
    multicore_launch_core1(core1_entry);

    while (1) {
        tud_task(); // Обслуживание USB событий TinyUSB
    }
    return 0;
}

/**
 * Callback: Вызывается TinyUSB при получении данных от хоста (Linux)
 */
void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    usb_packet_t rx_pkt;
    
    // Читаем пакет целиком
    uint32_t read_bytes = tud_vendor_n_read(itf, &rx_pkt, sizeof(rx_pkt));

    if (read_bytes >= sizeof(picolink_header_t)) {
        // Отладочный вывод только для I2C, чтобы видеть активность i2cdetect
        if (rx_pkt.header.iface_idx == IFACE_I2C) {
            printf("USB RX I2C: type=0x%02x\n", rx_pkt.header.type);
        }

        // Кладём пакет в очередь для Ядра 1
        if (!queue_try_add(&packet_queue, &rx_pkt)) {
            // Если очередь полна, Linux драйвер получит таймаут
            // printf("ERR: Queue Full!\n");
        }
    }
}
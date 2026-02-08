#include "pwm_handler.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "protocol.h"
#include <string.h>

void picolink_log(const char *format, ...);

void picolink_pwm_handle(usb_packet_t *pkt) {
    picolink_header_t *hdr = &pkt->header;
    
    // --- CONFIGURATION ---
    if (hdr->type == CMD_TYPE_CONFIG) {
        // 1. Безопасное копирование из упакованного пакета в локальную структуру
        // Это предотвращает ошибки выравнивания (unaligned access)
        pwm_config_t cfg;
        if (hdr->length < sizeof(pwm_config_t)) return; // Защита от битых пакетов
        memcpy(&cfg, pkt->payload, sizeof(pwm_config_t));

        if (cfg.pin >= 30) return;

        // 2. Инициализация GPIO
        gpio_set_function(cfg.pin, GPIO_FUNC_PWM);
        uint slice_num = pwm_gpio_to_slice_num(cfg.pin);

        pwm_config hw_cfg = pwm_get_default_config();
        
        // 3. Установка параметров
        // RP2040 поддерживает int 8 бит (0-255) и frac 4 бита (0-15)
        uint8_t div_int = (cfg.clkdiv_int > 255) ? 255 : (uint8_t)cfg.clkdiv_int;
        pwm_config_set_clkdiv_int_frac(&hw_cfg, div_int, cfg.clkdiv_frac);
        pwm_config_set_wrap(&hw_cfg, cfg.wrap);
        
        // Настройка фазовой коррекции (bit 0)
        pwm_config_set_phase_correct(&hw_cfg, (cfg.options & 0x01));
        
        // 4. Применение конфигурации
        pwm_init(slice_num, &hw_cfg, true);

        // 5. Настройка полярности
        // bit 1: invert A, bit 2: invert B
        bool inv_a = (cfg.options & 0x02) != 0;
        bool inv_b = (cfg.options & 0x04) != 0;
        pwm_set_output_polarity(slice_num, inv_a, inv_b);

        picolink_log("PWM CFG: Pin%d Sl%d Wrp%u Div%d.%d", 
                     cfg.pin, slice_num, cfg.wrap, div_int, cfg.clkdiv_frac);
    } 
    // --- DATA UPDATE (DUTY CYCLE) ---
    else if (hdr->type == CMD_TYPE_DATA) {
        // payload[0] = pin
        // payload[1] = level LSB
        // payload[2] = level MSB
        // Используем Little Endian, так как это стандарт для USB и Linux ядра
        
        if (hdr->length < 3) return;

        uint8_t pin = pkt->payload[0];
        if (pin >= 30) return;

        if (gpio_get_function(pin) != GPIO_FUNC_PWM) {
            gpio_set_function(pin, GPIO_FUNC_PWM);
            uint slice_num = pwm_gpio_to_slice_num(pin);
            pwm_config hw_cfg = pwm_get_default_config();
            
            // Для серво и нормального PWM ставим wrap побольше
            pwm_config_set_wrap(&hw_cfg, 65535); 
            // Ставим делитель 40, чтобы расширить диапазон в область низких частот
            pwm_config_set_clkdiv(&hw_cfg, 40.0f); 
            
            pwm_init(slice_num, &hw_cfg, true);
            picolink_log("PWM Auto-Init 16-bit: Pin %d", pin);
        }

        uint16_t level = (uint16_t)pkt->payload[1] | ((uint16_t)pkt->payload[2] << 8);
        
        pwm_set_gpio_level(pin, level);
    }
    // --- DISABLE ---
    else if (hdr->type == CMD_TYPE_DISABLE) {
        uint8_t pin = pkt->payload[0];
        if (pin >= 30) return;
        
        // Чтобы отключить PWM корректно:
        // 1. Возвращаем пин в обычный режим GPIO (чтобы не было паразитных сигналов)
        gpio_set_function(pin, GPIO_FUNC_SIO); 
        gpio_set_dir(pin, GPIO_IN);

        // Опционально: можно выключить сам слайс, если второй канал не используется,
        // но это сложнее отследить. Безопаснее просто отключить пин от PWM.
        
        picolink_log("PWM Disabled: GP%d", pin);
    }
}
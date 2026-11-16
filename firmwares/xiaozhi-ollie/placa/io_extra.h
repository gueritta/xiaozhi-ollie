// Ferramentas de I/O do Ollie: cor do LED RGB, barramento I2C (Grove J5 e header 2x4)
// e os GPIOs livres do header. Todas aparecem ao modelo como ferramentas MCP do aparelho,
// então o bot pode usá-las por voz sem nenhum código no servidor.
//
// GPIOs livres: OLLIE_GPIO_LIVRES no firmwares/xiaozhi-ollie/local.env (ex.: "4,5").
// Vazio (padrão) = as ferramentas de GPIO/PWM recusam tudo — só LED e I2C ficam ativos.
#ifndef _IO_EXTRA_H_
#define _IO_EXTRA_H_

#ifndef OLLIE_GPIO_LIVRES
#define OLLIE_GPIO_LIVRES "{{LIVRES}}"
#endif

// UART para falar com outra placa (ESP32, Flipper Zero...): OLLIE_UART_TX/RX no local.env.
// Vazio = as ferramentas self.uart.* recusam (nada é inicializado no boot).
#ifndef OLLIE_UART_TX
#define OLLIE_UART_TX "{{UART_TX}}"
#endif
#ifndef OLLIE_UART_RX
#define OLLIE_UART_RX "{{UART_RX}}"
#endif
#ifndef OLLIE_UART_BAUD
#define OLLIE_UART_BAUD {{UART_BAUD}}
#endif

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_adc/adc_oneshot.h>
#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <led/circular_strip.h>
#include <mcp_server.h>

#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

class IoExtra {
public:
    // barramento: i2c_bus_ da placa (I2C0 = Grove 47/48 + header). faixa: LED RGB (GPIO40).
    static void Instalar(i2c_master_bus_handle_t barramento, CircularStrip* faixa) {
        barramento_ = barramento;
        faixa_ = faixa;
        LerPinosLivres();
        MapearDispositivosInternos();
        RegistrarFerramentas();
    }

private:
    static inline i2c_master_bus_handle_t barramento_ = nullptr;
    static inline CircularStrip* faixa_ = nullptr;
    static inline std::set<int> internos_;      // endereços que já existiam no boot (não escrever)
    static inline std::set<int> pinos_livres_;  // allowlist de GPIO
    static inline int canal_pwm_ = 0;
    static inline int uart_tx_ = -1;
    static inline int uart_rx_ = -1;
    static inline int uart_baud_ = OLLIE_UART_BAUD;
    static inline bool uart_pronto_ = false;
    static inline adc_oneshot_unit_handle_t adc_ = nullptr;

    static std::vector<std::string> Dividir(const std::string& texto, char sep) {
        std::vector<std::string> partes;
        std::string atual;
        std::istringstream fluxo(texto);
        while (std::getline(fluxo, atual, sep)) {
            if (!atual.empty()) partes.push_back(atual);
        }
        return partes;
    }

    // atoi seguro: o firmware é compilado com -fno-exceptions, então nada de try/catch
    // Sobe (ou reconfigura) a UART1 só quando alguém usa a ponte — nada no boot.
    static bool AbrirUart() {
        if (uart_tx_ < 0 || uart_rx_ < 0) return false;
        if (uart_pronto_) return true;
        uart_driver_delete(UART_NUM_1);
        uart_config_t cfg = {};
        cfg.baud_rate = uart_baud_;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;
        if (uart_driver_install(UART_NUM_1, 4096, 4096, 0, nullptr, 0) != ESP_OK) return false;
        if (uart_param_config(UART_NUM_1, &cfg) != ESP_OK) return false;
        if (uart_set_pin(UART_NUM_1, uart_tx_, uart_rx_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
            return false;
        }
        uart_pronto_ = true;
        ESP_LOGI("IoExtra", "UART1 pronta: tx=%d rx=%d baud=%d", uart_tx_, uart_rx_, uart_baud_);
        return true;
    }

    static bool Inteiro(const std::string& texto, int base, long& saida) {
        if (texto.empty()) return false;
        char* fim = nullptr;
        saida = strtol(texto.c_str(), &fim, base);
        return fim != nullptr && *fim == '\0';
    }

    static void LerPinosLivres() {
        for (const auto& parte : Dividir(OLLIE_GPIO_LIVRES, ',')) {
            long pino = 0;
            if (Inteiro(parte, 10, pino) && pino >= 0 && pino <= 48) {
                pinos_livres_.insert((int)pino);
            } else {
                ESP_LOGW("IoExtra", "pino inválido em OLLIE_GPIO_LIVRES: %s", parte.c_str());
            }
        }
        long tx = 0, rx = 0;
        if (Inteiro(OLLIE_UART_TX, 10, tx) && Inteiro(OLLIE_UART_RX, 10, rx)) {
            uart_tx_ = (int)tx;
            uart_rx_ = (int)rx;
        }
        ESP_LOGI("IoExtra", "GPIOs livres: %d | UART tx=%d rx=%d baud=%d", (int)pinos_livres_.size(), uart_tx_,
                 uart_rx_, uart_baud_);
    }

    // Guarda o que já respondia na I2C antes de existir ferramenta: codec de áudio,
    // expansor de IO, touch. Escrever nesses endereços derrubaria áudio/tela.
    static void MapearDispositivosInternos() {
        if (barramento_ == nullptr) return;
        for (int endereco = 0x08; endereco < 0x78; endereco++) {
            if (i2c_master_probe(barramento_, endereco, 20) == ESP_OK) {
                internos_.insert(endereco);
            }
        }
        std::string lista;
        for (int e : internos_) {
            char buf[8];
            snprintf(buf, sizeof(buf), "0x%02X ", e);
            lista += buf;
        }
        ESP_LOGI("IoExtra", "I2C interna (protegida): %s", lista.c_str());
    }

    static bool PinoPermitido(int pino, std::string& erro) {
        if (pinos_livres_.empty()) {
            erro = "nenhum GPIO livre configurado: defina OLLIE_GPIO_LIVRES no local.env e recompile";
            return false;
        }
        if (!pinos_livres_.count(pino)) {
            std::string lista;
            for (int p : pinos_livres_) lista += std::to_string(p) + " ";
            erro = "pino " + std::to_string(pino) + " não está na lista permitida (" + lista + ")";
            return false;
        }
        return true;
    }

    static std::string Hex(const uint8_t* dados, int n) {
        static const char* digitos = "0123456789abcdef";
        std::string saida;
        for (int i = 0; i < n; i++) {
            if (i) saida += ' ';
            saida += digitos[dados[i] >> 4];
            saida += digitos[dados[i] & 0x0F];
        }
        return saida;
    }

    static void RegistrarFerramentas() {
        auto& mcp = McpServer::GetInstance();

        // ---- LED RGB (ws2813 em GPIO40) --------------------------------------
        mcp.AddTool("self.led.set_color",
                    "Sets the RGB LED colour on the device. Use for mood lights, status colours or "
                    "when the user asks to change the light. Values 0-255. brightness is 0-100 "
                    "(optional, 100 = the board's normal brightness).",
                    PropertyList({Property("red", kPropertyTypeInteger, 0, 255),
                                  Property("green", kPropertyTypeInteger, 0, 255),
                                  Property("blue", kPropertyTypeInteger, 0, 255),
                                  Property("brightness", kPropertyTypeInteger, 100, 0, 100)}),
                    [](const PropertyList& p) -> ReturnValue {
                        if (faixa_ == nullptr) return std::string("erro: LED não inicializado");
                        StripColor cor = {(uint8_t)p["red"].value<int>(), (uint8_t)p["green"].value<int>(),
                                          (uint8_t)p["blue"].value<int>()};
                        int brilho = p["brightness"].value<int>();
                        if (brilho < 100) {
                            int maximo = brilho * 255 / 100;
                            cor.red = (uint8_t)(cor.red * maximo / 255);
                            cor.green = (uint8_t)(cor.green * maximo / 255);
                            cor.blue = (uint8_t)(cor.blue * maximo / 255);
                        }
                        faixa_->SetTravada(true);  // a cor do usuário vence os estados do aparelho
                        faixa_->SetAllColor(cor);
                        char msg[80];
                        snprintf(msg, sizeof(msg), "LED em rgb(%d, %d, %d)", cor.red, cor.green, cor.blue);
                        return std::string(msg);
                    });

        mcp.AddTool("self.led.effect",
                    "Animates the RGB LED. mode: 'solid', 'breath', 'blink' or 'off'. Colours 0-255.",
                    PropertyList({Property("mode", kPropertyTypeString),
                                  Property("red", kPropertyTypeInteger, 0, 0, 255),
                                  Property("green", kPropertyTypeInteger, 0, 0, 255),
                                  Property("blue", kPropertyTypeInteger, 0, 0, 255)}),
                    [](const PropertyList& p) -> ReturnValue {
                        if (faixa_ == nullptr) return std::string("erro: LED não inicializado");
                        std::string modo = p["mode"].value<std::string>();
                        StripColor cor = {(uint8_t)p["red"].value<int>(), (uint8_t)p["green"].value<int>(),
                                          (uint8_t)p["blue"].value<int>()};
                        StripColor apagado = {0, 0, 0};
                        faixa_->SetTravada(true);
                        if (modo == "off") {
                            faixa_->SetAllColor(apagado);
                        } else if (modo == "breath") {
                            faixa_->Breathe(apagado, cor, 40);
                        } else if (modo == "blink") {
                            faixa_->Blink(cor, 400);
                        } else if (modo == "solid") {
                            faixa_->SetAllColor(cor);
                        } else {
                            return std::string("erro: mode deve ser solid, breath, blink ou off");
                        }
                        return std::string("efeito ") + modo + " aplicado";
                    });

        mcp.AddTool("self.led.release",
                    "Hands the RGB LED back to the device's own states (idle/listening/speaking).",
                    PropertyList(), [](const PropertyList&) -> ReturnValue {
                        if (faixa_ == nullptr) return std::string("erro: LED não inicializado");
                        faixa_->SetTravada(false);
                        faixa_->OnStateChanged();
                        return true;
                    });

        // ---- I2C (Grove J5 + header 2x4, barramento I2C0) --------------------
        mcp.AddTool("self.i2c.scan",
                    "Lists the I2C addresses that answer on the Watcher's Grove/header bus. Use it "
                    "before reading a sensor so you know the address.",
                    PropertyList(), [](const PropertyList&) -> ReturnValue {
                        if (barramento_ == nullptr) return std::string("erro: I2C não inicializado");
                        std::string achados;
                        for (int endereco = 0x08; endereco < 0x78; endereco++) {
                            if (i2c_master_probe(barramento_, endereco, 20) == ESP_OK) {
                                char buf[8];
                                snprintf(buf, sizeof(buf), "0x%02X ", endereco);
                                achados += buf;
                            }
                        }
                        if (achados.empty()) return std::string("nenhum dispositivo I2C respondeu");
                        return "I2C: " + achados + (achados.find_first_not_of("0123456789abcdefx ") == std::string::npos ? "" : "");
                    });

        mcp.AddTool("self.i2c.read",
                    "Reads bytes from an I2C device. Use register >= 0 to read a register, or -1 for "
                    "a plain read. length is 1-32 bytes.",
                    PropertyList({Property("address", kPropertyTypeInteger, 0x08, 0x77),
                                  Property("register", kPropertyTypeInteger, -1, -1, 255),
                                  Property("length", kPropertyTypeInteger, 1, 1, 32)}),
                    [](const PropertyList& p) -> ReturnValue {
                        if (barramento_ == nullptr) return std::string("erro: I2C não inicializado");
                        int endereco = p["address"].value<int>();
                        int registrador = p["register"].value<int>();
                        int n = p["length"].value<int>();
                        i2c_device_config_t cfg = {
                            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                            .device_address = (uint16_t)endereco,
                            .scl_speed_hz = 100000,
                        };
                        i2c_master_dev_handle_t dev = nullptr;
                        if (i2c_master_bus_add_device(barramento_, &cfg, &dev) != ESP_OK) {
                            return std::string("erro: endereço inválido");
                        }
                        uint8_t dados[32] = {0};
                        esp_err_t erro;
                        if (registrador >= 0) {
                            uint8_t reg = (uint8_t)registrador;
                            erro = i2c_master_transmit_receive(dev, &reg, 1, dados, n, 200);
                        } else {
                            erro = i2c_master_receive(dev, dados, n, 200);
                        }
                        i2c_master_bus_rm_device(dev);
                        if (erro != ESP_OK) return std::string("erro I2C: ") + esp_err_to_name(erro);
                        return Hex(dados, n);
                    });

        mcp.AddTool("self.i2c.write",
                    "Writes bytes to an I2C device. data is hex bytes separated by spaces, e.g. "
                    "\"01 00 80\". Refuses addresses that belong to the device itself (audio, screen, "
                    "IO expander) to avoid bricking sound or video.",
                    PropertyList({Property("address", kPropertyTypeInteger, 0x08, 0x77),
                                  Property("register", kPropertyTypeInteger, -1, -1, 255),
                                  Property("data", kPropertyTypeString)}),
                    [](const PropertyList& p) -> ReturnValue {
                        if (barramento_ == nullptr) return std::string("erro: I2C não inicializado");
                        int endereco = p["address"].value<int>();
                        if (internos_.count(endereco)) {
                            char buf[96];
                            snprintf(buf, sizeof(buf),
                                     "erro: 0x%02X é um dispositivo interno do Watcher (áudio/tela); "
                                     "escrita bloqueada", endereco);
                            return std::string(buf);
                        }
                        std::vector<uint8_t> bytes;
                        for (const auto& parte : Dividir(p["data"].value<std::string>(), ' ')) {
                            long valor = 0;
                            if (!Inteiro(parte, 16, valor) || valor < 0 || valor > 255) {
                                return std::string("erro: data deve ser hex, ex. \"01 ff 80\"");
                            }
                            bytes.push_back((uint8_t)valor);
                        }
                        if (bytes.empty()) return std::string("erro: data vazia");
                        int registrador = p["register"].value<int>();
                        if (registrador >= 0) bytes.insert(bytes.begin(), (uint8_t)registrador);
                        i2c_device_config_t cfg = {
                            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                            .device_address = (uint16_t)endereco,
                            .scl_speed_hz = 100000,
                        };
                        i2c_master_dev_handle_t dev = nullptr;
                        if (i2c_master_bus_add_device(barramento_, &cfg, &dev) != ESP_OK) {
                            return std::string("erro: endereço inválido");
                        }
                        esp_err_t erro = i2c_master_transmit(dev, bytes.data(), bytes.size(), 200);
                        i2c_master_bus_rm_device(dev);
                        if (erro != ESP_OK) return std::string("erro I2C: ") + esp_err_to_name(erro);
                        return "escrito " + std::to_string(bytes.size()) + " byte(s)";
                    });

        // ---- GPIO / PWM nos pinos livres do header --------------------------
        mcp.AddTool("self.gpio.list",
                    "Lists the GPIO pins available on the Watcher's expansion header and what can be "
                    "done with each (digital out/in, PWM).",
                    PropertyList(), [](const PropertyList&) -> ReturnValue {
                        if (pinos_livres_.empty()) {
                            return std::string("nenhum GPIO livre: defina OLLIE_GPIO_LIVRES no local.env");
                        }
                        std::string lista = "GPIOs livres (digital + PWM): ";
                        for (int p : pinos_livres_) lista += std::to_string(p) + " ";
                        return lista;
                    });

        mcp.AddTool("self.gpio.set",
                    "Drives an expansion-header GPIO high (1) or low (0). Only pins from "
                    "self.gpio.list are accepted.",
                    PropertyList({Property("pin", kPropertyTypeInteger, 0, 48),
                                  Property("level", kPropertyTypeInteger, 0, 1)}),
                    [](const PropertyList& p) -> ReturnValue {
                        int pino = p["pin"].value<int>();
                        std::string erro;
                        if (!PinoPermitido(pino, erro)) return erro;
                        if (uart_pronto_ && (pino == uart_tx_ || pino == uart_rx_)) {
                            return "erro: GPIO " + std::to_string(pino) +
                                   " está em uso pela UART (self.uart); reconfigura a UART antes";
                        }
                        gpio_config_t cfg = {};
                        cfg.pin_bit_mask = 1ULL << pino;
                        cfg.mode = GPIO_MODE_INPUT_OUTPUT;
                        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
                        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
                        cfg.intr_type = GPIO_INTR_DISABLE;
                        gpio_config(&cfg);
                        gpio_set_level((gpio_num_t)pino, p["level"].value<int>());
                        return "GPIO " + std::to_string(pino) + " = " + std::to_string(p["level"].value<int>());
                    });

        mcp.AddTool("self.gpio.get",
                    "Reads an expansion-header GPIO: 0 or 1.",
                    PropertyList({Property("pin", kPropertyTypeInteger, 0, 48)}),
                    [](const PropertyList& p) -> ReturnValue {
                        int pino = p["pin"].value<int>();
                        std::string erro;
                        if (!PinoPermitido(pino, erro)) return erro;
                        if (uart_pronto_ && (pino == uart_tx_ || pino == uart_rx_)) {
                            return "erro: GPIO " + std::to_string(pino) +
                                   " está em uso pela UART (self.uart); reconfigura a UART antes";
                        }
                        gpio_config_t cfg = {};
                        cfg.pin_bit_mask = 1ULL << pino;
                        cfg.mode = GPIO_MODE_INPUT;
                        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
                        cfg.intr_type = GPIO_INTR_DISABLE;
                        gpio_config(&cfg);
                        return gpio_get_level((gpio_num_t)pino);
                    });

        mcp.AddTool("self.gpio.pwm",
                    "Sets PWM on an expansion-header pin (servo, fan, dimmer). freq in Hz (50-40000), "
                    "duty 0-100%.",
                    PropertyList({Property("pin", kPropertyTypeInteger, 0, 48),
                                  Property("freq", kPropertyTypeInteger, 1000, 50, 40000),
                                  Property("duty", kPropertyTypeInteger, 0, 100)}),
                    [](const PropertyList& p) -> ReturnValue {
                        int pino = p["pin"].value<int>();
                        std::string erro;
                        if (!PinoPermitido(pino, erro)) return erro;
                        if (canal_pwm_ >= 8) return std::string("erro: sem canal PWM livre");
                        ledc_channel_t canal = (ledc_channel_t)canal_pwm_++;
                        ledc_timer_config_t timer = {};
                        timer.speed_mode = LEDC_LOW_SPEED_MODE;
                        timer.duty_resolution = LEDC_TIMER_10_BIT;
                        timer.timer_num = (ledc_timer_t)((int)canal % 4);
                        timer.freq_hz = p["freq"].value<int>();
                        timer.clk_cfg = LEDC_AUTO_CLK;
                        ledc_timer_config(&timer);
                        ledc_channel_config_t ch = {};
                        ch.gpio_num = pino;
                        ch.speed_mode = LEDC_LOW_SPEED_MODE;
                        ch.channel = canal;
                        ch.timer_sel = timer.timer_num;
                        ch.duty = p["duty"].value<int>() * 1023 / 100;
                        ch.hpoint = 0;
                        ledc_channel_config(&ch);
                        return "PWM no GPIO " + std::to_string(pino) + " a " + std::to_string(p["freq"].value<int>()) +
                               " Hz, duty " + std::to_string(p["duty"].value<int>()) + "%";
                    });

        // ---- ADC nos pinos livres (ADC1 = GPIO1..GPIO10 no S3) --------------
        mcp.AddTool("self.adc.read",
                    "Reads the analog value (0-4095) of an expansion-header GPIO that supports ADC "
                    "(ESP32-S3 ADC1: GPIO1-GPIO10). Only allowlisted pins are accepted.",
                    PropertyList({Property("pin", kPropertyTypeInteger, 0, 48)}),
                    [](const PropertyList& p) -> ReturnValue {
                        int pino = p["pin"].value<int>();
                        std::string erro;
                        if (!PinoPermitido(pino, erro)) return erro;
                        if (pino < 1 || pino > 10) {
                            return std::string("erro: GPIO ") + std::to_string(pino) +
                                   " não é ADC1 (só 1..10 no ESP32-S3)";
                        }
                        if (adc_ == nullptr) {
                            adc_oneshot_unit_init_cfg_t cfg_unidade = {};
                            cfg_unidade.unit_id = ADC_UNIT_1;
                            cfg_unidade.ulp_mode = ADC_ULP_MODE_DISABLE;
                            if (adc_oneshot_new_unit(&cfg_unidade, &adc_) != ESP_OK) {
                                return std::string("erro: ADC indisponível");
                            }
                        }
                        adc_oneshot_chan_cfg_t cfg_canal = {};
                        cfg_canal.atten = ADC_ATTEN_DB_12;
                        cfg_canal.bitwidth = ADC_BITWIDTH_DEFAULT;
                        adc_channel_t canal = (adc_channel_t)(pino - 1);
                        if (adc_oneshot_config_channel(adc_, canal, &cfg_canal) != ESP_OK) {
                            return std::string("erro: canal ADC inválido");
                        }
                        int valor = 0;
                        if (adc_oneshot_read(adc_, canal, &valor) != ESP_OK) {
                            return std::string("erro: leitura ADC falhou");
                        }
                        return valor;
                    });

        // ---- UART: ponte serial com outra placa (ESP32, Flipper Zero...) ----
        mcp.AddTool("self.uart.config",
                    "Configures the UART bridge to another board: baud rate and, optionally, the TX/RX "
                    "pins (only allowlisted pins). Call it before self.uart.write when the defaults "
                    "do not fit.",
                    PropertyList({Property("baud", kPropertyTypeInteger, OLLIE_UART_BAUD, 300, 921600),
                                  Property("tx", kPropertyTypeInteger, -1, -1, 48),
                                  Property("rx", kPropertyTypeInteger, -1, -1, 48)}),
                    [](const PropertyList& p) -> ReturnValue {
                        int baud = p["baud"].value<int>();
                        int tx = p["tx"].value<int>();
                        int rx = p["rx"].value<int>();
                        std::string erro;
                        if (tx >= 0 && !PinoPermitido(tx, erro)) return erro;
                        if (rx >= 0 && !PinoPermitido(rx, erro)) return erro;
                        if (tx >= 0) uart_tx_ = tx;
                        if (rx >= 0) uart_rx_ = rx;
                        uart_baud_ = baud;
                        if (uart_tx_ < 0 || uart_rx_ < 0) {
                            return std::string("erro: informe tx e rx (ou configure OLLIE_UART_TX/RX no local.env)");
                        }
                        uart_pronto_ = false;  // reconfigura na próxima escrita/leitura
                        return "UART em " + std::to_string(uart_baud_) + " baud, tx=" + std::to_string(uart_tx_) +
                               " rx=" + std::to_string(uart_rx_);
                    });

        mcp.AddTool("self.uart.write",
                    "Sends text over the UART bridge (adds no line ending unless you include it). Use "
                    "\n at the end for line-based protocols.",
                    PropertyList({Property("text", kPropertyTypeString)}),
                    [](const PropertyList& p) -> ReturnValue {
                        if (!AbrirUart()) {
                            return std::string("erro: UART não configurada (self.uart.config tx/rx/baud)");
                        }
                        std::string texto = p["text"].value<std::string>();
                        int enviados = uart_write_bytes(UART_NUM_1, texto.data(), texto.size());
                        if (enviados < 0) return std::string("erro: falha ao enviar");
                        uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(200));
                        return "enviados " + std::to_string(enviados) + " byte(s)";
                    });

        mcp.AddTool("self.uart.read",
                    "Reads whatever arrived on the UART bridge within timeout_ms (50-3000). Returns text "
                    "plus a hex dump when the bytes are not printable (binary protocols).",
                    PropertyList({Property("timeout_ms", kPropertyTypeInteger, 400, 50, 3000)}),
                    [](const PropertyList& p) -> ReturnValue {
                        if (!AbrirUart()) {
                            return std::string("erro: UART não configurada (self.uart.config tx/rx/baud)");
                        }
                        uint8_t buf[256] = {0};
                        int n = uart_read_bytes(UART_NUM_1, buf, sizeof(buf),
                                                pdMS_TO_TICKS(p["timeout_ms"].value<int>()));
                        if (n <= 0) return std::string("nada recebido");
                        bool imprimivel = true;
                        for (int i = 0; i < n; i++) {
                            if (buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t' &&
                                (buf[i] < 32 || buf[i] > 126)) {
                                imprimivel = false;
                                break;
                            }
                        }
                        if (imprimivel) return std::string((char*)buf, n);
                        return std::to_string(n) + " byte(s) hex: " + Hex(buf, n);
                    });
    }
};

#endif  // _IO_EXTRA_H_
